/**
 * @file    ui_port_misc.c
 * @brief   UI 框架杂项工具 —— CRC / 字符串 / 哈希
 *
 * 这些在原厂是编译好的库(interface/utils), 没有源码, 只能按行为重写。
 */
#include "jl_typedef.h"
#include "jl_crc.h"
#include "jl_ascii.h"
/* 打开本文件的分级日志。jl_debug.h 的 log_* 是靠这几个宏开关的,
 * 不定义就是空实现 —— port 层是上板排查的关键路径, 必须留着。 */
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE
#include "jl_debug.h"

/* ==================================================================== *
 *  CRC16
 * ==================================================================== */

/**
 * @brief 杰理 SDK 的 CRC16
 *
 * 算法 = CRC-16/XMODEM: 多项式 0x1021, 初值 0x0000, 无输入/输出反转,
 * 结果不异或, MSB first。
 *
 * @note 这个算法【不是猜的】。原厂 CRC16 只有声明、实现在预编译库里,
 *       所以是拿真实资源文件反推验证的: 用 703 SDK 的
 *       cpu/br27/tools/{JL_OLED,JL,ui_resource}/JL.res 共 892 个
 *       RES_BMP_T 表项, 逐个比对文件里存的 head_crc —— 892 全中, 0 失败。
 *
 * @note 为什么必须一致: res/resfile.c 的 open_image_by_id 拿
 *       CRC16(&res_pic.data_crc, sizeof(res_pic)-2) 和文件里的 head_crc
 *       比较, 算法不同则每张图都加载失败, 表现为界面整块空白。
 *       换资源打包工具版本后建议重新验一次。
 */
u16 CRC16(const void *ptr, u32 len)
{
    const u8 *p = (const u8 *)ptr;
    u16 crc = 0;
    u32 i;
    u8  bit;

    if ((p == NULL) || (len == 0)) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        crc ^= (u16)((u16)p[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U) {
                crc = (u16)((u16)(crc << 1) ^ 0x1021U);
            } else {
                crc = (u16)(crc << 1);
            }
        }
    }
    return crc;
}

u16 CRC16_with_initval(const void *ptr, u32 len, u16 i_val)
{
    const u8 *p = (const u8 *)ptr;
    u16 crc = i_val;
    u32 i;
    u8  bit;

    if ((p == NULL) || (len == 0)) {
        return i_val;
    }

    for (i = 0; i < len; i++) {
        crc ^= (u16)((u16)p[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U) {
                crc = (u16)((u16)(crc << 1) ^ 0x1021U);
            } else {
                crc = (u16)(crc << 1);
            }
        }
    }
    return crc;
}

void CrcDecode(void *buf, u16 len)
{
    /* 原厂用于给某类带 CRC 尾的数据做原地解码。UI 框架里只有声明、
     * 零调用点, 故留空实现。真需要时按具体数据格式补。 */
    (void)buf;
    (void)len;
}


/* ==================================================================== *
 *  字符串工具
 * ==================================================================== */

/**
 * @brief 无符号整数转十进制字符串
 *
 * @param pStr   输出缓冲
 * @param intNum 待转换的数
 * @param strLen 固定宽度: 大于 0 时【严格】输出这么多位, 高位补零, 且
 *               【不写结束符】; 等于 0 时按自然宽度输出并补结束符
 * @param bufLen 缓冲容量, 绝不越过
 *
 * @note 两种模式都是框架实际在用的, 不能统一:
 *       - ui_time.c:  ASCII_IntToStr(p, year, 4, 4) —— bufLen 等于 strLen,
 *         根本没有位置放结束符; 调用方随后 p += 4 继续拼下一段。
 *         这里若补结束符就会踩掉下一个字段。
 *       - ui_slider.c: ASCII_IntToStr(text, value, 0, 16) —— 结果直接
 *         当 C 字符串交给 show_text, 必须有结束符。
 */
void ASCII_IntToStr(void *pStr, u32 intNum, u32 strLen, u32 bufLen)
{
    u8 *out = (u8 *)pStr;
    u8 tmp[10];           /* u32 最多 10 位十进制 */
    u32 digits = 0;
    u32 i;

    if ((out == NULL) || (bufLen == 0)) {
        return;
    }

    /* 逆序取各位 */
    do {
        tmp[digits++] = (u8)(0x30U + (intNum % 10U));
        intNum /= 10U;
    } while ((intNum != 0U) && (digits < sizeof(tmp)));

    if (strLen > 0U) {
        /* 定宽模式: 右对齐, 高位补零。位数超出宽度时保留低位
         * (与"只显示两位分钟"这类需求一致, 不做截断报错) */
        u32 width = (strLen <= bufLen) ? strLen : bufLen;

        for (i = 0; i < width; i++) {
            u32 src = width - 1U - i;   /* 从高位往低位填 */
            out[i] = (src < digits) ? tmp[src] : (u8)0x30U;
        }
        /* 有意不写结束符 —— 见函数注释 */
    } else {
        /* 自然宽度模式: 需要给结束符留一个字节 */
        u32 room = bufLen - 1U;
        u32 n = (digits <= room) ? digits : room;

        for (i = 0; i < n; i++) {
            out[i] = tmp[n - 1U - i];
        }
        out[n] = 0U;
    }
}

