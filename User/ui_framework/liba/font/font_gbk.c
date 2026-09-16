/*
 * font_gbk.c —— GBK / GB2312 字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【转换表的分段】ConvertUTF16toGB2312 里那 20 段区间对应 GB2312 在 Unicode
 *   码位空间里零散分布的部分, 每段 addr = utf * 2 + K(K 见各分支)。相邻段
 *   在表里首尾相接, 已验算过: 上一段末项 +2 正好是下一段首项。
 *   本模块无 ASSERT。
 *
 * 【段属性】代码放在 .font_gbk.text。唯一的字符串常量 "r" 不单独设段,
 *   所以不开 const_seg。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_gbk.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"

extern u8 InitFont_ASCII(struct font_info *info);
extern u8 GetASCIICharacterData(struct font_info *info, u16 asc);

bool InitFont_GBK(struct font_info *info);
u8 GetGBKCharacterData(struct font_info *info, u16 textCode);
u16 ConvertUTF16toGBK(struct font_info *info, u16 utf);
u8 GetGB2312CharacterData(struct font_info *info, u16 textCode);
u16 ConvertUTF16toGB2312(struct font_info *info, u16 utf);
u16 TextOut_GBK(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_GBK(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);

/*
 * @brief 打开 GBK 字模文件与 UNICODE->内码 转换表
 * @return 1 = 成功; 0 = 失败(失败原因记在 info->sta 的 FT_ERROR_* 位里)
 * @note codepage 非 0 且外部注册过 lange_info_table 时, 字模文件里存在多个
 *       代码页分区, 需要先跳到该代码页的 ansi_offset 处再读字高。
 */
bool InitFont_GBK(struct font_info *info)
{
    int i;
    u32 offset = 0;

    if (InitFont_ASCII(info) == 0) {
        info->sta |= FT_ERROR_NOASCPIXFILE;
        return 0;
    }

    info->pixel.file.fd = font_sd_fopen(info->pixel.file.name, "r");
    if (info->pixel.file.fd == NULL) {
        info->sta |= FT_ERROR_NOPIXFILE;
        return 0;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            offset = lange_info_table[info->codepage].ansi_offset;
        }
    }

    font_sd_fseek(info->pixel.file.fd, SD_SEEK_SET, offset);
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
 * @brief 取一个 GBK 内码字的点阵到 info->pixel.pixelbuf
 * @return 字高(即 info->pixel.size); 0 = 非法内码 / 没有点阵缓冲
 * @note GBK 区位换算: 高字节 0x81~0xFE、低字节 0x40~0xFE, 每区 191 个字。
 *       字模文件头 6 字节是文件头, 所以 codepage_offset 从 6 起算。
 */
u8 GetGBKCharacterData(struct font_info *info, u16 textCode)
{
    u8 data_high = textCode >> 8;
    u8 data_low = textCode;
    u32 offset;
    u32 addr;
    u32 codepage_offset = 6;

    if ((data_high >= 0x81) && (data_high != 0xFF) && (data_low >= 0x40) && (data_low != 0xFF)) {
        offset = (data_high - 0x81) * 191 + (data_low - 0x40);
    } else {
        offset = -1;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            codepage_offset = lange_info_table[info->codepage].ansi_offset + 6;
        }
    }

    if (offset == -1) {
        return 0;
    }

    if (info->pixel.pixelbuf == NULL) {
        return 0;
    }

    addr = codepage_offset + info->pixel.nbytes * offset;
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
 * @brief UTF-16 码位 -> GBK 内码(查 .TAB 文件)
 * @return GBK 内码; 0 = 该码位不在表的覆盖区间内
 * @note 表按码位分 4 段紧密排列, 每项 2 字节。四段的 addr 常量已验算首尾相接。
 */
u16 ConvertUTF16toGBK(struct font_info *info, u16 utf)
{
    u8 gbk[2] = {0};
    u32 offset = utf * 2;
    u32 addr;
    u32 codepage_offset = 0;

    if (utf < 0x0480) {
        addr = offset;
    } else if ((utf >= 0x2000) && (utf <= 0x33FF)) {
        addr = offset - 14080;
    } else if ((utf >= 0x4E00) && (utf <= 0x9FFF)) {
        addr = offset - 27392;
    } else if (utf > 0xF8FF) {
        addr = offset - 68608;
    } else {
        return 0;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            codepage_offset = lange_info_table[info->codepage].table_offset;
        }
    }

    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, codepage_offset + addr);
    /* 表项读失败必须返回 0(查不到): 否则 gbk[] 里是上一次或未初始化的内容,
     * 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

/*
 * @brief 取一个 GB2312 内码字的点阵
 * @return 字高; 0 = 非法内码 / 没有点阵缓冲
 * @note GB2312 区位: 高字节 0xA1~0xF7、低字节 0xA1~0xFE, 每区 94 个字。
 */
