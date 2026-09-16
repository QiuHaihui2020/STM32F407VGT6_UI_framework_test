/*
 * font_big5.c —— BIG5(繁体中文)字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【与 font_gbk.c 的差异】同为双字节字库, 但这一路有四点不同:
 *   1. BIG5 【不涉及 codepage / lange_info_table】—— 字模文件头固定 6 字节,
 *      转换表从 0 开始, 所以没有 codepage_offset 这个量。
 *   2. TextOut_BIG5 / TextOutW_BIG5 【不做 info->offset 的起始偏移】, 直接用
 *      传入的 str / len(GBK 那一路会先 str += offset*2、len -= offset*2)。
 *   3. BIG5 的低字节分两段(0x40~0x7E 与 0xA1~0xFE), 第二段的序号要再加 63。
 *   4. 换行处理: 本文件的两个 TextOut 都是 '\n' 换行、'\r' 忽略, 前后一致。
 *
 * 【段属性】代码放在 .font_big5.text。唯一的字符串常量 "r" 不单独设段,
 *   所以不开 const_seg。
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
    /* 读不到字高就关掉文件并如实报错: 放过这次失败的话 info->pixel.size 会
     * 保持旧值(首次调用就是未初始化内存), 而 InitFont_* 还返回 1 表示成功 ——
     * 此后 nbytes 与所有取模偏移全建立在垃圾值上。 */
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
    /* 点阵读失败必须返回 0: 此时 pixelbuf 里还是【上一个字】的点阵, 若照常
     * 返回字高, 界面上就是"显示上一个字", 排查起来很费劲。 */
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
    /* gbk[] 要给初值: 它是栈上变量, 一旦下面的读表失败, 未初始化的内容就会
     * 被当成合法内码返回。连同下面的返回值检查一起, 是双保险。 */
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
    /* 表项读失败必须返回 0(查不到): 否则 gbk[] 里是上一次或未初始化的内容,
     * 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

/*
 * @brief BIG5 内码字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+1)
 * @note pixel_size 用"先选结构体指针、再取 size"的写法, 取两者中较大的那个
 *       字高作为行距基准。
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
 * @note 换行用 '\n'、忽略 '\r' —— 与本文件的 TextOut_BIG5 一致。
 *       (font_gbk.c 的 TextOutW_GBK 两者相反, 见那边的注意事项。)
 * @note 循环里【每个字符都重新读一次 info->bigendian】—— 回调有可能改它,
 *       所以不要把它提到循环外缓存。
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
 * 实现注意事项
 *
 *  1) 【读盘返回值都要判】三处 font_sd_fread 都判了实际长度: 字高读不到会让
 *     nbytes 与所有取模偏移建立在垃圾值上; 转换表项读不到会把栈内容当内码;
 *     点阵读不到则会把上一个字的点阵当本字显示。
 *
 *  2) 【ConvertUTF16toBIG5 的 gbk[] 有初值】栈上变量, 配合返回值检查双保险。
 *
 *  3) 【offset 用 -1 作哨兵】赋给 u32 再用 `== -1` 判定, 两边都是 0xFFFFFFFF,
 *     行为正确。区位不合法时就是这条路径。
 *
 *  4) 【BIG5 区位换算】高字节 0xA1~0xF9; 低字节分两段 —— 0x40~0x7E(63 个)与
 *     0xA1~0xFE(94 个), 每区共 157 个字, 第二段的序号要再加 63。
 *     字模数据从文件偏移 6 开始。
 */
