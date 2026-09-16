/*
 * font_ksc.c —— KSC(韩文)字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【两个偏移量分开命名】取模那一路用 ansi_offset, 查转换表那一路用
 *   table_offset —— 两者来自 lange_info_table 的不同字段, 不要混用。
 *   (font_gbk.c 里只用到一个, 统一叫 codepage_offset。)
 *
 * 【KSC 区位换算】累计基址都验算过:
 *   高字节 0x81~0xC5: 每区 178 个字, 低字节分三段
 *       0x41~0x5A(26 个) / 0x61~0x7A(26 个) / 0x81~0xFE(126 个)
 *   高字节 0xC6      : 该区被截断 —— 低字节只有 0x41~0x52(18 个) 与
 *                      0xA1~0xFE(94 个), 所以第二段的起始序号是 18 而不是 26
 *   高字节 0xC7~0xC8: 每区 94 个字(低字节 0xA1~0xFE), 累计基址 12394
 *                      = 69 区 * 178 + 0xC6 区的 (18 + 94)
 *   高字节 0xCA~0xFD: 每区 94 个字, 累计基址 12582 = 12394 + 2 区 * 94
 *   (0xC9 这一区【没有覆盖】, 落到 offset = -1, 见文末注意事项)
 *
 * 【与 font_gbk.c 的结构差异】
 *   TextOut_KSC / TextOutW_KSC 不做 info->offset 的起始偏移(GBK 那一路会做);
 *   两个 TextOut 都是 '\n' 换行、'\r' 忽略(与 font_big5.c 一致)。
 *
 * 【段属性】代码放在 .font_ksc.text。唯一的字符串常量 "r" 不单独设段,
 *   所以不开 const_seg。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_ksc.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"

extern u8 InitFont_ASCII(struct font_info *info);
extern u8 GetASCIICharacterData(struct font_info *info, u16 asc);

bool InitFont_KSC(struct font_info *info);
u8 GetKSCCharacterData(struct font_info *info, u16 textCode);
u16 ConvertUTF16toKSC(struct font_info *info, u16 utf);
u16 TextOut_KSC(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_KSC(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);

bool InitFont_KSC(struct font_info *info)
{
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
    /* 读不到字高就关掉文件并报错: 放过这次失败的话 info->pixel.size 会保持旧值(首次调用
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
 * @brief 取一个 KSC 内码字的点阵到 info->pixel.pixelbuf
 * @return 字高; 0 = 非法内码 / 没有点阵缓冲
 * @note 区位换算见文件头注释。0xC6 区被截断、0xC9 区未覆盖, 都是字库文件的实际布局。
 */
u8 GetKSCCharacterData(struct font_info *info, u16 textCode)
{
    u8 data_high = textCode >> 8;
    u8 data_low = textCode;
    u32 offset;
    u32 addr;
    u32 ansi_offset = 6;

    if ((data_high >= 0x81) && (data_high < 0xC6)) {
        if ((data_low >= 0x41) && (data_low <= 0x5A)) {
            offset = (data_high - 0x81) * 178 + (data_low - 0x41);
        } else if ((data_low >= 0x61) && (data_low <= 0x7A)) {
            offset = (data_high - 0x81) * 178 + (data_low - 0x61) + 26;
        } else if ((data_low < 0x81) || (data_low == 0xFF)) {
            offset = -1;
        } else {
            offset = (data_high - 0x81) * 178 + (data_low - 0x81) + 52;
        }
    } else if (data_high == 0xC6) {
        if ((data_low >= 0x41) && (data_low <= 0x52)) {
            offset = (data_high - 0x81) * 178 + (data_low - 0x41);
        } else if ((data_low < 0xA1) || (data_low == 0xFF)) {
            offset = -1;
        } else {
            offset = (data_high - 0x81) * 178 + (data_low - 0xA1) + 18;
        }
    } else if ((data_high >= 0xC7) && (data_high < 0xC9)) {
        if ((data_low < 0xA1) || (data_low == 0xFF)) {
            offset = -1;
        } else {
            offset = (data_high - 0xC7) * 94 + (data_low - 0xA1) + 12394;
        }
    } else if ((data_high >= 0xCA) && (data_high < 0xFE)) {
        if ((data_low < 0xA1) || (data_low == 0xFF)) {
            offset = -1;
        } else {
            offset = (data_high - 0xCA) * 94 + (data_low - 0xA1) + 12582;
        }
    } else {
        offset = -1;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            ansi_offset = lange_info_table[info->codepage].ansi_offset + 6;
        }
    }

    if (offset == -1) {
        return 0;
    }

    if (info->pixel.pixelbuf == NULL) {
        return 0;
    }

    addr = ansi_offset + info->pixel.nbytes * offset;
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
 * @brief UTF-16 码位 -> KSC 内码(查 .TAB 文件)
 * @return KSC 内码; 0 = 该码位不在表的覆盖区间内
 * @note 第三段覆盖到 0xD7FF(含韩文音节区), 比 GBK/BIG5 那两路都宽。
 */
u16 ConvertUTF16toKSC(struct font_info *info, u16 utf)
{
    u8 gbk[2] = {0};
    u32 offset = utf * 2;
    u32 addr;
    u32 table_offset = 0;

    if (utf < 0x0480) {
        addr = offset;
    } else if ((utf >= 0x2000) && (utf <= 0x33FF)) {
        addr = offset - 14080;
    } else if ((utf >= 0x4E00) && (utf <= 0xD7FF)) {
        addr = offset - 27392;
    } else if (utf > 0xF8FF) {
        addr = offset - 44288;
    } else {
        return 0;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            table_offset = lange_info_table[info->codepage].table_offset;
        }
    }

    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, table_offset + addr);
    /* 表项读失败必须返回 0(查不到): 否则 gbk[] 里是上一次的内容(或未初始化的
     * 栈内容), 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

u16 TextOut_KSC(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
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
            width = GetKSCCharacterData(info, text);
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

u16 TextOutW_KSC(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
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

    info->string_width = 0;
    info->string_height = 0;
    info->bigendian &= 1;

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
            text = ConvertUTF16toKSC(info, text);
            width = GetKSCCharacterData(info, text);
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
 * 实现注意事项与已知限制
 *
 *  1) 【高字节 0xC9 这一区没有覆盖】区间是 0x81~0xC5、0xC6、0xC7~0xC8、
 *     0xCA~0xFD 四段, 0xC9 落在缝里, 该区的内码一律返回 0、显示成 '-'。
 *     要补得先知道该区在字库文件里的确切起止内码与偏移算法 —— 照相邻区
 *     "推算"一个偏移填进去, 只会把"显示横杠"变成"显示乱码", 后者更难发现。
 *     等字库打包工具那边给出格式说明再补。
 *
 *  2) 【0xC6 区是截断的】低字节只有 0x41~0x52(18 个)与 0xA1~0xFE(94 个),
 *     所以第二段的起始序号是 18 而不是其它区的 26。换算见文件头。
 *
 *  3) 【offset 用 -1 作哨兵】赋给 u32 再用 `== -1` 判定, 两边都是 0xFFFFFFFF,
 *     行为正确。区位不合法时就是这条路径。
 *
 *  4) 【读盘返回值都要判】字高、转换表项、点阵三处都判了实际长度 —— 读失败
 *     时放过去会分别导致偏移算错、栈内容当内码、显示上一个字。
 */
