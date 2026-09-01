/*
 * font_textout.c —— font.a 的对外出口: 打开/关闭字库、文本宽度、内码/UTF-16/UTF-8
 *                   三种编码的显示入口, 以及编码互转
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_textout.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_textout.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_textout.c
 *
 * 【还原依据】函数原始行号(DISubprogram), 本文件按此顺序排列:
 *     font_set_offset_table@78   other_language@84        find_language_by_id@128
 *     font_open@152              font_text_width@243      font_textw_width@270
 *     font_textu_width@299       font_textout@337         font_textout_unicode@418
 *     __utf8_to_utf16@519        __utf16_to_utf8@624      font_textout_utf8@690
 *     font_close@713             __utf16toansi@748        font_utf16toansi@802
 *     font_utf8toutf16@842       font_utf8toansi@851
 *   其中 other_language / find_language_by_id / __utf16_to_utf8 / __utf16toansi
 *   四个是 static, 在原厂构建里已被全部内联(IR 里没有独立 define), 但 DWARF 仍
 *   保留了它们的签名与局部变量名, 所以能按原样还原成 static 函数再让本地 clang
 *   同样内联掉。四者的签名(取自 DWARF DISubroutineType):
 *     u8   other_language(struct font_info *info)
 *     struct font_info *find_language_by_id(u8 language)
 *     u16  __utf16_to_utf8(struct font_info *info, u8 *utf16, u16 utf16_len, u8 *utf8)
 *     u16  __utf16toansi(struct font_info *info, u16 utf)
 *   局部变量名同样取自 DWARF(putf16/putf8/utf16/utf16_len/low/high/wchar/
 *   _utf16/_ansi/cnt/ansilen/utf16buf ...)。
 *
 * 【段属性】原库代码在 .font_textout.text, font_info_table 在
 *   .font_textout.text.const, lange_info_table 与 f_info 在 .font_textout.data
 *   (见 ref IR 的 section 属性)。注意原厂 IR 里【字符串字面量没有 section】,
 *   所以 const_seg 只作用到 font_info_table 上, 字符串不受影响 —— 这与
 *   #pragma const_seg 的实际行为一致(它不管匿名字符串字面量)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".font_textout.data")
#pragma data_seg(".font_textout.data")
#pragma const_seg(".font_textout.text.const")
#pragma code_seg(".font_textout.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "font/font_textout.h"
#include "font/language_list.h"
#include "jl_res_config.h"   /* FONT_PATH: 字库路径不再硬编码, 见 font_info_table */
#include "jl_debug.h"    /* printf / puts: 原厂靠别处间接带入 */

extern void platform_putchar(struct font_info *info, u8 *pixel, u16 width, u16 height,
                             u16 x, u16 y);

extern u8 InitFont_GBK(struct font_info *info);
extern u8 InitFont_BIG5(struct font_info *info);
extern u8 InitFont_SJIS(struct font_info *info);
extern u8 InitFont_KSC(struct font_info *info);
extern u8 InitFont_OtherLanguage(struct font_info *info);

