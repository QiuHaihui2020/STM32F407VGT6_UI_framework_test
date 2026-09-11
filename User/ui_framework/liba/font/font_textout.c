/*
 * font_textout.c —— 字库模块的对外出口: 打开/关闭字库、文本宽度、
 *                   内码 / UTF-16 / UTF-8 三种编码的显示入口, 以及编码互转
 *
 * 【四个 static 是内部实现】other_language / find_language_by_id /
 *   __utf16_to_utf8 / __utf16toansi 只在本文件内用, 编译器一般会把它们内联掉。
 *
 * 【段属性】代码放在 .font_textout.text, font_info_table 在
 *   .font_textout.text.const, lange_info_table 与 f_info 在 .font_textout.data。
 *   注意 #pragma const_seg 不管匿名字符串字面量, 所以它只作用到
 *   font_info_table 上。
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
#include "jl_debug.h"    /* printf / puts: 显式包含, 保证本文件自包含 */

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
 * 路径【一律经 FONT_PATH 宏拼】, 不要在这里写死目录: 写死的话换平台、改资源
 * 根目录时这一处就对不上, 表现为"打不开 .../F_ASCII.PIX" + InitFont failed,
 * 而 .res/.str/.sty 却都能正常打开(它们走 RES_PATH), 很容易看漏。
 * FONT_PATH 只在 config/ui_port_config.h 一处配置。
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
 * @note 判断就是 `language_id > Korean`: language_id 为 0 表示"未打开",
 *       它也不算其它语言, 所以这个写法同时覆盖了 0。
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
 *       codepage 并初始化字库; 第二轮是无条件的兜底初始化。第一轮命中时
 *       goto __open 跳过第二轮。
 */
struct font_info *font_open(struct font_info *info, u8 language)
{
    /* ret 必须有初值: 下面两个 default 分支在 other_language() 为假时不会给它
     * 赋值, 而 __open 之后要用它 —— 读未初始化的局部变量是 UB。 */
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
     * 字库打不开时【仍然返回非 NULL】, 只打一条日志。
     *
     * 这是有意的: 返回 NULL 会让不判空的调用方直接崩。具体的错误位
     * InitFont_* 内部已经写进 info->sta(FT_ERROR_NOASCPIXFILE / NOPIXFILE /
     * NOTABFILE), 调用方要区分原因就看它。
     * 没有这条日志的话, 失败要到后面 font_textout 判 ascpixel.file.fd 才暴露,
     * 离原因很远、很难查。
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
 *       恢复", 所以三个 *_width 函数的骨架完全相同, 只差调哪个 textout。
 *       注意 else 分支要清掉 (FONT_GET_WIDTH | FONT_SHOW_MULTI_LINE) 两位,
 *       不是只清 FONT_SHOW_MULTI_LINE。
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
 * @note lange_info_table 非空时, 简体/繁体都走 GBK(两个 case 合并)。
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
 *       调用过程中它可能被改, 所以不要提到循环外缓存。
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
 * @note ⚠️ 本函数【只统计长度, 不做写出】—— 形参 utf8 目前没有被使用。
 *       全模块只有 font_textout_utf8 一处调用它并且传 NULL, 所以写出部分一直
 *       没有实现。要真的用它做 UTF-16 -> UTF-8 转换, 得先把写出补上,
 *       见文末注意事项。
 * @note wchar == 0xFFFF 算 4 字节, 与 __utf8_to_utf16 里"4 字节 UTF-8 写成
 *       0xFFFF"是配对的反向约定。
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

    /* strlen 为 0 时直接返回: malloc(0) 的返回值是实现相关的, 可能给 NULL,
     * 也可能给一个不该解引用的非空指针。str 一并判空。 */
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
 * @note 其它语言的有效区间是 [0x100, 0x2122]: 超过 0x2122 返回 '-',
 *       低于 0x100 返回 0。
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
 * 实现注意事项与已知限制
 *
 *  1) 【__utf16_to_utf8 只能测长度】它的写出部分没有实现(形参 utf8 未使用),
 *     因为唯一的调用点 font_textout_utf8 传的就是 NULL, 只要长度。
 *     要用它做真正的 UTF-16 -> UTF-8 转换, 必须先把写出补上。
 *
 *  2) 【font_open 失败仍返回非 NULL】字库打不开时只打日志、不返回 NULL ——
 *     返回 NULL 会让不判空的调用方直接崩。调用方要判成败就看 info->sta 的
 *     FT_ERROR_* 位。ret 必须有初值, 否则 default 分支不赋值时会读到 UB。
 *
 *  3) 【ascpixel 缓冲按 nbytes * 2 分配】而 font_ascii.c 的取模函数用
 *     info->ascpixel.nbytes 做上限判断 —— 两处对"nbytes 指字高还是字宽"的
 *     理解不一致, 现在靠这 2 倍余量兜着。要真正对齐得统一这个语义, 牵动整个
 *     font_* 家族, 而且现有字库都是按当前语义打包的, 属格式约定问题。
 *
 *  4) 【font_utf8toansi 里的 malloc(utf16len)】utf16len 就是字节数(每字符
 *     2 字节), 所以大小是对的; 之后把它当 u16* 用 —— malloc 的返回值满足对齐
 *     要求, 实际安全。
 *
 *  5) 【font_textout_utf8 的降级行为】__utf8_to_utf16 在遇到非法 UTF-8 时会
 *     提前退出, 这里不做额外处理 —— 显示已转换的部分比整串不显示更好。
 */
