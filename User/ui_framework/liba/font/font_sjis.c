/*
 * font_sjis.c —— Shift-JIS(日文)字库: 初始化、半角/全角取模、UTF-16 转内码、
 *                文本输出
 *
 * 【Shift-JIS 区位换算】累计基址都验算过:
 *   首字节 0x81~0x9F 与 0xE0~0xEF 两段, 每段每区 188 个字, 次字节分两段:
 *       0x40~0x7E(63 个) / 0x80~0xFC(125 个)
 *   0xE0 段的累计基址 5828 = 31 区(0x81~0x9F) * 188, 已独立验算。
 *
 * 【本模块特有的两点】
 *   1. ASCII/半角走的是本文件自己的 GetSJISASCCharacterData, 【不是】
 *      font_ascii.c 的 GetASCIICharacterData —— 区别是这里没有 asc > 127 的
 *      入口检查, 且缓冲上限判的是 ascpixel.nbytes * 2、超了只打印不重分配。
 *   2. TextOutW_SJIS 在 ConvertUTF16toSJIS 之后会判转换结果是否落在
 *      【单字节区 0x20~0xDF】(含半角片假名), 是则按半角走 ASCII 取模。
 *
 * 【段属性】代码放在 .font_sjis.text。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_sjis.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"
#include "jl_debug.h"    /* printf / puts: 显式包含, 保证本文件自包含 */

extern u8 InitFont_ASCII(struct font_info *info);

bool InitFont_SJIS(struct font_info *info);
u8 GetSJISASCCharacterData(struct font_info *info, u16 asc);
u8 GetSJISCharacterData(struct font_info *info, u16 textCode);
u16 ConvertUTF16toSJIS(struct font_info *info, u16 utf);
u16 TextOut_SJIS(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_SJIS(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);

bool InitFont_SJIS(struct font_info *info)
{
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
 * @brief 取一个半角字符的点阵(SJIS 专用的 ASCII 取模)
 * @return 该字符宽度; 0 = 点阵字节数超出缓冲
 * @note 与 font_ascii.c 的 GetASCIICharacterData 不同: 这里【没有 asc > 127
 *       的入口检查】(半角片假名 0xA1~0xDF 要能进来), 上限判的是
 *       ascpixel.nbytes * 2, 而且超了只打印错误、【不重分配缓冲】。
 */
u8 GetSJISASCCharacterData(struct font_info *info, u16 asc)
{
    ASCSTRUCT ascinfo;
    u32 addr;
    u16 nbytes;

    /*
     * 入口上界取 255, 【不能照搬 font_ascii 的 asc > 127】—— 这一路要支持
     * 半角片假名(0xA1~0xDF), 正好在 127 以上; 索引表是按单字节码建的,
     * 所以 255 是自然上界。不挡的话, asc 多大都会照算 asc * 4 + 2 去 fseek。
     */
    if (asc > 255) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, asc * 4 + 2);

    /* 读失败必须返回, 否则会拿栈垃圾当索引表项用。 */
    if (font_sd_fread(info->ascpixel.file.fd, &ascinfo, sizeof(ASCSTRUCT)) != sizeof(ASCSTRUCT)) {
        return 0;
    }

    if (info->ascpixel.pixelbuf) {
        nbytes = ascinfo.width * ((info->ascpixel.size + 7) / 8);
        if (nbytes > info->ascpixel.nbytes * 2) {
            printf("error:pixelbuf overlay!\n");
            return 0;
        }

        ascinfo.addr = font_ntoh(ascinfo.addr);
        font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, ascinfo.addr);

        /* 读失败必须返回 0: 此时 pixelbuf 里是上一个字的点阵, 照常返回宽度
         * 就是"显示上一个字"。 */
        if (font_sd_fread(info->ascpixel.file.fd, info->ascpixel.pixelbuf, nbytes) != (int)nbytes) {
            return 0;
        }
    }

    return ascinfo.width;
}

/*
 * @brief 取一个 Shift-JIS 全角字的点阵
 * @return 字高; 0 = 非法内码 / 没有点阵缓冲
 */