u8 GetGB2312CharacterData(struct font_info *info, u16 textCode)
{
    u8 data_high = textCode >> 8;
    u8 data_low = textCode;
    u32 offset;
    u32 addr;
    u32 codepage_offset = 6;

    if ((data_high >= 0xA1) && (data_high < 0xF8) && (data_low >= 0xA1) && (data_low != 0xFF)) {
        offset = (data_high - 0xA1) * 94 + (data_low - 0xA1);
    } else {
        offset = -1;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            codepage_offset = lange_info_table[info->codepage].ansi_offset + 6;
        }
    }

    if (offset == -1) {
        return 0;
    }

    if (info->pixel.pixelbuf == NULL) {
        return 0;
    }

    addr = codepage_offset + info->pixel.nbytes * offset;
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
 * @brief UTF-16 码位 -> GB2312 内码(查 .TAB 文件)
 * @return GB2312 内码; 0 = 该码位不在表的覆盖区间内
 * @note GB2312 只覆盖码位空间里零散的 20 段, 所以这里是一长串区间判断。
 *       每段 addr = utf * 2 + K, K 见各分支; 相邻段在表里首尾相接。
 */
u16 ConvertUTF16toGB2312(struct font_info *info, u16 utf)
{
    u8 gbk[2] = {0};
    u32 offset = utf * 2;
    u32 addr;
    u32 codepage_offset = 0;

    if ((utf >= 0x00A4) && (utf <= 0x02C9)) {
        addr = offset - 328;
    } else if ((utf >= 0x0391) && (utf <= 0x0451)) {
        addr = offset - 726;
    } else if ((utf >= 0x2014) && (utf <= 0x203B)) {
        addr = offset - 14938;
    } else if ((utf >= 0x2103) && (utf <= 0x2312)) {
        addr = offset - 15336;
    } else if ((utf >= 0x2460) && (utf <= 0x2642)) {
        addr = offset - 16002;
    } else if ((utf >= 0x3000) && (utf <= 0x3129)) {
        addr = offset - 20988;
    } else if ((utf >= 0x3220) && (utf <= 0x3229)) {
        addr = offset - 21480;
    } else if ((utf >= 0x4E00) && (utf <= 0x7DAE)) {
        addr = offset - 35732;
    } else if ((utf >= 0x7E3B) && (utf <= 0x8C98)) {
        addr = offset - 36012;
    } else if ((utf >= 0x8D1D) && (utf <= 0x8ECE)) {
        addr = offset - 36276;
    } else if ((utf >= 0x8F66) && (utf <= 0x91DC)) {
        addr = offset - 36578;
    } else if ((utf >= 0x9274) && (utf <= 0x99A8)) {
        addr = offset - 36880;
    } else if ((utf >= 0x9A6C) && (utf <= 0x9B54)) {
        addr = offset - 37270;
    } else if ((utf >= 0x9C7C) && (utf <= 0x9CE2)) {
        addr = offset - 37860;
    } else if ((utf >= 0x9E1F) && (utf <= 0x9FA0)) {
        addr = offset - 38492;
    } else if ((utf >= 0xE000) && (utf <= 0xE233)) {
        addr = offset - 71450;
    } else if ((utf >= 0xE766) && (utf <= 0xE814)) {
        addr = offset - 74110;
    } else if ((utf >= 0xFE31) && (utf <= 0xFE44)) {
        addr = offset - 85430;
    } else if ((utf >= 0xFF01) && (utf <= 0xFF5E)) {
        addr = offset - 85806;
    } else if (utf > 0xFFDF) {
        addr = offset - 86064;
    } else {
        return 0;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            codepage_offset = lange_info_table[info->codepage].table_offset;
        }
    }

    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, codepage_offset + addr);
    /* 表项读失败必须返回 0(查不到): 否则 gbk[] 里是上一次或未初始化的内容,
     * 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

/*
 * @brief 内码(GBK/GB2312)字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+1)
 *
 * @note pixel_size 取"汉字字高与 ASCII 字高里较大的那个", 用作行高。
 * @note info->flags 的 FONT_SHOW_PIXEL 决定是否真的调 putchar 出像素;
 *       FONT_GET_WIDTH 模式下 font_text_width 会把它清掉, 只累加 string_width。
 * @note putchar 的 y 坐标里加了 (pixel_size - height), 让矮字(ASCII)与
 *       高字(汉字)在同一行【底部对齐】。
 */
