/*
 * font_other_language.c —— 除中日韩内码字库之外的其它语言字库
 *   (代码页 CP874/CP1250~CP1258 + Unicode 扩展块 + 泰语/天城文/藏文组合显示)
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_other_language.c.o 还原。该库交付的是
 *   LLVM bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_other_language.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_other_language.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     InitFont_OtherLanguage_GBK@42   InitFont_OtherLanguage@71
 *     ConvertUTF16toOtherLanguage@131 GetOtherLanguageCharacterData@176
 *     GetUnicodeCharacterData@214     TextOut_OtherLanguage@259
 *     TextOutW_IndonesiaVietnam@308   TextOutW_OtherLanguage@379
 *     ConvertUTF16tocodepage@472      TextOutW_AllLanguage@509
 *   局部变量名与类型、lang[] 表的 19 项内容、code_block 的字段名(begin/end/
 *   num/addr)全部取自 DWARF。
 *
 * 【段属性】原库代码在 .font_other_language.text, lang[] 在
 *   .font_other_language.text.const, GetUnicodeCharacterData 里的静态
 *   fd_uni 在 .font_other_language.data(见 ref IR 的 section 属性)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".font_other_language.data")
#pragma data_seg(".font_other_language.data")
#pragma const_seg(".font_other_language.text.const")
#pragma code_seg(".font_other_language.text")
#endif

#define LOG_TAG             ""
#define LOG_INFO_ENABLE
#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/language_list.h"
#include "jl_debug.h"

typedef struct {
    u8 langid;
    u8 codepage;
} LANG;

/* Unicode 扩展字库(extfile)的块索引项, 每项 16 字节 */
struct code_block {
    u32 begin;
    u32 end;
    u32 num;
    u32 addr;
};

extern u8 GetASCIICharacterData(struct font_info *info, u16 asc);

bool InitFont_OtherLanguage_GBK(struct font_info *info);
bool InitFont_OtherLanguage(struct font_info *info);
u16 ConvertUTF16toOtherLanguage(struct font_info *info, u16 utf);
u8 GetOtherLanguageCharacterData(struct font_info *info, u16 asc);
u8 GetUnicodeCharacterData(struct font_info *info, u16 textCode);
u16 TextOut_OtherLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_IndonesiaVietnam(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u16 TextOutW_OtherLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);
u8 ConvertUTF16tocodepage(u16 utf);

/*
 * 泰语组合显示用的字形描述结构。原库把它定义在一个本 SDK 里不存在的头文件里
 * (DWARF 的 file 指向另一个源), 这里按 DWARF 给出的布局原样声明:
 *   unicode@0(2) ansi@2(2) buf@4(4) attr/bigendian 位域@8(合占 1 字节)
 *   width@10(2) height@12(2), sizeof = 16。
 * attr/bigendian 的具体位宽 DWARF 没给全, 但二者合占 1 字节且本文件从不访问,
 * 拆成 4+4 与原厂布局一致。
 */
typedef struct {
    u16 unicode;
    u16 ansi;
    u8 *buf;
    u8 attr      : 4;
    u8 bigendian : 4;
    u16 width;
    u16 height;
} THaiASCSTRUCT;

/*
 * 下面三个泰语函数在【整个 SDK 里都没有定义】—— 只有彩屏用的 font_new.a 里有
 * 前两个, 本工程不链接它。它们只被 TextOutW_AllLanguage 调用, 而该函数在本
 * 固件里是死代码(lange_info_table 恒为 NULL, font_textout.c 里那条分支被 LTO
 * 整段消掉), 所以链接不会报 undefined reference。
 * ⚠️ 如果将来有人调用 font_set_offset_table() 让 lange_info_table 非空,
 *    这三个符号就会变成真实的未定义引用、链接失败 —— 那时要么一并移植泰语
 *    那套代码, 要么把 TextOutW_AllLanguage 裁掉。
 */
extern u8 IsThaiOneWord_W(u8 *str, int len, int *i, THaiASCSTRUCT *w2,
                          THaiASCSTRUCT *w3, u8 attr, u8 bigendian);
extern u8 GetThaiLanguageCharacterData(struct font_info *info, u16 ansi, u8 **buf);
extern u8 ThaiLanguagecompose(struct font_info *info, u16 *width, THaiASCSTRUCT *w1,
                              THaiASCSTRUCT *w2, THaiASCSTRUCT *w3);

extern u16 ConvertUTF16toGB2312(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toGBK(struct font_info *info, u16 utf);
extern u8 GetGB2312CharacterData(struct font_info *info, u16 textCode);
extern u8 GetGBKCharacterData(struct font_info *info, u16 textCode);
extern u16 ConvertUTF16toKSC(struct font_info *info, u16 utf);
extern u8 GetKSCCharacterData(struct font_info *info, u16 textCode);

u16 TextOutW_AllLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y);

