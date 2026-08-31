/*
 * font_big5.c —— BIG5(繁体中文)字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_big5.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_big5.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_big5.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     InitFont_BIG5@11   GetBIG5CharacterData@38   ConvertUTF16toBIG5@62
 *     TextOut_BIG5@85    TextOutW_BIG5@165
 *   局部变量名与类型取自 DWARF。
 *
 * 【与 font_gbk.c 的结构差异】(都在 IR 里逐条核对过, 不是省略)
 *   1. BIG5 这一路【完全没有 codepage / lange_info_table 的处理】——
 *      文件头固定 6 字节, 转换表从 0 开始。所以没有 codepage_offset 这个局部。
 *   2. TextOut_BIG5 / TextOutW_BIG5 【不做 info->offset 的起始偏移】,
 *      直接用传入的 str / len(GBK 那一路会先 str += offset*2, len -= offset*2)。
 *   3. ConvertUTF16toBIG5 里的 gbk[2] 【没有 = {0} 初值】(参考 IR 是
 *      `alloca [2 x i8]` 且没有清零 store; GBK 那边是 `alloca i16` + store 0)。
 *   4. BIG5 的低字节分两段(0x40~0x7E 与 0xA1~0xFE), 第二段要再加 63。
 *
 * 【段属性】原库代码在 .font_big5.text(见 ref IR 的 section 属性)。唯一的字符串
 *   常量 "r" 在原厂 IR 里没有 section 属性, 所以不能开 const_seg。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_big5.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"

extern u8 InitFont_ASCII(struct font_info *info);
extern u8 GetASCIICharacterData(struct font_info *info, u16 asc);

bool InitFont_BIG5(struct font_info *info);
u8 GetBIG5CharacterData(struct font_info *info, u16 textCode);
u16 ConvertUTF16toBIG5(struct font_info *info, u16 utf);
u16 TextOut_BIG5(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_BIG5(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);

/*
 * @brief 打开 BIG5 字模文件与 UNICODE->BIG5 转换表
 * @return 1 = 成功; 0 = 失败(失败原因记在 info->sta 的 FT_ERROR_* 位里)
 */
bool InitFont_BIG5(struct font_info *info)
{
    int i;

    if (InitFont_ASCII(info) == 0) {
        info->sta |= FT_ERROR_NOASCPIXFILE;
        return 0;
    }

    info->pixel.file.fd = font_sd_fopen(info->pixel.file.name, "r");
    if (info->pixel.file.fd == NULL) {
        info->sta |= FT_ERROR_NOPIXFILE;
        return 0;
    }

    font_sd_fseek(info->pixel.file.fd, SD_SEEK_SET, 0);
    /* 加固: 原库丢弃返回值。读不到字高时 info->pixel.size 保持旧值(首次调用
     * 就是未初始化内存), 而 InitFont_* 仍返回 1 表示成功 —— 此后 nbytes 与
     * 所有取模偏移全建立在垃圾值上。读失败就关掉文件并如实报错。 */
    if (font_sd_fread(info->pixel.file.fd, &info->pixel.size, 1) != 1) {
        font_sd_fclose(info->pixel.file.fd);
        info->pixel.file.fd = NULL;
        return 0;
    }
    info->pixel.nbytes = ((info->pixel.size + 7) / 8) * info->pixel.size;

    info->tabfile.fd = font_sd_fopen(info->tabfile.name, "r");
    if (info->tabfile.fd == NULL) {
        info->sta |= FT_ERROR_NOTABFILE;
        return 0;
    }

    return 1;
}

/*
 * @brief 取一个 BIG5 内码字的点阵到 info->pixel.pixelbuf
 * @return 字高(即 info->pixel.size); 0 = 非法内码 / 没有点阵缓冲
 * @note BIG5 区位: 高字节 0xA1~0xF9; 低字节分两段 —— 0x40~0x7E(63 个)
 *       与 0xA1~0xFE(94 个), 每区共 157 个字, 第二段的序号要再加 63。
 */
