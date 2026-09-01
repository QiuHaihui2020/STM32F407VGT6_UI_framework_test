/**
 * @file    jl_ascii.c
 * @brief   杰理 ASCII 字符串工具库
 *
 * 【来源】从 cpu/br27/liba/ascii.a 的 ASCII_lib.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故按 IR + DWARF 还原, 不是按语义猜。
 *     原始路径: btsdk/lib/utils/ascii/ASCII_lib.c
 *     原厂行号: ToUpper 20  ToLower 29  StrCmp 47  StrCmpNoCase 74
 *               IntToStr 105  StrToInt 126  StrToHex 140  StrLen 177
 *               WStrLen 191  JBHash 204
 *
 * 【字符比较一律走 u8】原厂 IR 里字符载入是 `sext i8`, 即 pi32 的 char 有符号;
 *   本工程 armclang 带 -funsigned-char, char 无符号。原厂的大小写/数字区间判断
 *   用的是 `add i8 c, -48` + `icmp ult` 这种无符号手法, 所以这里统一按 u8 取值
 *   再比较 —— 两个平台结果完全一致, 不受 char 符号性影响。
 *
 * 【不加判空】原厂这几个函数一个都不判 NULL(IR 的函数属性里还带
 *   "allow-nullptr-deref")。这里如实照做, 不擅自加固: 加了会把"传 NULL"从
 *   崩溃变成静默返回, 是行为变更而不是修 Bug。
 */
#include "jl_ascii.h"


/*
 * @note 原厂是【倒序】遍历: while (len--) 之后拿 buf[len], 从末字节往前走。
 *       结果与正序相同, 照抄以对齐 IR。
 */
void ASCII_ToUpper(void *buf, u32 len)
{
    u8 *p = (u8 *)buf;

    while (len--) {
        u8 c = p[len];

        if ((u8)(c - 'a') < 26U) {
            p[len] = (u8)(c - 32U);
        }
    }
}

void ASCII_ToLower(void *buf, u32 len)
{
    u8 *p = (u8 *)buf;

    while (len--) {
        u8 c = p[len];

        if ((u8)(c - 'A') < 26U) {
            p[len] = (u8)(c + 32U);
        }
    }
}

/*
 * @brief 带通配符的字符串比较
 * @return 0 = 匹配; 非 0 = 第一个不匹配字符的【1 起序号】
 *
 * @note 通配符是 '?' 与 '*' 两个, 且【src 与 dst 两侧都生效】——
 *       jl_ascii.h 的注释只提了 dst 侧的 '?', 与实现不符, 以实现为准
 *       (IR 里四个 icmp: dst=='?' dst=='*' src=='?' src=='*' 全部 or 在一起)。
 * @note 两串同时到结束符 -> 0(匹配); 只有一方到 -> 报当前位置。
 */
u32 ASCII_StrCmp(const char *src, const char *dst, u32 len)
{
    u32 n = len;

    while (n--) {
        u8 s = (u8)*src;
        u8 d = (u8)*dst;

        if ((s == 0U) && (d == 0U)) {
            return 0;
        }
        if ((s == 0U) || (d == 0U)) {
            return len - n;
        }
        if ((s != d) && (d != '?') && (d != '*') && (s != '?') && (s != '*')) {
            return len - n;
        }
        src++;
        dst++;
    }

    return 0;
}

/*
 * @brief 不区分大小写的字符串比较(不支持通配符)
 * @return 0 = 匹配; 非 0 = 第一个不匹配字符的【1 起序号】
 *
 * @note 折叠方向是【单向】的: 只把 src 换成另一种大小写再和 dst 比,
 *       不对 dst 做任何变换。src 既不是字母又不等于 dst 时直接判不匹配。
 */
int ASCII_StrCmpNoCase(const char *src, const char *dst, int len)
{
    int n = len;

    while (n--) {
        u8 s = (u8)*src;
        u8 d = (u8)*dst;

        if (s == 0U) {
            if (d == 0U) {
                return 0;
            }
            return len - n;
        }
        if (d == 0U) {
            return len - n;
        }
        if (s != d) {
            if ((u8)(s - 'a') < 26U) {
                if ((u8)(s - 32U) != d) {
                    return len - n;
                }
            } else if ((u8)(s - 'A') < 26U) {
                if ((u8)(s + 32U) != d) {
                    return len - n;
                }
            } else {
                return len - n;
            }
        }
        src++;
        dst++;
    }

    return 0;
}

