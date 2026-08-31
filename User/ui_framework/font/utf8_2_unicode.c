/*
 * utf8_2_unicode.c —— UTF-8 → UTF-16LE 解码
 *
 * 【来源】从 cpu/br27/liba/font.a 的 utf8_2_unicode.c.o 还原。该库交付的是
 *   LLVM bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/utf8_2_unicode.ll
 *     原始路径: btsdk/lib/utils/ui/font/utf8_2_unicode.c
 *
 * 【还原依据】
 *   函数原始行号(DISubprogram): get_utf8_size@18  utf8_2_unicode_one@47
 *                              utf8_2_unicode@132
 *   本文件按此顺序排列。本模块【没有 ASSERT】, 也不含任何字符串常量,
 *   所以不需要像 imd.c 那样把行号钉死(没有 __FILE__/__LINE__ 参与编译)。
 *   形参与局部变量名、类型全部取自 DWARF:
 *     get_utf8_size(const unsigned char pInput), 局部 unsigned char ch
 *     utf8_2_unicode_one(const unsigned char *in, unsigned short *unicode),
 *       局部 char b1..b6 / int utfbytes / unsigned char *out
 *       —— b1..b6 是【有符号 char】, 这是 IR 里大量 sext 的来源, 别写成 u8
 *     utf8_2_unicode(u8 *utf8, u8 utf8_len, u8 *unicode, u8 unic_len),
 *       局部 int t_len / int r_len / u16 unic / int utf8_offset
 *
 * 【段属性】原库代码在 .utf8_2_unicode.text(见 ref IR 的 section 属性)。
 *   本模块无全局变量、无字符串常量, 故不需要 data/bss/const 段。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".utf8_2_unicode.text")
#endif

#include "jl_typedef.h"

/*
 * @brief 由 UTF-8 首字节判断该字符占几个字节
 * @param pInput UTF-8 序列首字节
 * @return 1~6 = 字节数; 0 = 非法首字节(0x80~0xBF 是续字节, 不能做首字节)
 */
int get_utf8_size(const unsigned char pInput)
{
    unsigned char ch = pInput;

    if (ch < 0x80) {
        return 1;
    } else if (ch < 0xC0) {
        return 0;
    } else if (ch < 0xE0) {
        return 2;
    } else if (ch < 0xF0) {
        return 3;
    } else if (ch < 0xF8) {
        return 4;
    } else if (ch < 0xFC) {
        return 5;
    } else {
        return 6;
    }
}

/*
 * @brief 解一个 UTF-8 字符成 UTF-16(小端写入 unicode 指向的字节)
 * @param in     UTF-8 序列
 * @param unicode 输出, 按【字节】写入(5/6 字节形式会写满 4 个字节)
 * @return 消耗掉的 UTF-8 字节数; 0 = 序列非法
 *
 * @note 续字节的合法性判断写成 `((bN & 0xC0) != 0x80)` 的 || 串联,
 *       与原厂 IR 的 lor.lhs.false 链逐个对应。
 * @note case 5 / case 6 的算式与 case 4 的递进规律【不一致】, 疑为原库笔误,
 *       但这里保持 1:1 等价, 详见文末 TODO。
 */
int utf8_2_unicode_one(const unsigned char *in, unsigned short *unicode)
{
    char b1, b2, b3, b4, b5, b6;
    int utfbytes = get_utf8_size(*in);
    unsigned char *out = (unsigned char *)unicode;

    *unicode = 0;

    switch (utfbytes) {
    case 1:
        *out = *in;
        return utfbytes;

    case 2:
        b1 = *in;
        b2 = *(in + 1);
        if ((b2 & 0xC0) != 0x80) {
            return 0;
        }
        *out = (b1 << 6) + (b2 & 0x3F);
        *(out + 1) = (b1 >> 2) & 0x07;
        return utfbytes;

    case 3:
        b1 = *in;
        b2 = *(in + 1);
        b3 = *(in + 2);
        if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80)) {
            return 0;
        }
        *out = (b2 << 6) + (b3 & 0x3F);
        *(out + 1) = (b1 << 4) + ((b2 >> 2) & 0x0F);
        return utfbytes;

    case 4:
        b1 = *in;
        b2 = *(in + 1);
        b3 = *(in + 2);
        b4 = *(in + 3);
        if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80) || ((b4 & 0xC0) != 0x80)) {
            return 0;
        }
        *out = (b3 << 6) + (b4 & 0x3F);
        *(out + 1) = (b2 << 4) + ((b3 >> 2) & 0x0F);
        *(unsigned char *)(unicode + 1) = ((b1 << 2) & 0x1C) + ((b2 >> 4) & 0x03);
        return utfbytes;

    case 5:
        b1 = *in;
        b2 = *(in + 1);
        b3 = *(in + 2);
        b4 = *(in + 3);
        b5 = *(in + 4);
        if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80) || ((b4 & 0xC0) != 0x80)
            || ((b5 & 0xC0) != 0x80)) {
            return 0;
        }
        *out = (b4 << 6) + (b5 & 0x3F);
        *(out + 1) = (b3 << 4) + ((b4 >> 2) & 0x0F);
        *(unsigned char *)(unicode + 1) = (b2 << 2) + ((b3 >> 4) & 0x03);
        *(out + 3) = (b1 << 6);
        return utfbytes;

    case 6:
        b1 = *in;
        b2 = *(in + 1);
        b3 = *(in + 2);
        b4 = *(in + 3);
        b5 = *(in + 4);
        b6 = *(in + 5);
        if (((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80) || ((b4 & 0xC0) != 0x80)
            || ((b5 & 0xC0) != 0x80) || ((b6 & 0xC0) != 0x80)) {
            return 0;
        }
        *out = (b5 << 6) + (b6 & 0x3F);
        *(out + 1) = (b5 << 4) + ((b6 >> 2) & 0x0F);
        *(unsigned char *)(unicode + 1) = (b3 << 2) + ((b4 >> 4) & 0x03);
        *(out + 3) = ((b1 << 6) & 0x40) + (b2 & 0x3F);
        return utfbytes;
    }

    return 0;
}