/* 语言 id -> 代码页 映射表(19 项, 顺序与原厂 IR 里的初值逐项一致) */
static const LANG lang[] = {
    {Thai,       CP874},
    {Czech,      CP1250},
    {Polish,     CP1250},
    {Hungarian,  CP1250},
    {Romanian,   CP1250},
    {Russian,    CP1251},
    {English,    CP1252},
    {French,     CP1252},
    {German,     CP1252},
    {Italian,    CP1252},
    {Dutch,      CP1252},
    {Portuguese, CP1252},
    {Spanish,    CP1252},
    {Swedish,    CP1252},
    {Danish,     CP1252},
    {Turkey,     CP1254},
    {Hebrew,     CP1255},
    {Arabic,     CP1256},
    {Vietnam,    CP1258},
};

/*
 * @brief 只打开点阵字模文件(不碰 ASCII 字库与转换表)
 * @return 1 = 成功; 0 = 打开失败
 */
bool InitFont_OtherLanguage_GBK(struct font_info *info)
{
    int i;
    u32 offset = 0;

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
    /* 加固: 原库丢弃返回值, 读不到字高就拿旧值(首次是未初始化内存)继续算。 */
    if (font_sd_fread(info->pixel.file.fd, &info->pixel.size, 1) != 1) {
        font_sd_fclose(info->pixel.file.fd);
        info->pixel.file.fd = NULL;
        return 0;
    }
    info->pixel.nbytes = ((info->pixel.size + 7) / 8) * info->pixel.size;

    return 1;
}

/*
 * @brief 按 language_id 定代码页, 打开 ASCII 字模、Unicode 扩展文件、转换表
 * @return 1 = 成功; 0 = 某个文件打开失败
 * @note 表里查不到该语言时退回 CP1252(西欧), 并打印一行提示。
 */
bool InitFont_OtherLanguage(struct font_info *info)
{
    int i;

    info->codepage = 0;

    for (i = 0; i < sizeof(lang) / sizeof(lang[0]); i++) {
        if (lang[i].langid == info->language_id) {
            break;
        }
    }

    if (i < sizeof(lang) / sizeof(lang[0])) {
        info->codepage = lang[i].codepage;
    } else {
        printf("Not support this langid ,now set this in page CP1252\n");
        info->codepage = CP1252;
    }

    if (info->ascpixel.file.name) {
        info->ascpixel.file.fd = font_sd_fopen(info->ascpixel.file.name, "r");
        if (info->ascpixel.file.fd == NULL) {
            info->sta |= FT_ERROR_NOASCPIXFILE;
            printf("ascpixel file open failed!\n");
            return 0;
        }
        printf("ascpixel file open successful\n");
        font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, 0);
        /* 加固: 原库丢弃返回值, 同上。 */
        if (font_sd_fread(info->ascpixel.file.fd, &info->ascpixel.size, 1) != 1) {
            font_sd_fclose(info->ascpixel.file.fd);
            info->ascpixel.file.fd = NULL;
            return 0;
        }
        info->ascpixel.nbytes = ((info->ascpixel.size + 7) / 8) * info->ascpixel.size;
    }

    if (info->extfile.name) {
        log_info("\033[32m\033[1mready open extfile!\n\033[0m");
        info->extfile.fd = font_sd_fopen(info->extfile.name, "r");
        if (info->extfile.fd == NULL) {
            log_info("\033[32m\033[1mextfile open faild!\n\033[0m");
            info->sta |= FT_ERROR_NOASCPIXFILE;
            return 0;
        }
        log_info("\033[32m\033[1mextfile open successful!\n\033[0m");
    }

    if (info->tabfile.name) {
        info->tabfile.fd = font_sd_fopen(info->tabfile.name, "r");
        if (info->tabfile.fd == NULL) {
            info->sta |= FT_ERROR_NOTABFILE;
            printf("table_file open failed!\n");
            return 0;
        }
        log_info("\033[32m\033[1mtabfile open successful!\n\033[0m");
    }

    return 1;
}

/*
 * @brief UTF-16 码位 -> 当前代码页的单/双字节内码
 * @return 内码; 0 = 超出该代码页范围
 * @note 泰语(CP874)只查 0x0E00~0x0E7F 这一段; 其它代码页查整表, 但结果上限
 *       按代码页区分 —— 越南语(CP1258)允许到 357, 其它只允许到 255。
 * @note 这里的字节序与其它 Convert* 【相反】: 高字节取的是 [1]。
 */