/*
 * @brief 无符号整数转十进制字符串, 右对齐填充
 * @param strLen 输出宽度。为 0 时按 intNum 的自然位数
 * @param bufLen 缓冲容量, 下标 >= bufLen 的那一位【跳过不写但照样继续循环】
 *
 * @note 【从不写结束符】—— 任何模式下都只写数字字符, 这是原厂行为
 *       (IR 里全函数只有一处 store, 存的是 `(intNum % 10) | 0x30`)。
 *       所以拿它产出 C 字符串的调用方必须自己保证缓冲已清零。
 * @note intNum 为 0 且 strLen 为 0 时自然位数是 0, 【一个字节都不写】。
 * @note 位数超过 strLen 时保留低位(高位被 intNum /= 10 吃掉)。
 */
void ASCII_IntToStr(void *pStr, u32 intNum, u32 strLen, u32 bufLen)
{
    u8 *out = (u8 *)pStr;

    if (strLen == 0U) {
        u32 len = 0;
        u32 num = intNum;

        while (num != 0U) {
            len++;
            num /= 10U;
        }
        strLen = len;
    }

    while (strLen--) {
        if (strLen < bufLen) {
            out[strLen] = (u8)((intNum % 10U) | 0x30U);
        }
        intNum /= 10U;
    }
}

/*
 * @brief 定长十进制字符串转整数
 * @return 0 = 全部转换成功; 非 0 = 遇到非数字时的【下标】
 *
 * @note 从【末尾往前】扫, 乘数 m 由 1 逐次 ×10 —— 所以 strLen 必须正好
 *       覆盖数字段, 多给会把前面的字节也当数位。
 * @note *pRint 在入口无条件清 0, 且每转一位就回写一次。
 */
u32 ASCII_StrToInt(const void *pStr, u32 *pRint, u32 strLen)
{
    const u8 *p = (const u8 *)pStr;
    u32 m = 1;

    *pRint = 0;

    while (strLen--) {
        u8 c = p[strLen];

        if ((u8)(c - '0') > 9U) {
            return strLen;
        }
        *pRint += (u32)(c - '0') * m;
        m *= 10U;
    }

    return 0;
}

/*
 * @brief 十六进制字符串转整数, 【必须带 "0x" / "0X" 前缀】
 * @return 解析出的数值; 任何一步不合法都返回 0
 *
 * @note 原厂是个三态状态机: 第 1 个字符必须是 '0', 第 2 个必须是 'x' 或 'X',
 *       之后才是十六进制数位。所以 "1F" 返回 0, 必须写成 "0x1F"。
 * @note 数位段遇到非法字符是【整个返回 0】, 不是返回已解析的部分。
 * @note 只有 "0x" 没有数位时返回 0。溢出静默丢高位。
 */
u32 ASCII_StrToHex(const char *pStr)
{
    u32 step = 0;
    u32 value = 0;

    while (*pStr != 0) {
        u8 c = (u8)*pStr;

        switch (step) {
        case 0:
            if (c != '0') {
                return 0;
            }
            step = 1;
            break;

        case 1:
            if ((c != 'x') && (c != 'X')) {
                return 0;
            }
            step = 2;
            break;

        case 2:
            if ((u8)(c - '0') < 10U) {
                value = (value << 4) | (u32)(c - '0');
            } else if ((u8)(c - 'a') < 6U) {
                value = (value << 4) | (u32)(c - 'a' + 10U);
            } else if ((u8)(c - 'A') < 6U) {
                value = (value << 4) | (u32)(c - 'A' + 10U);
            } else {
                return 0;
            }
            break;

        default:
            break;
        }
        pStr++;
    }

    return value;
}

/*
 * @brief 单字节串长度, 最多看 len 字节
 * @return 首个 0 字节的下标; 没有 0 则返回 len
 */
u32 ASCII_StrLen(void *str, u32 len)
{
    const u8 *p = (const u8 *)str;
    u32 i;

    for (i = 0; i < len; i++) {
        if (p[i] == 0U) {
            break;
        }
    }

    return i;
}

/*
 * @brief 双字节串长度, 步进 2, 以【连续两个 0 字节】为结束符
 * @return 结束符所在下标; 没有则返回停下时的 i
 *
 * @note 高位字节的下标原厂写的是 `i | 1` 而不是 `i + 1`。i 恒为偶数,
 *       两者等价, 照抄以对齐 IR。
 */
u32 ASCII_WStrLen(void *str, u32 len)
{
    const u8 *p = (const u8 *)str;
    u32 i;

    for (i = 0; i < len; i += 2U) {
        if ((p[i] == 0U) && (p[i | 1U] == 0U)) {
            break;
        }
    }

    return i;
}

/*
 * @brief JBHash(即 djb2): hash = hash * 33 + c, 初值 5381
 */
u32 JBHash(const void *data, int len)
{
    const u8 *p = (const u8 *)data;
    u32 hash = 5381;

    while (len--) {
        hash = hash * 33U + (u32)(*p++);
    }

    return hash;
}