u32 ASCII_StrToInt(const void *pStr, u32 *pRint, u32 strLen)
{
    const u8 *p = (const u8 *)pStr;
    u32 val = 0;
    u32 i;

    if ((p == NULL) || (pRint == NULL)) {
        return 0;
    }
    for (i = 0; i < strLen; i++) {
        if ((p[i] < 0x30U) || (p[i] > 0x39U)) {
            break;
        }
        val = val * 10U + (u32)(p[i] - 0x30U);
    }
    *pRint = val;
    return i;
}

u32 ASCII_StrToHex(const char *pStr)
{
    u32 val = 0;

    if (pStr == NULL) {
        return 0;
    }
    while (*pStr != 0) {
        u8 c = (u8)*pStr;
        u8 nibble;

        if ((c >= 0x30U) && (c <= 0x39U)) {             /* 0-9 */
            nibble = (u8)(c - 0x30U);
        } else if ((c >= 0x61U) && (c <= 0x66U)) {      /* a-f */
            nibble = (u8)(c - 0x61U + 10U);
        } else if ((c >= 0x41U) && (c <= 0x46U)) {      /* A-F */
            nibble = (u8)(c - 0x41U + 10U);
        } else {
            break;
        }
        val = (val << 4) | nibble;
        pStr++;
    }
    return val;
}

void ASCII_ToUpper(void *buf, u32 len)
{
    u8 *p = (u8 *)buf;
    u32 i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        if ((p[i] >= 0x61U) && (p[i] <= 0x7AU)) {   /* a-z */
            p[i] = (u8)(p[i] - 0x20U);
        }
    }
}

void ASCII_ToLower(void *buf, u32 len)
{
    u8 *p = (u8 *)buf;
    u32 i;

    if (p == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        if ((p[i] >= 0x41U) && (p[i] <= 0x5AU)) {   /* A-Z */
            p[i] = (u8)(p[i] + 0x20U);
        }
    }
}

/** @return 相等返回 0, 不等返回 1(与原厂 u32 返回值一致) */
u32 ASCII_StrCmp(const char *src, const char *dst, u32 len)
{
    u32 i;

    if ((src == NULL) || (dst == NULL)) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        if (src[i] != dst[i]) {
            return 1;
        }
    }
    return 0;
}

/** @return 相等返回 0, 不等返回 -1 */
int ASCII_StrCmpNoCase(const char *src, const char *dst, int len)
{
    int i;

    if ((src == NULL) || (dst == NULL)) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        u8 a = (u8)src[i];
        u8 b = (u8)dst[i];

        if ((a >= 0x41U) && (a <= 0x5AU)) {
            a = (u8)(a + 0x20U);
        }
        if ((b >= 0x41U) && (b <= 0x5AU)) {
            b = (u8)(b + 0x20U);
        }
        if (a != b) {
            return -1;
        }
    }
    return 0;
}

/** 单字节字符串长度, 最多查 len 个字节 */
u32 ASCII_StrLen(void *str, u32 len)
{
    const u8 *p = (const u8 *)str;
    u32 i;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (p[i] == 0U) {
            break;
        }
    }
    return i;
}

/** 双字节(Unicode)字符串长度, 以 0x0000 结束; 返回【字节数】 */
u32 ASCII_WStrLen(void *str, u32 len)
{
    const u8 *p = (const u8 *)str;
    u32 i;

    if (p == NULL) {
        return 0;
    }
    for (i = 0; (i + 1U) < len; i += 2U) {
        if ((p[i] == 0U) && (p[i + 1U] == 0U)) {
            break;
        }
    }
    return i;
}


/* ==================================================================== *
 *  其它
 * ==================================================================== */

/**
 * @brief 字符串哈希, 原厂用于资源缓存指纹
 * @note 只需在本工程内部自洽(不与资源文件里存的值比对), 所以算法不必
 *       与原厂位级一致。这里用 DJB2。
 */
u32 JBHash(const void *data, int len)
{
    const u8 *p = (const u8 *)data;
    u32 hash = 5381;
    int i;

    if ((p == NULL) || (len <= 0)) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (u32)p[i];   /* hash * 33 + c */
    }
    return hash;
}

/**
 * @brief 资源文件魔数校验的开关位
 *
 * res/resfile.c 里 if (JLUI_TYPE_AND_VERSION & 0x10) 分出两支, 但框架的
 * 加固记录已指出【两支的魔数校验逐字相同】(原厂复制粘贴后忘了改),
 * 所以取值不影响行为。这里取 0, 走 else 支。
 *
 * 用非 const 的 int 是因为框架声明成 extern int。
 */
int JLUI_TYPE_AND_VERSION = 0;