u16 ConvertUTF16toOtherLanguage(struct font_info *info, u16 utf)
{
    u8 OtherLanguage[2];
    u16 code;
    u32 addr;
    u32 offset = 0;

    if (info->codepage && lange_info_table) {
        if (lange_info_table[info->codepage].codepage == info->codepage) {
            offset = lange_info_table[info->codepage].table_offset;
        }
    }

    if (info->codepage == CP874) {
        if ((utf & 0xFF80) == 0x0E00) {
            addr = utf * 2;
            font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, offset + addr);
            /* 加固: 原库丢弃返回值, 读失败会把上一次的内容当内码返回。 */
            if (font_sd_fread(info->tabfile.fd, OtherLanguage, 2) != 2) {
                return 0;
            }
            code = (OtherLanguage[1] << 8) | OtherLanguage[0];
            return code;
        }
        return 0;
    }

    addr = utf * 2;
    font_sd_fseek(info->tabfile.fd, SD_SEEK_SET, offset + addr);
    /* 加固: 原库丢弃返回值, 同上。 */
    if (font_sd_fread(info->tabfile.fd, OtherLanguage, 2) != 2) {
        return 0;
    }
    code = (OtherLanguage[1] << 8) | OtherLanguage[0];

    if (info->codepage == CP1258) {
        if (code > 357) {
            return 0;
        }
    } else {
        if (code > 255) {
            return 0;
        }
    }

    return code;
}

/*
 * @brief 取一个代码页字符的点阵到 info->ascpixel.pixelbuf
 * @return 该字符宽度; 0 = 点阵字节数超出缓冲
 * @note 与 font_sjis.c 的 GetSJISASCCharacterData 同构: 上限判 nbytes * 2,
 *       超了【只打印不重分配】。
 */
u8 GetOtherLanguageCharacterData(struct font_info *info, u16 asc)
{
    ASCSTRUCT ascinfo;
    u32 addr;
    u16 nbytes;
    u32 offset;

    /* 加固: 原库无入口检查, asc 多大都照算 asc * 4 + 2 去 fseek。
     * 这一路同样要支持 127 以上的单字节码, 所以上界取 255 而不是 127。 */
    if (asc > 255) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, asc * 4 + 2);

    /* 加固: 原库丢弃返回值, 读失败会拿栈垃圾当索引表项。 */
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

        /* 加固: 原库丢弃返回值, 读失败即"显示上一个字"。 */
        if (font_sd_fread(info->ascpixel.file.fd, info->ascpixel.pixelbuf, nbytes) != (int)nbytes) {
            return 0;
        }
    }

    return ascinfo.width;
}

/*
 * @brief 从 Unicode 扩展文件(extfile)里按码位取点阵
 * @return 字符宽度; 0 = 没有扩展文件
 *
 * @note 文件布局: [u16 font_size][u16 block_num][block_num 个 code_block]
 *       每个 code_block 描述一段连续码位 begin~end 及其索引表地址 addr,
 *       索引表每项 4 字节(就是一个 ASCSTRUCT)。
 * @note 块索引表【借用 ascpixel.pixelbuf 当临时缓冲】(够大时), 不够才 malloc;
 *       结尾靠"指针是否等于 pixelbuf"决定要不要 free。
 * @note 循环命中后【没有 break】, 会把剩下的块全扫一遍 —— 见文末 TODO。
 */
u8 GetUnicodeCharacterData(struct font_info *info, u16 textCode)
{
    ASCSTRUCT asc;
    u8 width = 0;
    int i;
    u16 font_size;
    u16 block_num;
    struct code_block *blocks;
    static void *fd_uni;

    if (info->extfile.fd == NULL) {
        return 0;
    }

    fd_uni = info->extfile.fd;

    font_sd_fseek(fd_uni, SD_SEEK_SET, 0);

    /* 加固: 三处 fread 原库都丢弃返回值。这里读的是块索引表的表头,
     * 读失败时 block_num 是栈垃圾, 会拿它去 malloc 和循环。 */
    if (font_sd_fread(fd_uni, &font_size, 2) != 2) {
        return 0;
    }
    if (font_sd_fread(fd_uni, &block_num, 2) != 2) {
        return 0;
    }
    if (block_num == 0) {
        return 0;
    }
    (void)font_size;    /* 加固: 原库读出来就没用过, 显式标明 */

    if (info->ascpixel.nbytes > block_num * sizeof(struct code_block)) {
        blocks = (struct code_block *)info->ascpixel.pixelbuf;
    } else {
        blocks = malloc(block_num * sizeof(struct code_block));
        /* 加固: 原库【不检查 malloc 返回值】就 fread 进去。 */
        if (blocks == NULL) {
            return 0;
        }
    }

    if (font_sd_fread(fd_uni, blocks, block_num * sizeof(struct code_block))
        != (int)(block_num * sizeof(struct code_block))) {
        if (blocks != (struct code_block *)info->ascpixel.pixelbuf) {
            free(blocks);
        }
        return 0;
    }

    for (i = 0; i < block_num; i++) {
        if ((textCode >= blocks[i].begin) && (textCode <= blocks[i].end)) {
            font_sd_fseek(fd_uni, SD_SEEK_SET,
                          blocks[i].addr + (textCode - blocks[i].begin) * 4);
            if (font_sd_fread(fd_uni, &asc, 4) != 4) {
                break;
            }

            /*
             * 加固: asc.size 直接来自字库文件, 原库不校验就往 pixelbuf 里读 ——
             * 文件损坏或字号不匹配时直接溢出写。
             */
            if (asc.size > info->ascpixel.nbytes) {
                break;
            }

            font_sd_fseek(fd_uni, SD_SEEK_SET, asc.addr);
            if (font_sd_fread(fd_uni, info->ascpixel.pixelbuf, asc.size) != (int)asc.size) {
                break;
            }
            width = asc.width;

            /*
             * 加固【本函数最要紧的一处】: 原库命中后【不 break】, 会把剩下的块
             * 全扫一遍。这不只是浪费 —— 当 blocks 借用的正是
             * info->ascpixel.pixelbuf 时(上面那个 if 分支), 紧邻的这次
             * font_sd_fread(fd_uni, info->ascpixel.pixelbuf, asc.size)
             * 【已经把 blocks 指向的索引表覆盖成点阵数据了】, 而循环还要接着
             * 读 blocks[i].begin / end / addr —— 拿点阵字节当块索引用,
             * 可能再次"命中"、再次覆盖, 甚至 fseek 到任意位置。
             * 码位只可能落在一个块里, 命中即退出。
             */
            break;
        }
    }

    if (blocks != (struct code_block *)info->ascpixel.pixelbuf) {
        free(blocks);
    }

    return width;
}

