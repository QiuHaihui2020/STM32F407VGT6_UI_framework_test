/*
 * font_sjis.c —— Shift-JIS(日文)字库: 初始化、半角/全角取模、UTF-16 转内码、
 *                文本输出
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_sjis.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_sjis.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_sjis.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     InitFont_SJIS@11        GetSJISASCCharacterData@35   GetSJISCharacterData@58
 *     ConvertUTF16toSJIS@96   TextOut_SJIS@127             TextOutW_SJIS@207
 *   局部变量名与类型取自 DWARF。
 *
 * 【Shift-JIS 区位换算(从 IR 常量反推并验算过累计基址)】
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
 * 【段属性】原库代码在 .font_sjis.text(见 ref IR 的 section 属性)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_sjis.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"
#include "jl_debug.h"    /* printf / puts: 原厂靠别处间接带入 */

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
     * 加固: 原库没有任何入口检查, 传进来的 asc 有多大就照算 asc * 4 + 2 去
     * fseek, 越界读文件、只靠 fread 失败兜着。
     * 【注意不能照搬 font_ascii 的 asc > 127】—— 这一路要支持半角片假名
     * (0xA1~0xDF), 正好在 127 以上。索引表是按单字节码建的, 所以上界取 255。
     */
    if (asc > 255) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, asc * 4 + 2);

    /* 加固: 原库丢弃返回值, 读失败会拿栈垃圾当索引表项用。 */
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

        /* 加固: 原库丢弃返回值, 读失败时 pixelbuf 里是上一个字的点阵,
         * 却照常返回宽度 —— 表现为"显示上一个字"。 */
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
    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回字高 —— 表现为"显示上一个字", 排查起来很费劲。 */
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
    /* 加固: 原库丢弃返回值。表项读失败时 gbk[] 是上一次的内容(或未初始化的
     * 栈内容), 会被当成合法内码返回, 后面拿它去取模。 */
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
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [保留] 1 —— GetSJISASCCharacterData 缓冲不够时只打印不重分配, 不像
 *                font_ascii.c 那样 free+malloc 自愈。【不照搬那一套】: font_ascii
 *                的重分配本身就带着"nbytes 语义两边理解不一致"的老问题
 *                (见 font_textout.c 第 3 条), 复制过来是扩散而不是修复。
 *   [已修] 2 —— 没有入口检查, asc 多大都照算 asc*4+2 去 fseek -> 已补上界。
 *                注意【不能照搬 font_ascii 的 asc > 127】: 这一路要支持半角
 *                片假名(0xA1~0xDF), 正好在 127 以上, 故上界取 255(索引表按
 *                单字节码建)。两处 fread 也补了返回值判断。
 *   [保留] 3 —— TextOut_SJIS 认 0xE0~0xFC 为全角首字节, 而 GetSJISCharacterData
 *                只覆盖到 0xEF, 0xF0~0xFC 会被当全角吃掉 2 字节再显示横杠。
 *                按 SJIS 标准 0xF0~0xFC 确实是全角首字节(用户自定义区), 所以
 *                问题在【字库没有这一区的数据】, 不是判断写错。把 TextOut 的
 *                区间缩到 0xEF 会让这些码位改按单字节解析, 同样是错的显示。
 *                与 font_ksc 第 1 条同类, 等字库格式说明。
 *   [保留] 4 —— offset = -1 的哨兵写法, 行为正确, 见 font_big5.c 的同条说明。
 *
 * 1) GetSJISASCCharacterData 缓冲不够时【只打印不重分配】就返回 0
 *    (font_ascii.c 的同类函数会 free + malloc 重开)。也就是说这一路碰到宽字符
 *    就永久显示失败, 而不是自愈。
 *
 * 2) GetSJISASCCharacterData 没有 `asc > 127` 的入口检查 —— 这是半角片假名
 *    (0xA1~0xDF)必需的, 但同时也意味着传入任意大的 asc 都会照算
 *    `asc * 4 + 2` 去 fseek, 越界读文件, 只靠 fread 失败兜着。
 *
 * 3) TextOut_SJIS 的全角首字节区间写作 0xE0~0xFC, 而 GetSJISCharacterData
 *    只覆盖到 0xEF。首字节落在 0xF0~0xFC 时会当全角进来、取模失败、
 *    退回显示 '-' 并吃掉 2 个字节。参考 IR 两处的常量确认原库就是这样
 *    (TextOut 的 `(c+32) <u 29` 覆盖到 0xFC; GetSJISCharacterData 的
 *     `(textCode & 0xF000) == 0xE000` 只覆盖 0xE0~0xEF)。
 *
 * 4) `offset = -1` 是把 -1 赋给 u32 再用 `== -1` 判定, 与其它 font_* 一致。
 */