u8 GetBIG5CharacterData(struct font_info *info, u16 textCode)
{
    u8 data_high = textCode >> 8;
    u8 data_low = textCode;
    u32 offset = -1;
    u32 addr;

    if ((data_high >= 0xA1) && (data_high < 0xFA)) {
        if ((data_low >= 0x40) && (data_low <= 0x7E)) {
            offset = (data_high - 0xA1) * 157 + (data_low - 0x40);
        } else if ((data_low >= 0xA1) && (data_low != 0xFF)) {
            offset = (data_high - 0xA1) * 157 + (data_low - 0xA1) + 63;
        }
    }

    if (offset == -1) {
        return 0;
    }

    if (info->pixel.pixelbuf == NULL) {
        return 0;
    }

    addr = info->pixel.nbytes * offset + 6;
    font_sd_fseek(info->pixel.file.fd, SD_SEEK_SET, addr);
    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回字高 —— 表现为"显示上一个字", 排查起来很费劲。 */
    if (font_sd_fread(info->pixel.file.fd, info->pixel.pixelbuf, info->pixel.nbytes)
        != (int)info->pixel.nbytes) {
        return 0;
    }

    return info->pixel.size;
}

/*
 * @brief UTF-16 码位 -> BIG5 内码(查 .TAB 文件)
 * @return BIG5 内码; 0 = 该码位不在表的覆盖区间内
 * @note 表按码位分 4 段紧密排列, 每项 2 字节。
 */
u16 ConvertUTF16toBIG5(struct font_info *info, u16 utf)
{
    /* 加固: 原库这里【没有初值】, 而下面 font_sd_fread 的返回值又不检查 —— 读失败
     * 时返回的是未初始化的栈内容, 被当成合法内码用。GBK 那一路的同名局部是
     * = {0} 的, 至少失败时返回 0(表示查不到)。两处现在都补了返回值检查, 初值
     * 也补上, 双保险。 */
    u8 gbk[2] = {0};
    u32 offset = utf * 2;
    u32 addr;

    if (utf < 0x0400) {
        addr = offset;
    } else if ((utf >= 0x2000) && (utf <= 0x33FF)) {
        addr = offset - 14336;
    } else if ((utf >= 0x4E00) && (utf <= 0x9FFF)) {
        addr = offset - 27648;
    } else if (utf > 0xF67F) {
        addr = offset - 71936;
    } else {
        return 0;
    }

    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, addr);
    /* 加固: 原库丢弃返回值。表项读失败时 gbk[] 是上一次的内容(或未初始化的
     * 栈内容), 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

/*
 * @brief BIG5 内码字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+1)
 * @note pixel_size 的取法要用【选指针再取 size】的写法, 理由见
 *       accept/font_gbk.txt 里的说明(写成 if/else 会被 GVN 合并成"选值")。
 */
u16 TextOut_BIG5(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width;
    u16 height;
    u16 xpos = 0;
    u16 ypos = 0;
    u16 i;
    u8 step;
    u8 pixel_size;
    u8 ascii;

    pixel_size = ((info->pixel.size > info->ascpixel.size) ? &info->pixel
                  : &info->ascpixel)->size;

    info->string_width = 0;
    info->string_height = 0;

    for (i = 0; (i < len) && str[i]; i += step) {
        if ((str[i] > 0x7F) && ((i + 1) < len)) {
            text = (str[i] << 8) | str[i + 1];
            width = GetBIG5CharacterData(info, text);
            step = 2;
            ascii = 0;
            if (width == 0) {
                width = GetASCIICharacterData(info, '-');
                ascii = 1;
            }
        } else if (str[i] == '\r') {
            step = 1;
            continue;
        } else if (str[i] == '\n') {
            ypos += info->ratio * pixel_size;
            if (ypos + info->ratio * pixel_size > info->text_height) {
                i++;
                break;
            }
            xpos = 0;
            step = 1;
            continue;
        } else {
            text = str[i];
            width = GetASCIICharacterData(info, text);
            step = 1;
            ascii = 1;
        }

        xpos += info->ratio * width;
        info->string_width += info->ratio * width;

        if (xpos > info->text_width) {
            if (!(info->flags & FONT_SHOW_MULTI_LINE)) {
                break;
            }
            ypos += info->ratio * pixel_size;
            if (ypos + info->ratio * pixel_size > info->text_height) {
                break;
            }
            xpos = info->ratio * width;
        }

        if (ascii) {
            height = info->ascpixel.size;
        } else {
            height = info->pixel.size;
        }

        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info,
                              ascii ? info->ascpixel.pixelbuf : info->pixel.pixelbuf,
                              width, height,
                              xpos + x - info->ratio * width,
                              ypos + y + ((pixel_size > height) ? (u8)(pixel_size - height) : 0));
            }
        }

        info->string_height = height + ypos;
    }

    return i;
}