/*
 * @brief 代码页内码(单字节)字符串输出
 * @return 实际消耗掉的字节数(遇到换行溢出时返回 i+1)
 * @note 单字节语言, 所以没有 pixel_size / ascii 之分, 行高直接用 ascpixel.size。
 */
u16 TextOut_OtherLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 width;
    u16 xpos = 0;
    u16 ypos = 0;
    u16 i;

    info->string_width = 0;
    info->string_height = 0;

    for (i = 0; i < len; i++) {
        if (str[i] == 0) {
            break;
        } else if (str[i] == '\r') {
            continue;
        } else if (str[i] == '\n') {
            ypos += info->ascpixel.size * info->ratio;
            if (ypos + info->ascpixel.size * info->ratio > info->text_height) {
                i++;
                break;
            }
            xpos = 0;
            continue;
        }

        width = GetOtherLanguageCharacterData(info, str[i]);

        xpos += info->ratio * width;
        info->string_width += info->ratio * width;

        if (xpos > info->text_width) {
            if (!(info->flags & FONT_SHOW_MULTI_LINE)) {
                break;
            }
            ypos += info->ratio * info->ascpixel.size;
            if (ypos + info->ratio * info->ascpixel.size > info->text_height) {
                break;
            }
            xpos = info->ratio * width;
        }

        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info, info->ascpixel.pixelbuf, width, info->ascpixel.size,
                              xpos + x - info->ratio * width, ypos + y);
            }
        }

        info->string_height += info->ascpixel.size;
    }

    return i;
}

/*
 * @brief 印尼语/越南语的 UTF-16 输出(带【按词换行】)
 * @return 消耗掉的字节数
 *
 * @note 算法是"两趟":正常渲染时遇到空格且已越过 x+80, 就把 i 退回 2 字节、
 *       置 tmp=1 进入【预扫描】; 预扫描只累加 tmp_len/tmp_i 不出像素, 直到
 *       下一个空格; 然后比较这一整个词的宽度决定是换行重排还是原地继续。
 *       tmp/jl/p 三个标志的含义:
 *         tmp = 1 预扫描中; jl = 1 预扫描已吃到过非空格; p = 1 允许再触发预扫描
 * @note 起始 xpos 是 x + 40(不是 0), 换行后是 x + 5, 都是原厂写死的常量。
 */