u16 TextOut_GBK(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
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

    /* 取"汉字字高与 ASCII 字高里较大的那个"作行高: 先选结构体指针, 再取 size。 */
    pixel_size = ((info->pixel.size > info->ascpixel.size) ? &info->pixel
                  : &info->ascpixel)->size;

    info->string_width = 0;
    info->string_height = 0;

    str += info->offset * 2;
    len -= info->offset * 2;

    for (i = 0; (i < len) && str[i]; i += step) {
        if ((str[i] > 0x7F) && ((i + 1) < len)) {
            text = (str[i] << 8) | str[i + 1];
            if (info->isgb2312) {
                width = GetGB2312CharacterData(info, text);
            } else {
                width = GetGBKCharacterData(info, text);
            }
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
 * @brief UTF-16 字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+2)
 *
 * @note 与 TextOut_GBK 的两点差异:
 *       1. 换行统一按 '\n' 换行、'\r' 忽略, 与 TextOut_GBK 一致(见循环里的说明)。
 *       2. 控制字符(< 0x20)在这里替换成 '*' 显示; TextOut_GBK 里会原样送去
 *          GetASCIICharacterData。
 * @note 循环里【每个字符都重新读一次 info->bigendian】—— 回调有可能改它,
 *       所以不要把它提到循环外缓存。
 */
u16 TextOutW_GBK(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width;
    u16 height;
    u16 xpos = 0;
    u16 ypos = 0;
    u16 i;
    u8 pixel_size;
    u8 ascii;

    /* 取"汉字字高与 ASCII 字高里较大的那个"作行高: 先选结构体指针, 再取 size。 */
    pixel_size = ((info->pixel.size > info->ascpixel.size) ? &info->pixel
                  : &info->ascpixel)->size;

    info->string_width = 0;
    info->string_height = 0;
    info->bigendian &= 1;

    str += info->offset * 2;
    len -= info->offset * 2;

    for (i = 0; (i + 1) < len; i += 2) {
        text = (str[i + 1 - info->bigendian] << 8) | str[i + info->bigendian];
        if (text == 0) {
            break;
        }

        if ((str[i + info->bigendian] < 0x80) && (str[i + 1 - info->bigendian] == 0)) {
            /*
             * 换行规则与 TextOut_GBK 保持一致: '\n' 换行、'\r' 忽略。
             * 【不要】改成"两个都换行" —— 那会让 "\r\n" 换两行。按现在这样:
             *   "\n"   -> 换一行
             *   "\r\n" -> '\r' 忽略、'\n' 换行, 仍是一行
             *   "\r"   -> 不换行(纯 \r 换行是老 Mac 风格, 资源里几乎不会出现)
             */
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
            if (info->isgb2312) {
                text = ConvertUTF16toGB2312(info, text);
            } else {
                text = ConvertUTF16toGBK(info, text);
            }
            if (info->isgb2312) {
                width = GetGB2312CharacterData(info, text);
            } else {
                width = GetGBKCharacterData(info, text);
            }
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
 *  1) 【CR/LF 两条输出路径要一致】TextOut_GBK 与 TextOutW_GBK 都是 '\n' 换行、
 *     '\r' 忽略。两边不一致的话, 同一段文本走内码路径和走 UTF-16 路径会得到
 *     不同的排版; 而"两个都换行"又会让 "\r\n" 换两行 —— 现在这样是折中后的
 *     唯一自洽选择。
 *
 *  2) 【offset 用 -1 作哨兵】两个 Get*CharacterData 把 -1 赋给 u32 再用
 *     `== -1` 判定, 两边都是 0xFFFFFFFF, 行为正确。区位不合法时走这条路径。
 *
 *  3) 【读盘返回值都要判】字高、两处转换表项、两处点阵都判了实际长度 ——
 *     读失败时放过去会分别导致偏移算错、栈内容当内码、显示上一个字。
 *
 *  4) 【区位换算】
 *       GBK    : 高字节 0x81~0xFE、低字节 0x40~0xFE, 每区 191 个字;
 *       GB2312 : 高字节 0xA1~0xF7、低字节 0xA1~0xFE, 每区 94 个字。
 *     两者的字模数据都从 codepage_offset(默认 6, 多代码页时再加 ansi_offset)
 *     起算。
 *
 *  5) 【底部对齐】putchar 的 y 坐标里加了 (pixel_size - height), 让矮字(ASCII)
 *     与高字(汉字)在同一行底部对齐。
 */