/*
 * @brief UTF-16 字符串输出(转 BIG5 后取模)
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+2)
 * @note 与 TextOutW_GBK 不同, 这里做换行的是 '\n'、忽略的是 '\r' ——
 *       与本文件的 TextOut_BIG5 一致。font_gbk.c 的 TextOutW_GBK 恰好相反,
 *       那是原库自己的不一致(见 font_gbk.c 文末 TODO)。
 * @note 循环里【每个字符都重新读一次 info->bigendian】, 参考 IR 的 load 就在
 *       循环体内, 还原时不要顺手提到循环外。
 */
u16 TextOutW_BIG5(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width;
    u16 height;
    u16 xpos = 0;
    u16 ypos = 0;
    u16 i;
    u8 pixel_size;
    u8 ascii;

    pixel_size = ((info->pixel.size > info->ascpixel.size) ? &info->pixel
                  : &info->ascpixel)->size;

    info->bigendian &= 1;
    info->string_width = 0;
    info->string_height = 0;

    for (i = 0; (i + 1) < len; i += 2) {
        text = (str[i + 1 - info->bigendian] << 8) | str[i + info->bigendian];
        if (text == 0) {
            break;
        }

        if ((str[i + info->bigendian] < 0x80) && (str[i + 1 - info->bigendian] == 0)) {
            if (str[i + info->bigendian] == '\r') {
                continue;
            } else if (str[i + info->bigendian] == '\n') {
                ypos += info->ratio * pixel_size;
                if (ypos + info->ratio * pixel_size > info->text_height) {
                    i += 2;
                    break;
                }
                xpos = 0;
                continue;
            }
            text = (str[i + info->bigendian] > 0x1F) ? str[i + info->bigendian] : '*';
            width = GetASCIICharacterData(info, text);
            ascii = 1;
        } else {
            text = ConvertUTF16toBIG5(info, text);
            width = GetBIG5CharacterData(info, text);
            ascii = 0;
            if (width == 0) {
                width = GetASCIICharacterData(info, '-');
                ascii = 1;
            }
        }

        xpos += info->ratio * width;
        info->string_width += info->ratio * width;

        if (xpos > info->text_width) {
            if (!(info->flags & FONT_SHOW_MULTI_LINE)) {
                break;
            }
            ypos += info->ratio * pixel_size;
            if (ypos + info->ratio * pixel_size > info->text_height) {
                break;
            }
            xpos = info->ratio * width;
        }

        if (ascii) {
            height = info->ascpixel.size;
        } else {
            height = info->pixel.size;
        }

        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info,
                              ascii ? info->ascpixel.pixelbuf : info->pixel.pixelbuf,
                              width, height,
                              xpos + x - info->ratio * width,
                              ypos + y + ((pixel_size > height) ? (u8)(pixel_size - height) : 0));
            }
        }

        info->string_height = height + ypos;
    }

    return i;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— gbk[2] 没有初值 + 不检查 fread -> 两处都补上了。
 *   [保留] 2 —— offset = -1 的哨兵写法。这【不是缺陷】: -1 转 u32 与 == -1 比较
 *                在 C 里都是 0xFFFFFFFF, 行为完全正确, 只是可读性一般。
 *                改它没有实际收益, 只会增加与原库逐条对照时的噪声。
 *   [已修] 3 —— 不检查 fread -> 已补(读失败返回 0, 不再"显示上一个字")。
 *   [保留] 4 —— InitFont_BIG5 里未使用的局部变量 i。同 rle.c 的死变量 i: 删它对代码
 *                生成毫无影响, 只会增加对照噪声。
 *
 * 1) ConvertUTF16toBIG5 里的 gbk[2] 【没有初值】, 而 font_sd_fread 的返回值
 *    又不检查。读失败时返回的是【未初始化的栈内容】当成内码用, 后面会拿它去
 *    取模。GBK 那一路的同名局部有 = {0}, 至少失败时返回 0(表示查不到)。
 *
 * 2) GetBIG5CharacterData 里 `offset = -1` 是把 -1 赋给 u32 再用 `== -1` 判定,
 *    与 font_gbk.c 同样的写法。
 *
 * 3) 同样不检查 font_sd_fread 的返回值, 读失败时 pixelbuf 里是上一个字的点阵,
 *    却照常返回字高 —— 表现为"显示上一个字"。
 *
 * 4) InitFont_BIG5 里的局部变量 i 从未使用(DWARF 里确有这个变量)。
 */
