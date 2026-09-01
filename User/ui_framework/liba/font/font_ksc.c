/*
 * font_ksc.c —— KSC(韩文)字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_ksc.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_ksc.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_ksc.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     InitFont_KSC@11   GetKSCCharacterData@44   ConvertUTF16toKSC@93
 *     TextOut_KSC@121   TextOutW_KSC@201
 *   局部变量名与类型取自 DWARF —— 注意这里两个偏移局部分别叫 ansi_offset
 *   (取模那一路)与 table_offset(转换表那一路), 与 font_gbk.c 里统一叫
 *   codepage_offset 不同。
 *
 * 【KSC 区位换算(从 IR 的常量逐段反推并验算过累计基址)】
 *   高字节 0x81~0xC5: 每区 178 个字, 低字节分三段
 *       0x41~0x5A(26 个) / 0x61~0x7A(26 个) / 0x81~0xFE(126 个)
 *   高字节 0xC6      : 该区被截断 —— 低字节只有 0x41~0x52(18 个) 与
 *                      0xA1~0xFE(94 个), 所以第二段的起始序号是 18 而不是 26
 *   高字节 0xC7~0xC8: 每区 94 个字(低字节 0xA1~0xFE), 累计基址 12394
 *                      = 69 区 * 178 + 0xC6 区的 (18 + 94)
 *   高字节 0xCA~0xFD: 每区 94 个字, 累计基址 12582 = 12394 + 2 区 * 94
 *   (0xC9 这一区在原库里【没有覆盖】, 直接落到 offset = -1)
 *
 * 【与 font_gbk.c 的结构差异】
 *   TextOut_KSC / TextOutW_KSC 不做 info->offset 的起始偏移(GBK 那一路会做);
 *   两个 TextOut 都是 '\n' 换行、'\r' 忽略(与 font_big5.c 一致)。
 *
 * 【段属性】原库代码在 .font_ksc.text(见 ref IR 的 section 属性)。唯一的字符串
 *   常量 "r" 在原厂 IR 里没有 section 属性, 所以不能开 const_seg。
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
 * @brief 取一个 KSC 内码字的点阵到 info->pixel.pixelbuf
 * @return 字高; 0 = 非法内码 / 没有点阵缓冲
 * @note 区位换算见文件头注释。0xC6 区被截断、0xC9 区未覆盖, 都是原库的实际布局。
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
    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回字高 —— 表现为"显示上一个字", 排查起来很费劲。 */
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
    /* 加固: 原库丢弃返回值。表项读失败时 gbk[] 是上一次的内容(或未初始化的
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
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [保留] 1 —— 高字节 0xC9 这一区【完全没有覆盖】, 落到该区的内码显示成横杠。
 *                这是【数据层面】的缺口: 要补得知道该区在字库文件里的确切起止
 *                内码与偏移算法, 而这套 .PIX/.TAB 格式没有公开文档。照着相邻区
 *                "推算"一个偏移填进去, 只会把"显示横杠"变成"显示乱码", 后者更
 *                难发现。留待拿到字库打包工具的格式说明后再补。
 *   [保留] 2 —— offset = -1 的哨兵写法。这【不是缺陷】: -1 转 u32 与 == -1 比较
 *                在 C 里都是 0xFFFFFFFF, 行为完全正确, 只是可读性一般。
 *                改它没有实际收益, 只会增加与原库逐条对照时的噪声。
 *   [已修] 3 —— 不检查 fread -> 已补(读失败返回 0, 不再"显示上一个字")。
 *
 * 1) 高字节 0xC9 这一区【完全没有覆盖】(0xC6 之后直接跳到 0xC7~0xC8, 再跳到
 *    0xCA~0xFD), 落到该区的内码一律返回 0 —— 显示成 '-'。参考 IR 的四段
 *    区间判断确认原库就是这样, 不是还原漏了一段。
 *
 * 2) `offset = -1` 是把 -1 赋给 u32 再用 `== -1` 判定, 与其它 font_* 一致。
 *
 * 3) 不检查 font_sd_fread 的返回值, 读失败时 pixelbuf 里是上一个字的点阵,
 *    却照常返回字高 —— 表现为"显示上一个字"。
 */