u16 TextOutW_IndonesiaVietnam(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width;
    u16 xpos;
    u16 ypos = 0;
    u16 i;
    u16 tmp_len = 0;
    u16 tmp_i = 0;
    u16 tmp = 0;
    u16 jl = 0;
    u16 p = 1;

    info->string_width = 0;
    /* 加固: 原库【不初始化 string_height】(其它 TextOut* 都在开头置 0),
     * 调用方拿到的是上一次调用留下的值。 */
    info->string_height = 0;
    info->bigendian &= 1;
    xpos = x + 40;

    for (i = 0; (i + 1) < len; i += 2) {
        text = (str[i + 1 - info->bigendian] << 8) | str[i + info->bigendian];

        if ((text >= 0x80) && (text <= 0x2122)) {
            text = ConvertUTF16toOtherLanguage(info, text);
            if (text == 0) {
                text = '-';
            }
        } else {
            if (text > 0x2122) {
                text = '-';
            }
        }

        if (tmp) {
            if (jl || (text == ' ')) {
                tmp_i += 2;
                tmp_len += GetOtherLanguageCharacterData(info, text);
                if (jl && (text == ' ')) {
                    if (tmp_len > (x + 3 - xpos + info->text_width)) {
                        ypos += info->ascpixel.size * info->ratio;
                        xpos = x + 5;
                        i = i + 2 - tmp_i;
                        tmp = 0;
                        continue;
                    }
                    i = i - tmp_i;
                    tmp = 0;
                    p = 0;
                    continue;
                }
                jl = 1;
                continue;
            }
            jl = 0;
            continue;
        }

        if ((text == ' ') && (xpos > (x + 80)) && p) {
            i = i - 2;
            tmp = 1;
            jl = 0;
            continue;
        }

        width = GetOtherLanguageCharacterData(info, text);
        xpos += info->ratio * width;
        info->string_width += info->ratio * width;

        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info, info->ascpixel.pixelbuf, width, info->ascpixel.size,
                              xpos - info->ratio * width, ypos + y);
            }
        }

        tmp_len = 0;
        tmp_i = 0;
        tmp = 0;
        p = 1;
    }

    return i;
}

/*
 * @brief 其它语言的 UTF-16 输出(按词换行 + 天城文/藏文组合字符)
 * @return 消耗掉的字节数
 *
 * @note 与 TextOutW_IndonesiaVietnam 同一套按词换行算法, 额外处理两类码位:
 *       天城文 0x0900~0x097F 与藏文 0x0F00~0x0FFF —— 这两类【不查代码页表】,
 *       直接用码位去 Unicode 扩展文件取模(GetUnicodeCharacterData)。
 * @note 藏文里的组合记号(附加在前一个字上, 自身不占宽度)被列举成一串常量:
 *       0x0F19/0x0F35/0x0F37/0x0F39/0x0F3E/0x0F3F/0x0F86/0x0F87/0x0FC6,
 *       以及两个区间 0x0F71~0x0F84 与 0x0F8D~0x0FBC。命中时 xpos 不前进、
 *       string_width 也不累加。
 */
u16 TextOutW_OtherLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width;
    u16 xpos = 0;
    u16 ypos = 0;
    u16 i;
    u16 tmp_len = 0;
    u16 tmp_i = 0;
    u8 tmp = 0;
    u8 jl = 0;
    u8 p = 1;

    info->string_width = 0;
    /* 加固: 同 TextOutW_IndonesiaVietnam, 原库不初始化 string_height。 */
    info->string_height = 0;
    info->bigendian &= 1;

    for (i = 0; (i + 1) < len; i += 2) {
        text = (str[i + 1 - info->bigendian] << 8) | str[i + info->bigendian];

        if ((text >= 0x80) && (text <= 0x2122)) {
            if (((u16)(text & 0xFF80) != 0x0900) && (str[i + 1 - info->bigendian] != 0x0F)) {
                text = ConvertUTF16toOtherLanguage(info, text);
            }
            if (text == 0) {
                text = '-';
            }
        } else {
            if (text > 0x2122) {
                text = '-';
            }
        }

        if (tmp) {
            if (jl || (text == ' ')) {
                tmp_i += 2;
                /* 加固: 天城文/藏文那一路原库写的是 `tmp_len =`(覆盖), 另一路是
                 * `tmp_len +=`(累加)。这里在算【一整个词】的总宽度以决定是否
                 * 换行重排, 覆盖显然是笔误 —— 一个词里混了组合字符时词宽会被
                 * 算成"最后一个字符的宽度", 换行位置随之出错。统一为累加。 */
                if (((u16)(text & 0xFF00) == 0x0F00) || ((u16)(text & 0xFF80) == 0x0900)) {
                    tmp_len += GetUnicodeCharacterData(info, text);
                } else {
                    tmp_len += GetOtherLanguageCharacterData(info, text);
                }
                if (jl && (text == ' ')) {
                    if (tmp_len > (x + 3 - xpos + info->text_width)) {
                        ypos += info->ascpixel.size * info->ratio;
                        xpos = x + 5;
                        i = i + 2 - tmp_i;
                        tmp = 0;
                        continue;
                    }
                    i = i - tmp_i;
                    tmp = 0;
                    p = 0;
                    continue;
                }
                jl = 1;
                continue;
            }
            jl = 0;
            continue;
        }

        if ((text == ' ') && (xpos > (x + 80)) && p) {
            i = i - 2;
            tmp = 1;
            jl = 0;
            continue;
        }

        if (((u16)(text & 0xFF00) == 0x0F00) || ((u16)(text & 0xFF80) == 0x0900)) {
            width = GetUnicodeCharacterData(info, text);
            switch (text) {
            case 0x0F19:
            case 0x0F35:
            case 0x0F37:
            case 0x0F39:
            case 0x0F3E:
            case 0x0F3F:
            case 0x0F86:
            case 0x0F87:
            case 0x0FC6:
                break;
            default:
                if (((text >= 0x0F71) && (text <= 0x0F84))
                    || ((text >= 0x0F8D) && (text <= 0x0FBC))) {
                    break;
                }
                xpos += info->ratio * width;
                info->string_width += info->ratio * width;
                break;
            }

            if (xpos > info->text_width) {
                ypos += info->ascpixel.size * info->ratio;
                xpos = 0;
            }
        } else {
            width = GetOtherLanguageCharacterData(info, text);
            xpos += info->ratio * width;
            info->string_width += info->ratio * width;
        }

        /* 加固: 原库这一处 putchar【没有判 FONT_SHOW_PIXEL】, 只判了 putchar
         * 非空 —— 其它 TextOut* 都判了。于是 font_textw_width() 那种"只量宽度
         * 不出像素"的模式在这一路会照样往屏上画。 */
        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info, info->ascpixel.pixelbuf, width, info->ascpixel.size,
                              xpos + x - info->ratio * width, ypos + y);
            }
        }

        /* 加固: 原库这一路【不设 string_height】(其它 TextOut* 都设),
         * 调用方拿到的是上一次调用留下的值。 */
        info->string_height = info->ascpixel.size + ypos;

        tmp_len = 0;
        tmp_i = 0;
        tmp = 0;
        p = 1;
    }

    return i;
}

