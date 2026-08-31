/**
 * @file    jl_typedef.h
 * @brief   杰理 SDK generic/typedef.h 的 STM32 等价物
 *
 * 提供框架源码依赖的类型别名(u8/u16/u32...)、GNU 属性宏(SEC/AT/ALIGNED...)
 * 和位操作宏。
 *
 * @note 有意【不】叫 typedef.h: 工程 Core/Inc/typedef.h 会抢占同名头。
 *       框架里原本 #include "typedef.h" / "generic/typedef.h" 的地方
 *       已统一改为本文件。
 */
#ifndef __JL_TYPEDEF_H__
#define __JL_TYPEDEF_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

/* ---- 错误码 ---------------------------------------------------------
 * 框架用 -EINVAL / -EFAULT / -ENOMEM / -ENOENT 作返回值。
 * Keil 的 <errno.h> 只给了 EINVAL 与 ENOMEM, 缺的两个在这里补上。
 * 用 #ifndef 而不是照搬杰理 errno-base.h 的 Linux 取值, 是为了不和
 * Keil 已有定义打架 —— 框架只关心"非零且为负", 不关心具体数值。 */
#ifndef EFAULT
#define EFAULT      14      /* Bad address */
#endif
#ifndef ENOENT
#define ENOENT      2       /* No such file or directory */
#endif

/* ---- 类型别名 -------------------------------------------------------
 * 直接复用工程自己的 Core/Inc/typedef.h(u8/s8/u16/s16/u32/s32/u64/s64/
 * BOOL/FOURCC 全都有), 而【不是】在这里再 typedef 一遍。
 *
 * 为什么: 两个头都会被同一个 TU 间接包含(框架 -> ui_core.h ->
 * circular_buf.h -> Core/Inc/typedef.h)。重复 typedef 在 C11 才合法,
 * 工程是 C99 严格模式, 会刷出一大片
 * "redefinition of typedef 'u8' is a C11 feature" 告警。
 * 让类型只有一个来源, 顺带也避免了将来两边改得不一致。
 *
 * 注意: Core/Inc/typedef.h 里的 BIT / ARRAY_SIZE 是【无条件】定义的,
 * 所以必须在本文件定义它们【之前】包含它, 下面那些 #ifndef 才拦得住。 */
#include "typedef.h"

/* ---- 编译器属性 -----------------------------------------------------
 * armclang(AC6) 是 clang 前端, GNU 属性全部可用。
 *
 * SEC/sec/AT 在原厂用于把代码数据摆进指定段, 配合 GNU ld 的段收集做
 * 控件注册表。STM32 侧注册表已改为显式表(port/ui_port_registry.c),
 * 因此这些宏【有意留空】—— 保留段属性只会迫使 Keil 分散加载文件跟着改,
 * 反而不利于再移植。 */
#define SEC(x)
#define sec(x)
#define SEC_USED(x)     __attribute__((used))
#define AT(x)
#define SET(x)          __attribute__((x))
#define ALIGNED(x)      __attribute__((aligned(x)))
#define _GNU_PACKED_    __attribute__((packed))
#define _NOINLINE_      __attribute__((noinline))
#define _INLINE_        __attribute__((always_inline))
#define _WEAK_          __attribute__((weak))
#define _WEAKREF_       __attribute__((weakref))
#define _NORETURN_      __attribute__((noreturn))
#define _NAKED_         __attribute__((naked))

/** 原厂把 UI 热代码摆进片内 RAM 段 .ui_ram 提速。
 * F407 从 Flash 取指有 ART 加速, 且改分散加载会污染工程, 故留空。
 * 若后续要提速: 在 .sct 里加 RAM 执行区, 再把本宏改回 AT(.ui_ram) */
#ifndef AT_UI_RAM
#define AT_UI_RAM
#endif

/* ---- 布尔 / 空指针 -------------------------------------------------- */
#undef  FALSE
#define FALSE           0
#undef  TRUE
#define TRUE            1

#ifndef NULL
#define NULL            (void *)0
#endif

/* ---- 位操作 --------------------------------------------------------- */
#ifndef BIT
#define BIT(n)                  (1UL << (n))
#endif

#define BitSET(REG, POS)        ((REG) |= (1UL << (POS)))
#define BitCLR(REG, POS)        ((REG) &= (~(1UL << (POS))))
#define BitXOR(REG, POS)        ((REG) ^= (~(1UL << (POS))))
#define BitCHK_1(REG, POS)      (((REG) & (1UL << (POS))) == (1UL << (POS)))
#define BitCHK_0(REG, POS)      (((REG) & (1UL << (POS))) == 0x00)
#define testBit(REG, POS)       ((REG) & (1UL << (POS)))
#define clrBit(x, y)            ((x) &= ~(1UL << (y)))
#define setBit(x, y)            ((x) |= (1UL << (y)))

/* ---- 非对齐存取 ----------------------------------------------------- */
#define LD_WORD(ptr)            (u16)(*(u16 *)(u8 *)(ptr))
#define LD_DWORD(ptr)           (u32)(*(u32 *)(u8 *)(ptr))
#define ST_WORD(ptr, val)       *(u16 *)(u8 *)(ptr) = (u16)(val)
#define ST_DWORD(ptr, val)      *(u32 *)(u8 *)(ptr) = (u32)(val)

/* ---- 地址读写(框架里只用于资源解析, 非真寄存器) -------------------- */
#define readb(addr)             *((volatile unsigned char *)(addr))
#define readw(addr)             *((volatile unsigned short *)(addr))
#define readl(addr)             *((volatile unsigned long *)(addr))
#define writeb(addr, val)       *((volatile unsigned char *)(addr))  = (u8)(val)
#define writew(addr, val)       *((volatile unsigned short *)(addr)) = (u16)(val)
#define writel(addr, val)       *((volatile unsigned long *)(addr))  = (u32)(val)

#define ALIGN_4BYTE(size)       (((size) + 3) & 0xfffffffcUL)

/** 小端序拼 u16。STM32 与 BR27 同为小端, 直接采用原厂小端分支 */
#define __cpu_u16(lo, hi)       ((hi) | ((lo) << 8))

/* ---- 常用工具宏 ----------------------------------------------------- */
#ifndef MIN
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array)       (sizeof(array) / sizeof((array)[0]))
#endif

#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

/** 位域写入: 把 dat 写进 sfr 的 [start, start+len) 位 */
#define SFR(sfr, start, len, dat) \
    (sfr = (sfr & ~((~(0xffffffffUL << (len))) << (start))) | \
     (((dat) & (~(0xffffffffUL << (len)))) << (start)))

/** 32 位以内的回绕安全比较 */
#define LOOP_OVERTAKE(a, b, n) \
    ((((a) - (b)) & ((1ULL << (n)) - 1)) < ((1UL << ((n) - 1))))

#ifdef offsetof
#undef offsetof
#endif
#define offsetof(type, memb)    ((unsigned long)(&((type *)0)->memb))

#ifdef container_of
#undef container_of
#endif
#define container_of(ptr, type, memb) \
    ((type *)((char *)(ptr) - offsetof(type, memb)))

/** 资源缓存指纹用的哈希。实现在 port/ui_port_misc.c */
u32 JBHash(const void *data, int len);

#endif /* __JL_TYPEDEF_H__ */