extern u16 TextOut_GBK(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOut_BIG5(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOut_SJIS(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOut_KSC(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOut_OtherLanguage(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);

extern u16 TextOutW_GBK(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOutW_BIG5(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOutW_SJIS(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOutW_KSC(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOutW_AllLanguage(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);
extern u16 TextOutW_OtherLanguage(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y);

extern u16 ConvertUTF16toGB2312(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toGBK(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toBIG5(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toSJIS(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toKSC(struct font_info *info, u16 utf);
extern u16 ConvertUTF16toOtherLanguage(struct font_info *info, u16 utf);

const LANG_TABLE *lange_info_table = NULL;

/*
 * 字库文件路径。
 *
 * 加固: 原厂这里是【完全硬编码】的 "flash/res/font/F_XXX.PIX", 绕过了
 * ui/res_config.h 的 FONT_PATH 宏 —— 换平台后资源根目录一变, 这一处就对不上,
 * 表现为 "打不开 flash/res/font/F_ASCII.PIX (FR_NO_PATH)" + InitFont failed,
 * 而 .res/.str/.sty 却都能正常打开(它们走的是 RES_PATH 宏), 很容易看漏。
 * 现在统一走 FONT_PATH, 只在 port/ui_port_config.h 一处配置。
 */
const struct font_info font_info_table[] = {
    {
        .ascpixel    = { .file = { .name = FONT_PATH"F_ASCII.PIX", }, },
        .pixel       = { .file = { .name = FONT_PATH"F_GB2312.PIX", }, },
        .tabfile     = { .name = FONT_PATH"F_GB2312.TAB", },
        .language_id = Chinese_Simplified,
        .isgb2312    = 1,
        .flags       = FONT_DEFAULT | FONT_SHOW_MULTI_LINE,
        .putchar     = platform_putchar,
    },
    {
        .ascpixel    = { .file = { .name = FONT_PATH"F_CP1252.PIX", }, },
        .tabfile     = { .name = FONT_PATH"F_CP1252.TAB", },
        .language_id = Swedish,
        .flags       = FONT_DEFAULT | FONT_SHOW_MULTI_LINE,
        .putchar     = platform_putchar,
    },
    { 0 },
};

static struct font_info f_info;

int font_set_offset_table(const LANG_TABLE *table)
{
    lange_info_table = table;

    return 0;
}

/*
 * @brief 是否属于"其它语言"(即不走 GBK/BIG5/SJIS/KSC 这四套内码字库的语言)
 * @return 1 = 其它语言
 * @note 原厂构建已把本函数完全内联并把条件折叠成 `language_id > Korean`
 *       (参考 IR 里就是一条 `icmp ugt i8 %x, 4`)。language_id 为 0 表示
 *       "未打开", 也不算其它语言, 所以 `> Korean` 这个写法同时覆盖了 0。
 */
static u8 other_language(struct font_info *info)
{
    if (info->language_id > Korean) {
        return 1;
    }

    return 0;
}

/*
 * @brief 按语言 id 在 font_info_table 里找一份配置, 拷进 f_info
 * @return &f_info; NULL = 表里没有这个语言
 */
static struct font_info *find_language_by_id(u8 language)
{
    struct font_info *p;

    if (f_info.language_id == language) {
        return &f_info;
    }

    for (p = (struct font_info *)font_info_table; p->language_id != 0; p++) {
        if (p->language_id == language) {
            memcpy(&f_info, p, sizeof(struct font_info));
            return &f_info;
        }
    }

    return NULL;
}

/*
 * @brief 打开字库
 * @param info 传 NULL 表示"按 language 从 font_info_table 里挑一份"; 传 &f_info
 *             表示"重开当前这份"(会先 font_close)
 * @return 打开后的 info; NULL = 表里没有这个语言
 *
 * @note 这里有两轮 switch: 第一轮只在 lange_info_table 非空时跑, 负责按语言设
 *       codepage 并初始化字库; 第二轮是无条件的兜底初始化。第一轮命中时会
 *       goto __open 跳过第二轮 —— 参考 IR 里 sw.bb / sw.bb12 直接跳到 __open。
 */
struct font_info *font_open(struct font_info *info, u8 language)
{
    /* 加固: 原库这里【不初始化】。下面 default 分支在 other_language() 为假时
     * 根本不给 ret 赋值, 而 __open 之后又要用它 —— 读未初始化的局部变量是 UB。 */
    bool ret = 0;

    if (info == &f_info) {
        font_close(info);
    } else if (info == NULL) {
        info = find_language_by_id(language);
        if (info == NULL) {
            return NULL;
        }
    }

    printf("language:%d\n", language);

    info->ratio = 1;
    info->sta = 0;
    info->codepage = 0;

    if (lange_info_table) {
        switch (info->language_id) {
        case Chinese_Simplified:
        case Chinese_Traditional:
            info->codepage = CP937;
            ret = InitFont_GBK(info);
            goto __open;
        case Korean:
            info->codepage = CPKSC;
            ret = InitFont_KSC(info);
            goto __open;
        default:
            if (other_language(info)) {
                ret = InitFont_OtherLanguage(info);
                printf(">>>>> InitFont_OtherLanguage\n");
            }
            break;
        }
    }

    switch (info->language_id) {
    case Chinese_Simplified:
        ret = InitFont_GBK(info);
        goto __open;
    case Chinese_Traditional:
        ret = InitFont_BIG5(info);
        goto __open;
    case Japanese:
        ret = InitFont_SJIS(info);
        goto __open;
    case Korean:
        ret = InitFont_KSC(info);
        goto __open;
    default:
        if (other_language(info)) {
            printf("InitFont_OtherLanguage!\n");
            ret = InitFont_OtherLanguage(info);
        }
        break;
    }

__open:
    /*
     * 加固: 原库把 InitFont_* 的返回值收进 ret 却【从不检查】。字库文件缺失时
     * InitFont_* 返回 0, font_open 照样返回非 NULL, 要到后面 font_textout 里
     * 判 ascpixel.file.fd 才失败 —— 失败点离原因很远, 很难查。
     *
     * 【故意不改返回值】: 改成失败就 return NULL 会让不判空的调用方直接崩,
     * 那是行为变更而不是加固。这里只补一条日志; 具体的错误位 InitFont_*
     * 内部已经写进 info->sta(FT_ERROR_NOASCPIXFILE / NOPIXFILE / NOTABFILE)。
     */
    if (!ret) {
        printf("font_open: InitFont failed, language = %d, sta = 0x%x\n", language, info->sta);
    }

    if (info->pixel.nbytes) {
        info->pixel.pixelbuf = malloc(info->pixel.nbytes);
        if (info->pixel.pixelbuf == NULL) {
            info->sta |= FT_ERROR_NOMEM;
        }
    }

    if (info->ascpixel.nbytes) {
        info->ascpixel.pixelbuf = malloc(info->ascpixel.nbytes * 2);
        if (info->ascpixel.pixelbuf == NULL) {
            info->sta |= FT_ERROR_NOMEM;
        }
    }

    return info;
}

/*
 * @brief 取一段内码文本的显示宽度
 * @note 手法是"把 flags 临时切成只算宽度不出像素, 调一次 textout, 再把 flags
 *       还原", 所以三个 *_width 函数的骨架完全相同, 只差调哪个 textout。
 *       还原时注意 else 分支清的是 (FONT_GET_WIDTH | FONT_SHOW_MULTI_LINE)
 *       而不是只清 FONT_SHOW_MULTI_LINE —— 参考 IR 是 `and -6`(即 ~5)。
 */
u16 font_text_width(struct font_info *info, u8 *str, u16 strlen)
{
    u16 len;
    u32 flags = info->flags;

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= FONT_GET_WIDTH;
    info->string_width = 0;

    len = font_textout(info, str, strlen, 0, 0);

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= (flags & FONT_SHOW_PIXEL);
    if (flags & FONT_SHOW_MULTI_LINE) {
        info->flags |= FONT_SHOW_MULTI_LINE;
    } else {
        info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_MULTI_LINE);
    }

    return info->string_width;
}

u16 font_textw_width(struct font_info *info, u8 *str, u16 strlen)
{
    u16 len;
    u32 flags = info->flags;

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= FONT_GET_WIDTH;
    info->string_width = 0;

    len = font_textout_unicode(info, str, strlen, 0, 0);

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= (flags & FONT_SHOW_PIXEL);
    if (flags & FONT_SHOW_MULTI_LINE) {
        info->flags |= FONT_SHOW_MULTI_LINE;
    } else {
        info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_MULTI_LINE);
    }

    return info->string_width;
}

/*
 * @note 与另外两个 *_width 的唯一差别: 这里还额外清了 string_height。
 */
u16 font_textu_width(struct font_info *info, u8 *str, u16 strlen)
{
    u16 len;
    u32 flags = info->flags;

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= FONT_GET_WIDTH;
    info->string_width = 0;
    info->string_height = 0;

    len = font_textout_utf8(info, str, strlen, 0, 0);

    info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_PIXEL);
    info->flags |= (flags & FONT_SHOW_PIXEL);
    if (flags & FONT_SHOW_MULTI_LINE) {
        info->flags |= FONT_SHOW_MULTI_LINE;
    } else {
        info->flags &= ~(FONT_GET_WIDTH | FONT_SHOW_MULTI_LINE);
    }

    return info->string_width;
}

/*
 * @brief 内码(ANSI)文本显示
 * @note lange_info_table 非空时, 简体/繁体都走 GBK —— 参考 IR 把 case 1 与
 *       case 2 合并成了一次 `(id-1) <u 2` 的范围判断。
 */
u16 font_textout(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y)
{
    u16 len;

    if (info->ascpixel.file.fd == NULL) {
        return 0;
    }

    if (lange_info_table) {
        switch (info->language_id) {
        case Chinese_Simplified:
        case Chinese_Traditional:
            return TextOut_GBK(info, str, strlen, x, y);
        default:
            break;
        }
    }

    switch (info->language_id) {
    case Chinese_Simplified:
        len = TextOut_GBK(info, str, strlen, x, y);
        break;
    case Chinese_Traditional:
        len = TextOut_BIG5(info, str, strlen, x, y);
        break;
    case Japanese:
        len = TextOut_SJIS(info, str, strlen, x, y);
        break;
    case Korean:
        len = TextOut_KSC(info, str, strlen, x, y);
        break;
    default:
        if (other_language(info)) {
            len = TextOut_OtherLanguage(info, str, strlen, x, y);
        } else {
            len = 0;
        }
        break;
    }

    return len;
}

/*
 * @brief UTF-16 文本显示
 * @note 两轮 switch 的语言分派【不一样】: lange_info_table 非空时 English(5)
 *       会临时把 codepage 切成 CP937 走 GBK; 其它语言走 TextOutW_AllLanguage。
 *       为空时才是常规的 GBK/BIG5/SJIS/KSC/OtherLanguage 分派。
 */
u16 font_textout_unicode(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y)
{
    u16 len;
    u8 codepage;

    if (info->ascpixel.file.fd == NULL) {
        return 0;
    }

    if (lange_info_table) {
        printf("language_id: %d", info->language_id);
        switch (info->language_id) {
        case Chinese_Simplified:
            len = TextOutW_GBK(info, str, strlen, x, y);
            break;
        case Chinese_Traditional:
            printf("coming in Chinese_Traditional!!\n");
            len = TextOutW_BIG5(info, str, strlen, x, y);
            break;
        case English:
            codepage = info->codepage;
            info->codepage = CP937;
            len = TextOutW_GBK(info, str, strlen, x, y);
            info->codepage = codepage;
            break;
        case Japanese:
            printf("coming in Japanese!!!\n");
            len = TextOutW_SJIS(info, str, strlen, x, y);
            break;
        case Korean:
            len = TextOutW_KSC(info, str, strlen, x, y);
            break;
        default:
            if (other_language(info)) {
                printf("coming in AllLanguage!!\n");
                len = TextOutW_AllLanguage(info, str, strlen, x, y);
            } else {
                len = 0;
            }
            break;
        }

        return len;
    }

    printf("language_id == %d", info->language_id);
    switch (info->language_id) {
    case Chinese_Simplified:
        printf("comming in Chinese_Simplified");
        len = TextOutW_GBK(info, str, strlen, x, y);
        break;
    case Chinese_Traditional:
        len = TextOutW_BIG5(info, str, strlen, x, y);
        break;
    case Japanese:
        len = TextOutW_SJIS(info, str, strlen, x, y);
        break;
    case Korean:
        len = TextOutW_KSC(info, str, strlen, x, y);
        break;
    default:
        if (other_language(info)) {
            printf("coming in Other_Language!!\n");
            len = TextOutW_OtherLanguage(info, str, strlen, x, y);
        } else {
            len = 0;
        }
        break;
    }

    return len;
}

/*
 * @brief UTF-8 转 UTF-16
 * @param utf16_buf 可传 NULL —— 此时只统计需要的字节数, 不写出。
 * @return 写出(或需要)的字节数, 每个字符固定 2 字节
 *
 * @note 4 字节的 UTF-8(即 BMP 之外的码位)统一写成 0xFFFF, 因为输出是 UTF-16
 *       而这里不做代理对。
 * @note 字节序由 info->bigendian 决定, 且【每个字符都重新读一次】这个字段 ——
 *       参考 IR 里该 load 在循环体内, 没有被提出去, 还原时不要顺手缓存。
 */
static u16 __utf8_to_utf16(struct font_info *info, u8 *utf8_buf, u16 utf8_len, u16 *utf16_buf)
{
    u16 *putf16 = utf16_buf;
    u8 *putf8 = utf8_buf;
    u16 utf16;
    u16 utf16_len = 0;

    while (utf8_len) {
        if ((u8)(*putf8 & 0xF8) == 0xF0) {
            if (utf8_len < 4) {
                break;
            }
            if (putf16) {
                ((u8 *)putf16)[0] = 0xFF;
                ((u8 *)putf16)[1] = 0xFF;
            }
            putf8 += 4;
            utf8_len -= 4;
        } else if ((u8)(*putf8 & 0xF0) == 0xE0) {
            if (utf8_len < 3) {
                break;
            }
            if (putf16) {
                utf16 = (((u32)putf8[0] << 12) & 0xF000) | ((u32)(putf8[1] & 0x3F) << 6)
                        | (putf8[2] & 0x3F);
                if (info->bigendian) {
                    ((u8 *)putf16)[0] = utf16 >> 8;
                    ((u8 *)putf16)[1] = utf16;
                } else {
                    ((u8 *)putf16)[0] = utf16;
                    ((u8 *)putf16)[1] = utf16 >> 8;
                }
            }
            putf8 += 3;
            utf8_len -= 3;
        } else if ((u8)(*putf8 & 0xE0) == 0xC0) {
            if (utf8_len < 2) {
                break;
            }
            if (putf16) {
                utf16 = ((u32)(u8)(putf8[0] & 0x1F) << 6) | (putf8[1] & 0x3F);
                if (info->bigendian) {
                    ((u8 *)putf16)[0] = utf16 >> 8;
                    ((u8 *)putf16)[1] = utf16;
                } else {
                    ((u8 *)putf16)[0] = utf16;
                    ((u8 *)putf16)[1] = utf16 >> 8;
                }
            }
            putf8 += 2;
            utf8_len -= 2;
        } else if (*putf8 < 0x80) {
            if (putf16) {
                utf16 = *putf8;
                if (info->bigendian) {
                    ((u8 *)putf16)[0] = 0;
                    ((u8 *)putf16)[1] = utf16;
                } else {
                    ((u8 *)putf16)[0] = utf16;
                    ((u8 *)putf16)[1] = 0;
                }
            }
            putf8 += 1;
            utf8_len -= 1;
        } else {
            printf("utf8 err!\n");
            break;
        }

        if (putf16) {
            putf16++;
        }
        utf16_len += 2;
    }

    return utf16_len;
}

/*
 * @brief 统计一段 UTF-16 转成 UTF-8 需要多少字节
 * @param utf8 输出缓冲
 * @return 需要的 UTF-8 字节数
 *
 * @note ⚠️ 本函数的【写出部分无法从 IR 还原】: 全库只有 font_textout_utf8
 *       一处调用它, 且传的 utf8 是 NULL, 于是原厂构建把所有写出代码都优化掉了,
 *       参考 IR 里这段内联体一条 store 都没有。所以这里只还原了统计逻辑
 *       (它是 IR 里实际存在的全部内容), 保持与原厂固件逐指令等价。
 *       如果将来要真的用它做转换, 必须重新实现写出部分 —— 见文末 TODO。
 * @note wchar == 0xFFFF 算 4 字节, 是与 __utf8_to_utf16 里"4 字节 UTF-8 写成
 *       0xFFFF"配对的反向约定。
 */
static u16 __utf16_to_utf8(struct font_info *info, u8 *utf16, u16 utf16_len, u8 *utf8)
{
    u16 len = 0;
    u8 high;
    u8 low;
    u16 wchar;

    while (utf16_len) {
        high = utf16[0];
        low  = utf16[1];
        if (info->bigendian == 0) {
            high = utf16[1];
            low  = utf16[0];
        }
        wchar = (high << 8) | low;

        if (wchar < 0x80) {
            len += 1;
        } else if (wchar < 0x800) {
            len += 2;
        } else if (wchar == 0xFFFF) {
            len += 4;
        } else {
            len += 3;
        }

        utf16 += 2;
        utf16_len -= 2;
    }

    return len;
}

/*
 * @brief UTF-8 文本显示
 * @note 内部先转成 UTF-16 再走 font_textout_unicode, 然后把"实际显示掉的
 *       UTF-16 长度"再折算回 UTF-8 字节数返回, 这样调用方能知道吃掉了多少输入。
 */
u16 font_textout_utf8(struct font_info *info, u8 *str, u16 strlen, u16 x, u16 y)
{
    u16 len = 0;
    u16 utf16_len;
    u8 *utf16;

    /* 加固: 原库不检查 strlen 是否为 0 —— malloc(0) 的返回值是实现相关的,
     * 可能返回 NULL(那样整段被跳过, 恰好没事), 也可能返回一个不该解引用的
     * 非空指针, 于是拿它去做 UTF-8 转换。顺带补上 str 判空。 */
    if (str == NULL || strlen == 0) {
        return 0;
    }

    utf16 = malloc(strlen * 2);
    if (utf16) {
        info->bigendian = 1;
        utf16_len = __utf8_to_utf16(info, str, strlen, (u16 *)utf16);
        utf16_len = font_textout_unicode(info, utf16, utf16_len, x, y);
        len = __utf16_to_utf8(info, utf16, utf16_len, NULL);
        if (len == 0xFFFF) {
            len = 0;
        }
        free(utf16);
    }

    return len;
}

void font_close(struct font_info *info)
{
    if (info->language_id == 0) {
        return;
    }

    if (info->pixel.pixelbuf) {
        free(info->pixel.pixelbuf);
        info->pixel.pixelbuf = NULL;
    }

    if (info->ascpixel.pixelbuf) {
        free(info->ascpixel.pixelbuf);
        info->ascpixel.pixelbuf = NULL;
    }

    if (info->pixel.file.fd) {
        font_sd_fclose(info->pixel.file.fd);
        info->pixel.file.fd = NULL;
    }

    if (info->tabfile.fd) {
        font_sd_fclose(info->tabfile.fd);
        info->tabfile.fd = NULL;
    }

    if (info->ascpixel.file.fd) {
        font_sd_fclose(info->ascpixel.file.fd);
        info->ascpixel.file.fd = NULL;
    }

    info->language_id = 0;
}

/*
 * @brief 单个 UTF-16 码位转内码
 * @return 内码; '-' 表示该字库查不到这个字; 0 表示没有 TAB 文件或超出范围
 * @note 其它语言的有效区间是 [0x100, 0x2122](参考 IR 折叠成
 *       `(utf - 0x100) <u 8227`)。超过 0x2122 返回 '-', 低于 0x100 返回 0。
 */
static u16 __utf16toansi(struct font_info *info, u16 utf)
{
    u16 ret;

    if (info->tabfile.fd == NULL) {
        return 0;
    }

    switch (info->language_id) {
    case Chinese_Simplified:
        if (info->isgb2312) {
            ret = ConvertUTF16toGB2312(info, utf);
        } else {
            ret = ConvertUTF16toGBK(info, utf);
        }
        if (ret == 0) {
            return '-';
        }
        break;
    case Chinese_Traditional:
        ret = ConvertUTF16toBIG5(info, utf);
        if (ret == 0) {
            return '-';
        }
        break;
    case Japanese:
        ret = ConvertUTF16toSJIS(info, utf);
        if (ret == 0) {
            return '-';
        }
        break;
    case Korean:
        ret = ConvertUTF16toKSC(info, utf);
        if (ret == 0) {
            return '-';
        }
        break;
    default:
        if ((utf >= 0x100) && (utf <= 0x2122)) {
            ret = ConvertUTF16toOtherLanguage(info, utf);
            if (ret == 0) {
                return '-';
            }
        } else if (utf > 0x2122) {
            return '-';
        } else {
            return 0;
        }
        break;
    }

    return ret;
}

/*
 * @brief UTF-16 串转内码串
 * @param ansi 可传 NULL —— 此时只统计需要的字节数, 不写出
 * @return 内码字节数
 * @note 字节序由 info->bigendian 决定: 低字节取 utf[i + bigendian],
 *       高字节取 utf[i + 1 - bigendian]。
 */
u16 font_utf16toansi(struct font_info *info, u8 *utf, u16 len, u8 *ansi)
{
    u16 i;
    u16 _utf16;
    u16 _ansi;
    u8 *p = ansi;
    u16 cnt = 0;

    for (i = 0; (i + 1) < len; i += 2) {
        _utf16 = (utf[i + 1 - info->bigendian] << 8) | utf[i + info->bigendian];
        if (_utf16 == 0) {
            break;
        }

        if (_utf16 > 128) {
            _ansi = __utf16toansi(info, _utf16);
            if (_ansi < 128) {
                if (ansi) {
                    *p++ = _ansi;
                }
                cnt += 1;
            } else {
                if (ansi) {
                    *p++ = _ansi >> 8;
                    *p++ = _ansi;
                }
                cnt += 2;
            }
        } else {
            if (ansi) {
                *p++ = utf[i + info->bigendian];
            }
            cnt += 1;
        }
    }

    return cnt;
}

u16 font_utf8toutf16(struct font_info *info, u8 *utf8, u16 utf8len, u16 *utf16)
{
    u16 utf16len = __utf8_to_utf16(info, utf8, utf8len, utf16);

    return utf16len;
}

/*
 * @note 先用 utf16_buf = NULL 空跑一遍拿长度, 再 malloc 真正转一遍。
 */
u16 font_utf8toansi(struct font_info *info, u8 *utf8, u16 utf8len, u8 *ansi)
{
    u16 utf16len;
    u16 *utf16buf;
    u16 ansilen;

    utf16len = __utf8_to_utf16(info, utf8, utf8len, NULL);

    utf16buf = malloc(utf16len);
    if (utf16buf == NULL) {
        return 0;
    }

    __utf8_to_utf16(info, utf8, utf8len, utf16buf);
    ansilen = font_utf16toansi(info, (u8 *)utf16buf, utf16len, ansi);

    free(utf16buf);

    return ansilen;
}

/*
 * 原库缺陷/限制清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样
 * 保留; 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [保留] 1 —— __utf16_to_utf8 的【写出部分在原厂固件里根本不存在】(全库只有
 *                一处调用且传 utf8 = NULL, 原厂构建把写出代码整段优化掉了)。
 *                本文件只还原了 IR 里实际存在的统计逻辑。补上写出部分属于
 *                【实现新功能】而不是修缺陷, 而且那一段【不受 verify.sh 保护】
 *                —— 原厂没有可比对的机器码。结论不变: 该函数当前只能测长度。
 *   [已修] 2 —— font_open 把 InitFont_* 的返回值收进 ret 却从不检查; 更要命的是
 *                default 分支在 other_language() 为假时【根本不给 ret 赋值】,
 *                读未初始化的局部变量是 UB。-> ret 已初始化, 失败时补一条日志。
 *                【故意不改返回值】: 改成失败就 return NULL 会让不判空的调用方
 *                直接崩, 那是行为变更; 具体错误位 InitFont_* 已写进 info->sta。
 *   [保留] 3 —— font_open 按 nbytes*2 分配, 而 font_ascii.c 按 nbytes 判上限,
 *                两边对 nbytes 的理解不一致, 靠这 2 倍余量兜着。要真正对齐得
 *                统一"nbytes 指字高还是字宽"的语义, 牵动 font_* 全家, 而且现有
 *                字库都是按当前语义打包的 —— 属格式约定问题, 不是能就地改的。
 *   [保留] 4 —— 原注释即"这条只是记录, 不需要改"(malloc 返回值的对齐足够)。
 *   [已修] 5 —— font_textout_utf8 的 malloc(strlen*2) 不检查 strlen 是否为 0
 *                -> 已补 str / strlen 判断。至于"未检查 __utf8_to_utf16 是否
 *                提前退出": 出错时显示已转换的部分是【合理降级】, 比整串不显示
 *                好, 故保持原样。
 *
 * 1) __utf16_to_utf8 的【写出部分在原厂固件里根本不存在】。全库只有
 *    font_textout_utf8 一处调用它并且传 utf8 = NULL, 所以原厂构建把写出代码
 *    整段优化掉了(参考 IR 的内联体里一条 store 都没有)。本文件只还原了 IR 里
 *    实际存在的统计逻辑。**结论: 这个函数当前只能用来测长度, 不能用来转换。**
 *    如果将来要用它做真正的 UTF-16→UTF-8 转换, 必须重新实现写出部分, 并且
 *    知道这一段【不受 verify.sh 保护】(原厂没有可比对的机器码)。
 *
 * 2) font_open 收集了 InitFont_* 的返回值到局部变量 ret, 但【从不检查】。
 *    字库文件缺失时 InitFont_* 返回 0, font_open 依然返回非 NULL,
 *    要到后面 font_textout 里判 ascpixel.file.fd 才失败。
 *
 * 3) font_open 里 `info->ascpixel.pixelbuf = malloc(info->ascpixel.nbytes * 2)`
 *    分配的是 2 倍, 而 font_ascii.c 的 GetASCIICharacterData 用
 *    info->ascpixel.nbytes 做上限判断 —— 两边对"nbytes 到底指多少字节"的理解
 *    不一致。当前靠这 2 倍余量掩盖了 font_ascii.c 里那个按 width 算、按字高比
 *    的错位(见 font_ascii.c 的 TODO), 属于"用余量兜 bug"。
 *
 * 4) font_utf8toansi 里 `malloc(utf16len)` 分配的是【UTF-16 的字节数】,
 *    而 __utf8_to_utf16 每个字符写 2 字节、返回的就是字节数, 所以这里是对的;
 *    但紧接着把它当 u16* 用而没有对齐保证(malloc 返回 8 字节对齐, 实际安全)。
 *    这条只是记录, 不需要改。
 *
 * 5) font_textout_utf8 的 `malloc(strlen * 2)` 没有检查 strlen 是否为 0,
 *    malloc(0) 的返回值实现相关; 且未检查 __utf8_to_utf16 是否因 "utf8 err!"
 *    提前退出, 出错时仍会照常显示已转换的部分。
 */