/*
 * @brief 按 UTF-16 码位猜它属于哪个代码页
 * @return CP* 常量
 * @note 只在 TextOutW_AllLanguage 里用, 用于"一段文本里混多种语言"的场景。
 */
u8 ConvertUTF16tocodepage(u16 utf)
{
    if ((utf & 0xFF80) == 0x0E00) {
        return CP874;
    } else if ((utf >= 0x0601) && (utf <= 0x06FE)) {
        return CP1256;
    } else if ((utf >= 0x2000) && (utf <= 0x33FF)) {
        return CP937;
    } else if (((utf >= 0x4E00) && (utf <= 0x9FFF)) || (utf > 0xF8FF)) {
        return CP937;
    } else if ((utf >= 0xA000) && (utf <= 0xD7FF)) {
        return CPKSC;
    } else if (utf < 0x80) {
        return CP937;
    }

    return CP1258;
}


/*
 * @brief 一段文本里混排多种语言时的 UTF-16 输出
 * @return 消耗掉的字节数
 *
 * @note 与其它 TextOutW_* 最大的不同: 这里【逐字符】用 ConvertUTF16tocodepage()
 *       猜代码页, 临时改写 info->codepage 去取模, 用完再改回来。所以同一行里
 *       可以同时出现泰文、中文、韩文和西欧字符。
 * @note 泰语走 IsThaiOneWord_W / GetThaiLanguageCharacterData /
 *       ThaiLanguagecompose 三件套做组合字形拼装(元音/声调符号叠在辅音上下),
 *       w1/w2/w3 分别是主体字、上标、下标, 各自的点阵 buf 由
 *       GetThaiLanguageCharacterData 分配, 用完在本函数里 free。
 * @note 换行仍是那套"按词预扫描"算法(tmp/jl/p 三个标志), 与
 *       TextOutW_OtherLanguage 同源, 但断词符除了空格还认逗号和句点;
 *       并且预扫描中途遇到中日韩字符会直接放弃预扫描(见 CP937/CPKSC 那一支)。
 * @note 本函数在当前固件里是【死代码】, 见文件上方对三个泰语符号的说明。
 */