/*
 * @brief 把一段 UTF-8 转成 UTF-16LE
 * @param utf8      输入
 * @param utf8_len  输入字节数
 * @param unicode   输出缓冲(按字节写, 每个字符 2 字节, 小端)
 * @param unic_len  输出缓冲字节数
 * @return 实际写出的字节数(字符数 * 2)
 *
 * @note 越界判断【已加固】为 (r_len * 2 + 2) > unic_len, 把本轮要写的 2 字节
 *       算进去。原库是 r_len * 2 > unic_len, 会多写 2 字节 —— 见文末清单。
 */
int utf8_2_unicode(u8 *utf8, u8 utf8_len, u8 *unicode, u8 unic_len)
{
    int t_len;
    int r_len = 0;
    u16 unic = 0;
    int utf8_offset = 0;

    while (utf8_len) {
        t_len = utf8_2_unicode_one(utf8 + utf8_offset, &unic);
        /*
         * 加固: 原库写的是 (r_len * 2) > unic_len —— 在【写入之前】拿"已写
         * 字节数"去比, 而每轮要写 2 字节。当 unic_len 为偶数、缓冲刚好写满时
         * (r_len*2 == unic_len), 判断不成立, 于是又写了 2 字节, 【溢出 2 字节】。
         * 改成把本轮这 2 字节算进去再比。
         * 调用方(lyrics.c)传的 outlen 目前都留了余量, 所以一直没触发。
         */
        if ((t_len == 0) || ((r_len * 2 + 2) > unic_len)) {
            break;
        }
        utf8_offset += t_len;
        utf8_len -= t_len;
        unicode[1] = unic >> 8;
        unicode[0] = unic;
        unicode += 2;
        r_len++;
    }

    return r_len * 2;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [保留] 1 —— case 6 第二个输出字节用了 (b5,b6) 而非 (b4,b5)。
 *   [保留] 2 —— case 5 第三个输出字节少了 0x1C 掩码。
 *                以上两条【只影响 5/6 字节的 UTF-8 形式】。RFC 3629 已把 UTF-8
 *                限死在 4 字节以内, 5/6 字节形式不可能出现在合法输入里; 而这
 *                两处"正确写法"该是什么也只能靠推测(原库那套位移本身就是自创的,
 *                不对应任何标准解码表)。改了既无从验证、也不会被执行到。
 *   [已修] 3 —— 越界判断 r_len*2 > unic_len 是在【写入之前】拿已写字节数比,
 *                而每轮要写 2 字节 —— 缓冲刚好写满时会再写 2 字节, 【溢出】。
 *                -> 改成 (r_len*2 + 2) > unic_len, 把本轮要写的算进去。
 *
 * 1) utf8_2_unicode_one 的 case 6 里, 第二个输出字节用的是 (b5, b6) ——
 *    与 case 5 用 (b3, b4)、case 4 用 (b2, b3) 的递进规律不符, 正确应为
 *    (b4, b5)。参考 IR 里 %shl172 = shl(sext in[4]) 、%shr174 = lshr(sext in[5]),
 *    确认原库就是这么写的, 不是还原笔误。b4 在该分支只用于第三个字节。
 *
 * 2) case 5 的第三个输出字节 `(b2 << 2) + ((b3 >> 4) & 0x03)` 【少了 & 0x1C】,
 *    而 case 4 的同一位置有。少了掩码时 b2 的高位会漏进这个字节。
 *
 *    以上两条都只影响 5/6 字节的 UTF-8 形式。RFC 3629 已把 UTF-8 限死在
 *    4 字节以内, 5/6 字节形式不可能出现在合法输入里, 所以现实中不会触发。
 *
 * 3) utf8_2_unicode 的越界判断 `r_len * 2 > unic_len` 是在写入【之前】用
 *    "已写字节数"比较, 而每轮要写 2 字节。当 unic_len 为偶数、缓冲刚好写满时
 *    (r_len*2 == unic_len), 判断不成立, 于是又写了 2 字节 —— 溢出 2 字节。
 *    正确应为 `(r_len * 2 + 2) > unic_len` 或 `r_len * 2 >= unic_len`。
 *    调用方(lyrics.c)传的 outlen 目前都留了余量, 暂不触发。
 */
