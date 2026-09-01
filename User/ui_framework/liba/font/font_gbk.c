/*
 * font_gbk.c —— GBK / GB2312 字库: 初始化、内码取模、UTF-16 转内码、文本输出
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_gbk.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_gbk.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_gbk.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     InitFont_GBK@11         GetGBKCharacterData@44    ConvertUTF16toGBK@75
 *     GetGB2312CharacterData@105  ConvertUTF16toGB2312@133
 *     TextOut_GBK@195         TextOutW_GBK@285
 *   局部变量名与类型全部取自 DWARF, 例如 TextOut_GBK 的
 *   text/width/height/xpos/ypos/i(u16) + step/pixel_size/ascii(u8),
 *   ConvertUTF16to* 的 gbk[2]/offset/addr/codepage_offset。
 *   ConvertUTF16toGB2312 的 20 段区间表与每段的地址偏移常量, 是从参考 IR 的
 *   `add i16 %utf, -A` + `icmp ult ..., L` 和汇合处 phi 的常量逐段提取出来的
 *   (已验算相邻段首尾相接: 每段 addr = utf*2 + K, 上段末 +2 = 下段首)。
 *   本模块无 ASSERT。
 *
 * 【段属性】原库代码在 .font_gbk.text(见 ref IR 的 section 属性)。唯一的字符串
 *   常量 "r" 在原厂 IR 里没有 section 属性, 所以不能开 const_seg。
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
    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回字高 —— 表现为"显示上一个字", 排查起来很费劲。 */
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
    /* 加固: 原库丢弃返回值。表项读失败时 gbk[] 是上一次的内容(或未初始化的
     * 栈内容), 会被当成合法内码返回, 后面拿它去取模。 */
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
    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回字高 —— 表现为"显示上一个字", 排查起来很费劲。 */
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
    /* 加固: 原库丢弃返回值。表项读失败时 gbk[] 是上一次的内容(或未初始化的
     * 栈内容), 会被当成合法内码返回, 后面拿它去取模。 */
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

    /*
     * 取"汉字字高与 ASCII 字高里较大的那个"。写成【选指针再取 size】而不是
     * if/else 里各取一次 —— 参考 IR 是 `select %struct.font*` 之后再 load 一次
     * (即比较用的那两次 load 之外还有第三次)。写成 if/else 会被 GVN 把分支里的
     * load 与比较用的那两次合并掉, 变成"选值", 与原厂对不上。
     */
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
 * @note 与 TextOut_GBK 的不对称之处(原库如此, 见文末清单):
 *       1. 换行符处理【已加固统一】: 原库这里做换行的是 '\r'、'\n' 被忽略,
 *          与 TextOut_GBK 恰好相反; 现已改成与它一致('\n' 换行、'\r' 忽略)。
 *       2. 控制字符(< 0x20)在这里被替换成 '*' 显示; TextOut_GBK 里会原样
 *          送去 GetASCIICharacterData。
 * @note 循环里【每个字符都重新读一次 info->bigendian】, 参考 IR 的 load 就在
 *       循环体内, 还原时不要顺手提到循环外。
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

    /*
     * 取"汉字字高与 ASCII 字高里较大的那个"。写成【选指针再取 size】而不是
     * if/else 里各取一次 —— 参考 IR 是 `select %struct.font*` 之后再 load 一次
     * (即比较用的那两次 load 之外还有第三次)。写成 if/else 会被 GVN 把分支里的
     * load 与比较用的那两次合并掉, 变成"选值", 与原厂对不上。
     */
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
             * 加固: 原库这里 '\n' 被【直接忽略】、只有 '\r' 换行, 而同文件的
             * TextOut_GBK 恰好相反('\n' 换行、'\r' 忽略)。后果是只带 '\n' 的
             * 文本走 UTF-16 这一路时【完全不换行】。
             *
             * 改成与 TextOut_GBK 一致('\n' 换行、'\r' 忽略), 而不是"两个都换行"
             * —— 后者会让 "\r\n" 换两行, 是新的退步。按现在这样:
             *   "\n"   -> 换一行(原库不换, 正是被修好的那种)
             *   "\r\n" -> '\r' 忽略、'\n' 换行, 仍是一行
             *   "\r"   -> 不再换行(纯 \r 换行是老 Mac 风格, 资源里几乎不会出现)
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
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— TextOut_GBK 与 TextOutW_GBK 对 CR/LF 的处理【正好相反】。
 *                已把 TextOutW_GBK 改成与 TextOut_GBK 一致(LF 换行、CR 忽略)。
 *                【没有】改成"两个都换行" —— 那会让 CRLF 换两行, 是新的退步。
 *   [保留] 2 —— offset = -1 的哨兵写法。这【不是缺陷】: -1 转 u32 与 == -1 比较
 *                在 C 里都是 0xFFFFFFFF, 行为完全正确, 只是可读性一般。
 *                改它没有实际收益, 只会增加与原库逐条对照时的噪声。
 *   [已修] 3 —— 两个 Get*CharacterData 不检查 fread -> 已补。
 *   [保留] 4 —— InitFont_GBK 里未使用的局部变量 i。同 rle.c 的死变量 i: 删它对代码
 *                生成毫无影响, 只会增加对照噪声。
 *
 * 1) TextOut_GBK 与 TextOutW_GBK 对 CR/LF 的处理【正好相反】:
 *      TextOut_GBK :  '\n'(0x0A) 换行, '\r'(0x0D) 忽略
 *      TextOutW_GBK:  '\r'(0x0D) 换行, '\n'(0x0A) 忽略
 *    参考 IR 里两个 switch 的 case 常量确认原库就是这样(TextOut_GBK 的
 *    case 10 走换行分支, TextOutW_GBK 的 case 13 走换行分支), 不是还原笔误。
 *    后果: 同一段带 "\r\n" 的文本, 走内码路径与走 UTF-16 路径的换行行为一致
 *    (两者各认一个), 但只带 "\n" 的文本在 UTF-16 路径下不会换行。
 *
 * 2) GetGBKCharacterData / GetGB2312CharacterData 里 `offset = -1` 是把 -1
 *    赋给 u32, 再用 `offset == -1` 判定。虽然在本目标上能正常工作, 但用一个
 *    合法区位算不出来的哨兵值更稳妥。
 *
 * 3) 两个 Get*CharacterData 都不检查 font_sd_fread 的返回值, 读失败时
 *    pixelbuf 里是上一个字的点阵, 却照常返回字高 —— 表现为"显示上一个字"。
 *
 * 4) InitFont_GBK 里的局部变量 i 从未使用(DWARF 里确有这个变量, 说明原库
 *    就声明了它)。无副作用, 保留以保持与原库一致。
 */