u16 TextOutW_AllLanguage(struct font_info *info, u8 *str, u16 len, u16 x, u16 y)
{
    u16 text;
    u16 width = 0;
    u16 height;
    u16 xpos;
    u16 ypos = 0;
    int i = 0;
    u8 ascii;
    u8 thai;
    u16 tmp_len = 0;
    u16 tmp_i = 0;
    u8 tmp = 0;
    u8 jl = 0;
    u8 p = 1;
    u16 pixel_size = 0;
    u8 codepage;
    u8 cp;
    u8 *pixel;
    THaiASCSTRUCT w1;
    THaiASCSTRUCT w2;
    THaiASCSTRUCT w3;

    info->string_width = 0;
    info->bigendian &= 1;
    xpos = x + 40;

    for (i = 0; (i + 1) < len; i += 2) {
        printf("info->bigendian == %d\n", info->bigendian);
        printf("str[%d] == 0x%x  str[%d] == 0x%x\n", i, str[i], i + 1, str[i + 1]);

        text = (str[i + 1 - info->bigendian] << 8) | str[i + info->bigendian];
        if (text == 0) {
            break;
        }

        codepage = info->codepage;
        cp = ConvertUTF16tocodepage(text);
        info->codepage = cp;

        switch (cp) {
        case CP874:
            pixel_size = info->ascpixel.size;
            memset(&w1, 0, sizeof(THaiASCSTRUCT));
            memset(&w2, 0, sizeof(THaiASCSTRUCT));
            memset(&w3, 0, sizeof(THaiASCSTRUCT));
            thai = IsThaiOneWord_W(str, len, &i, &w2, &w3, 1, info->bigendian);
            if (thai) {
                w1.unicode = text;
                w1.ansi = ConvertUTF16toOtherLanguage(info, text);
                w1.width = GetThaiLanguageCharacterData(info, w1.ansi, &w1.buf);
                w1.height = info->ascpixel.size;
                if (w2.unicode) {
                    w2.buf = NULL;
                    w2.ansi = ConvertUTF16toOtherLanguage(info, w2.unicode);
                    w2.width = GetThaiLanguageCharacterData(info, w2.ansi, &w2.buf);
                    w2.height = info->ascpixel.size;
                }
                if (w3.unicode) {
                    w3.buf = NULL;
                    w3.ansi = ConvertUTF16toOtherLanguage(info, w3.unicode);
                    w3.width = GetThaiLanguageCharacterData(info, w3.ansi, &w3.buf);
                    w3.height = info->ascpixel.size;
                }
                ThaiLanguagecompose(info, &width, &w1, &w2, &w3);
            } else {
                text = ConvertUTF16toOtherLanguage(info, text);
                width = GetThaiLanguageCharacterData(info, text, NULL);
            }
            if (w1.buf) {
                free(w1.buf);
                w1.buf = NULL;
            }
            if (w2.buf) {
                free(w2.buf);
                w2.buf = NULL;
            }
            if (w3.buf) {
                free(w3.buf);
                w3.buf = NULL;
            }
            ascii = 1;
            break;

        case CP937:
            pixel_size = info->pixel.size;
            if ((str[i + 1 - info->bigendian] == 0) && (str[i + info->bigendian] < 0x80)) {
                pixel_size = info->ascpixel.size;
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
                    text = '-';
                    width = GetASCIICharacterData(info, '-');
                    ascii = 1;
                }
            }
            break;

        case CPKSC:
            pixel_size = info->pixel.size;
            if ((str[i + 1 - info->bigendian] == 0) && (str[i + info->bigendian] < 0x80)) {
                if (str[i + info->bigendian] == '\r') {
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
                    text = '-';
                    width = GetASCIICharacterData(info, '-');
                    ascii = 1;
                }
            }
            break;

        case CP1258:
            switch (info->language_id) {
            case Russian:
                info->codepage = CP1251;
                break;
            case Turkey:
                info->codepage = CP1254;
                break;
            case Czech:
            case Hungarian:
            case Romanian:
            case Polish:
                info->codepage = CP1250;
                break;
            case Italian:
            case Dutch:
            case Portuguese:
            case French:
            case German:
            case Spanish:
            case Danish:
                info->codepage = CP1252;
                break;
            default:
                break;
            }
            pixel_size = info->ascpixel.size;
            if ((text >= 0x80) && (text <= 0x2122)) {
                text = ConvertUTF16toOtherLanguage(info, text);
            }
            if ((text == 0) || (text > 0x2122)) {
                text = '-';
            }
            width = GetOtherLanguageCharacterData(info, text);
            ascii = 1;
            break;

        default:
            if ((text >= 0x80) && (text <= 0x2122)) {
                text = ConvertUTF16toOtherLanguage(info, text);
            }
            if ((text == 0) || (text > 0x2122)) {
                text = '-';
            }
            width = GetOtherLanguageCharacterData(info, text);
            ascii = 1;
            break;
        }

        info->codepage = codepage;

        if (tmp) {
            if (jl || (text == ' ') || (text == ',') || (text == '.')) {
                if ((cp == CPKSC) || ((cp == CP937) && ((text == 0) || (text > 127)))) {
                    i -= 2;
                    tmp = 0;
                    continue;
                }
                tmp_i += 2;
                tmp_len += width;
                if (jl && ((text == ' ') || (text == ',') || (text == '.'))) {
                    if (tmp_len > (x + 8 - xpos + info->text_width)) {
                        ypos += info->ascpixel.size * info->ratio;
                        xpos = x + 5;
                        i = i + 2 - tmp_i;
                    } else {
                        i = i - tmp_i;
                        p = 0;
                    }
                    tmp = 0;
                } else {
                    jl = 1;
                }
            }
            continue;
        }

        if (((text == ' ') || (text == ',') || (text == '.'))
            && (xpos > (x + 80)) && p) {
            i -= 2;
            tmp = 1;
            jl = 0;
            continue;
        }

        xpos += info->ratio * width;
        info->string_width += info->ratio * width;

        if (xpos > (info->text_width + x)) {
            xpos = (x + 5) + info->ratio * width;
            ypos += info->ascpixel.size * info->ratio;
        }

        if (ascii) {
            height = info->ascpixel.size;
            pixel = info->ascpixel.pixelbuf;
        } else {
            height = info->pixel.size;
            pixel = info->pixel.pixelbuf;
        }

        if (info->flags & FONT_SHOW_PIXEL) {
            if (info->putchar) {
                info->putchar(info, pixel,
                              width, pixel_size,
                              xpos - info->ratio * width,
                              ypos + y + ((pixel_size > height) ? (u8)(pixel_size - height) : 0));
            }
        }

        tmp_len = 0;
        tmp_i = 0;
        tmp = 0;
        p = 1;
    }

    return i;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— 块查找命中后不 break。这条与第 2 条【叠加起来才是真隐患】,
 *                已加 break, 理由见第 2 条。
 *   [已修] 2 —— 不检查 malloc + 借 ascpixel.pixelbuf 当块索引表缓冲。
 *                命中时紧跟的 fread(pixelbuf, asc.size) 会【把索引表覆盖成点阵
 *                数据】, 而原库还要继续用 blocks[i] —— 拿点阵字节当块索引,
 *                可能再次"命中"、再次覆盖, 甚至 fseek 到任意位置。
 *                -> 加 break(命中即退, 覆盖之后不再读 blocks)、补 malloc 判空、
 *                   补 asc.size 上界检查(原库不校验就往 pixelbuf 里读)。
 *   [已修] 3 —— font_size 读出来没用过 -> (void) 显式标明。
 *   [已修] 4 —— TextOutW_OtherLanguage 的 putchar 不判 FONT_SHOW_PIXEL, 于是
 *                "只量宽度不出像素"的模式照样往屏上画 -> 已补判断。
 *   [已修] 5 —— 预扫描里天城文/藏文那一路把累加写成了覆盖(tmp_len = 而非 +=),
 *                一个词里混了组合字符时词宽算错、换行位置随之出错 -> 统一为累加。
 *   [保留] 6 —— InitFont_OtherLanguage_GBK 里未使用的局部变量 i。同 rle.c 的
 *                死变量 i: 删它对代码生成毫无影响, 只增加对照噪声。
 *   [已修] 7 —— 两个 TextOutW 都不设 string_height -> 开头置 0, 并在
 *                TextOutW_OtherLanguage 的循环里按 ascpixel.size + ypos 设置。
 *
 * 1) GetUnicodeCharacterData 的块查找循环【命中后不 break】, 会继续把剩下的
 *    块全扫一遍。码位只可能落在一个块里, 所以后面的扫描纯属浪费 —— 而且每块
 *    都要做两次 fseek + 两次 fread 的判定开销。参考 IR 里 if.then26 直接回到
 *    for.inc(而不是跳出循环), 确认原库就是这样。
 *
 * 2) GetUnicodeCharacterData 【不检查 malloc 返回值】就 fread 进去。
 *    而且它把块索引表借 info->ascpixel.pixelbuf 当缓冲, 与点阵数据共用同一块
 *    内存 —— 紧接着的 `font_sd_fread(fd_uni, info->ascpixel.pixelbuf, asc.size)`
 *    就把索引表覆盖了。因为循环还要继续用 blocks[i](见第 1 条), 命中之后
 *    后续几轮读到的 begin/end/addr 已经是点阵数据的字节了。
 *    这是"第 1 条"与"借缓冲"两个设计叠加出来的真实隐患。
 *
 * 3) GetUnicodeCharacterData 读出的 font_size 从未使用。
 *
 * 4) TextOutW_OtherLanguage 里的 putchar 调用【没有判 FONT_SHOW_PIXEL】,
 *    只判了 putchar 非空。也就是说 font_textw_width() 那种"只量宽度不出像素"
 *    的模式在这一路会照样往屏上画。参考 IR 里 if.end287 直接 load putchar 并
 *    判空, 没有 `and flags, 2` 的检查(其它 TextOut* 都有), 确认是原库如此。
 *
 * 5) TextOutW_OtherLanguage 的预扫描分支里, 天城文/藏文那一路是
 *    `tmp_len = GetUnicodeCharacterData(...)`(覆盖), 而另一路是
 *    `tmp_len += GetOtherLanguageCharacterData(...)`(累加)。一个词里混了组合
 *    字符时词宽会被算错。参考 IR 的 phi(if.then80 给 conv82, if.else83 给
 *    add87)确认原库如此。
 *
 * 6) InitFont_OtherLanguage_GBK 与 InitFont_OtherLanguage 里的局部变量 i
 *    在前者中从未使用(DWARF 里确有)。
 *
 * 7) TextOutW_IndonesiaVietnam / TextOutW_OtherLanguage 都【不设置
 *    info->string_height】(其它 TextOut* 都设), 调用方拿到的是上一次的值。
 */
