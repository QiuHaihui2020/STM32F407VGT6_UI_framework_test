/*
 * utf8_2_unicode.c —— UTF-8 → UTF-16LE 解码
 *
 * 【b1..b6 必须是有符号 char】下面各 case 的位运算是按 char 的符号扩展写的,
 *   写成 u8 会改变移位后的高位结果。
 *
 * 【段属性】代码放在 .utf8_2_unicode.text。本模块无 ASSERT、无全局变量、
 *   无字符串常量, 故不需要 data/bss/const 段, 也不涉及 __FILE__/__LINE__。
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
 * @note 续字节必须是 10xxxxxx, 所以用 `((bN & 0xC0) != 0x80)` 逐个判, 任意
 *       一个不合法就整串作废。
 * @note case 5 / case 6 的算式与 case 4 的递进规律【不一致】, 见文末注意事项 ——
 *       这两种形式在合法 UTF-8 里不会出现。
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
 * @note 越界判断是 (r_len * 2 + 2) > unic_len —— 必须把本轮要写的 2 字节
 *       算进去, 见下面循环里的说明。
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
         * 这里【必须把本轮要写的 2 字节算进去】再比: 写成 (r_len * 2) > unic_len
         * 等于在写入之前拿"已写字节数"去比, 当 unic_len 为偶数、缓冲刚好写满时
         * (r_len*2 == unic_len)判断不成立, 于是又写了 2 字节, 溢出。
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
 * 实现注意事项
 *
 *  1) 【只有 1~4 字节形式会真正用到】RFC 3629 已把 UTF-8 限死在 4 字节以内,
 *     5 / 6 字节形式不会出现在合法输入里。这两个 case 留着是为了让
 *     get_utf8_size 的返回值都有对应分支, 但其中的位移算式与 case 4 的递进
 *     规律并不一致:
 *       · case 6 第二个输出字节用的是 (b5, b6), 按递进规律应是 (b4, b5);
 *       · case 5 第三个输出字节少了 & 0x1C 掩码, b2 的高位会漏进来。
 *     要动这两处得先定清楚 5/6 字节形式该怎么映射到 UTF-16 —— 它本身不在
 *     标准里, 所以维持现状, 不臆改。
 *
 *  2) 【输出越界判断】utf8_2_unicode 里用 (r_len * 2 + 2) > unic_len:
 *     每轮要写 2 字节, 判断必须把它算进去。写成 r_len * 2 > unic_len 会在
 *     缓冲刚好写满时再写 2 字节、溢出。
 *
 *  3) 【b1..b6 是有符号 char】各 case 的位运算按符号扩展写的, 改成 u8 会
 *     改变结果。
 */