u8 GetSJISCharacterData(struct font_info *info, u16 textCode)
{
    u8 data_high = textCode >> 8;
    u8 data_low = textCode;
    u32 offset = -1;
    u32 addr;
    u32 ansi_offset = 6;

    if ((data_high >= 0x81) && (data_high < 0xA0)) {
        if ((data_low >= 0x40) && (data_low <= 0x7E)) {
            offset = (data_high - 0x81) * 188 + (data_low - 0x40);
        } else if ((data_low >= 0x80) && (data_low <= 0xFC)) {
            offset = (data_high - 0x81) * 188 + (data_low - 0x80) + 63;
        }
    } else if ((data_high >= 0xE0) && (data_high <= 0xEF)) {
        if ((data_low >= 0x40) && (data_low <= 0x7E)) {
            offset = (data_high - 0xE0) * 188 + (data_low - 0x40) + 5828;
        } else if ((data_low >= 0x80) && (data_low <= 0xFC)) {
            offset = (data_high - 0xE0) * 188 + (data_low - 0x80) + 63 + 5828;
        }
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

u16 ConvertUTF16toSJIS(struct font_info *info, u16 utf)
{
    u8 gbk[2] = {0};
    u32 offset = utf * 2;
    u32 addr;
    u32 table_offset = 0;

    if (utf < 0x0480) {
        addr = offset;
    } else if ((utf >= 0x2000) && (utf <= 0x33FF)) {
        addr = offset - 14080;
    } else if ((utf >= 0x4E00) && (utf <= 0x9FFF)) {
        addr = offset - 27392;
    } else if (utf > 0xF87F) {
        addr = offset - 72704;
    } else {
        return 0;
    }

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            table_offset = lange_info_table[info->codepage].table_offset;
        }
    }

    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, table_offset + addr);
    /* 表项读失败必须返回 0(查不到): 否则 gbk[] 里是上一次或未初始化的内容,
     * 会被当成合法内码返回, 后面拿它去取模。 */
    if (font_sd_fread(info->tabfile.fd, gbk, 2) != 2) {
        return 0;
    }

    return (gbk[0] << 8) | gbk[1];
}

/*
 * @brief Shift-JIS 内码字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+1)
 * @note 全角首字节的判定是 Shift-JIS 的两段 lead byte 区:
 *       0x81~0x9F 与 0xE0~0xFC(不是像 GBK 那样简单判 > 0x7F)。
 */
u16 TextOut_SJIS(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
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
        if ((((str[i] >= 0x81) && (str[i] <= 0x9F))
             || ((str[i] >= 0xE0) && (str[i] <= 0xFC))) && ((i + 1) < len)) {
            text = (str[i] << 8) | str[i + 1];
            width = GetSJISCharacterData(info, text);
            step = 2;
            ascii = 0;
            if (width == 0) {
                width = GetSJISASCCharacterData(info, '-');
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
            width = GetSJISASCCharacterData(info, text);
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
 * @brief UTF-16 字符串输出(转 Shift-JIS 后取模)
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+2)
 * @note 转换结果落在 0x20~0xDF(ASCII + 半角片假名)时按【半角】走 ASCII 取模,
 *       这是 Shift-JIS 特有的一步, 其它 font_* 模块没有。
 */
u16 TextOutW_SJIS(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
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
            width = GetSJISASCCharacterData(info, text);
            ascii = 1;
        } else {
            text = ConvertUTF16toSJIS(info, text);
            if ((text >= 0x20) && (text <= 0xDF)) {
                width = GetSJISASCCharacterData(info, text);
                ascii = 1;
            } else {
                width = GetSJISCharacterData(info, text);
                ascii = 0;
                if (width == 0) {
                    width = GetSJISASCCharacterData(info, '-');
                    ascii = 1;
                }
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
 *  1) 【半角取模是本文件自己的一份】GetSJISASCCharacterData 与 font_ascii.c 的
 *     同类函数有两点不同: 入口上界是 255(要放过半角片假名 0xA1~0xDF), 缓冲
 *     不够时【只打印错误、不重分配】。
 *     这里有意不照搬 font_ascii 的 free+malloc 自愈 —— 那套重分配本身带着
 *     "nbytes 语义两处理解不一致"的问题(见 font_textout.c 的注意事项),
 *     复制过来是扩散而不是解决。代价是这一路碰到超宽字符会持续显示失败。
 *
 *  2) 【0xF0~0xFC 这一区字库里没有数据】TextOut_SJIS 按 Shift-JIS 标准把
 *     0x81~0x9F 与 0xE0~0xFC 都当全角首字节, 而 GetSJISCharacterData 的区位
 *     换算只覆盖到 0xEF(标准里 0xF0 之后是用户自定义区)。落在 0xF0~0xFC 的
 *     码位会被当全角吃掉 2 字节、取模失败后显示 '-'。
 *     把 TextOut 的区间缩到 0xEF 并不对 —— 那会让这些码位改按单字节解析,
 *     显示同样是错的。等字库这一区的数据与偏移算法明确后再补。
 *
 *  3) 【offset 用 -1 作哨兵】赋给 u32 再用 `== -1` 判定, 两边都是 0xFFFFFFFF,
 *     行为正确。区位不合法时就是这条路径。
 *
 *  4) 【TextOutW_SJIS 多一步单字节区判定】转换结果落在 0x20~0xDF(ASCII +
 *     半角片假名)时按半角走 ASCII 取模, 这是 Shift-JIS 特有的一步。
 */
