/**
 * @file    jl_debug.h
 * @brief   UI 框架日志 / 断言接口
 *
 * 合并了原厂 interface/utils/debug.h(分级日志宏) 与
 * interface/system/generic/cpu.h(ASSERT) 两处, 只保留框架真正用到的。
 * 实现在 liba/common/jl_debug.c, 最终落到工程 RTT/log_debug.h 的串口打印。
 *
 * @note 有意【不】叫 debug.h —— 避免和别处同名头相互抢占。
 */
#ifndef __JL_DEBUG_H__
#define __JL_DEBUG_H__

#include "jl_typedef.h"
#include <stdio.h>

/* ⚠ 必须在这里就把工程自己的日志头包进来。
 *
 * RTT/log_debug.h 也定义了 log_debug / log_info / log_error。若它在本文件
 * 【之后】才被包含, 就会覆盖框架的分级版本并刷出 macro-redefined 告警。
 * 先包含它、让它的 include guard 生效, 再由本文件 undef + 重新定义,
 * 顺序就固定了: 这三个宏本文件永远是最终定义方。
 * 其余的(y_/g_/r_printf、put_buf)则直接沿用工程版本, 不重复定义。
 *
 * @note 因此【框架/port 之外的工程文件不要包含 jl_debug.h】——
 *       框架版 log_debug 需要每个 .c 自行 #define LOG_DEBUG_ENABLE 才输出,
 *       没定义就是空实现, 会静默丢掉打印。工程文件请直接用 log_debug.h。 */
#include "log_debug.h"

/* Keil 的 <stdio.h> 把 putchar 实现为【函数式宏】。而框架 font_all.h 里
 * 有个同名的函数指针成员(info->putchar), 调用点 info->putchar(info, ...)
 * 会被宏抢走, 报 "too many arguments to function-like macro"。
 * 这里取消宏定义, 保留同名库函数(标准要求它同时是函数)。 */
#ifdef putchar
#undef putchar
#endif

/* ---- 终端颜色(框架里有零星引用) ------------------------------------- */
#define RedBold             "\033[31;1m"
#define GreenBold           "\033[32;1m"
#define YellowBold          "\033[33;1m"
#define BlueBold            "\033[34;1m"
#define PurpleBold          "\033[35;1m"
#define WhiteBold           "\033[37;1m"
#define Reset               "\033[0;25m"

/* ---- 底层输出 ------------------------------------------------------- */
/** 日志等级, 与原厂取值保持一致 */
#define __LOG_VERB      0
#define __LOG_INFO      1
#define __LOG_DEBUG     2
#define __LOG_WARN      3
#define __LOG_ERROR     4

/**
 * @brief 分级日志底层输出
 * @param level  __LOG_* 之一
 * @param tag    保留, 框架恒传 NULL
 * @param format printf 风格格式串
 */
void log_print(u32 level, const char *tag, const char *format, ...);

void log_put_buf(const uint8_t *buf, u32 len);

/* 十六进制转储【有意不另立接口】: 工程 RTT/log_debug.h 已有 put_buf() 宏
 * (同样是 %02X + 每 16 字节换行), 下面的 *_hexdump 直接转发过去。
 * 上面已经 include 了 log_debug.h, 所以 put_buf 在这里可用。 */

/* y_printf / g_printf / r_printf 【有意不在这里定义】——
 * 工程 RTT/log_debug.h 已经有这三个(颜色 + 时间戳 + printf), 语义完全相同。
 * 上面已 include 了它, 所以框架里那两处调用直接用工程版本即可。
 * 早先在这里 undef + 重定义过一遍, 属于重复造轮子, 还会刷 macro-redefined 告警。
 *
 * 注意与下面 log_debug/log_info/log_error 的区别: 那三个【必须】重定义,
 * 因为框架的约定是每个 .c 自行 #define LOG_DEBUG_ENABLE 才输出, 没定义就是
 * 空实现 —— 框架里有几百处 log_debug, 不做分级会把串口刷爆。
 * 而 y_/g_/r_printf 在框架里是无条件打印, 没有分级需求。 */

/* ---- 分级日志宏 -----------------------------------------------------
 * 原厂支持"宏控制"和"常量控制"两套开关机制。本移植统一走宏控制:
 * 每个 .c 在 #include 本文件之前自行 #define LOG_DEBUG_ENABLE 等,
 * 未定义的等级会被编译成空语句。
 *
 * ⚠ 工程 RTT/log_debug.h 也定义了 log_debug/log_info/log_error。
 *   两者语义相同(都是打印), 但 token 序列不同, 同一个 TU 里同时包含
 *   会触发 armclang 的 macro-redefined 告警 —— 所以这里先 #undef。
 *   框架文件不包含 log_debug.h, 只有 port/ 会, 两边不会真正混用。 */
#undef  log_debug
#undef  log_info
#undef  log_error
#undef  log_warn

#ifndef LOG_TAG
#define LOG_TAG         ""
#endif

#ifdef LOG_INFO_ENABLE
#define log_info(fmt, ...)      log_print(__LOG_INFO,  NULL, LOG_TAG fmt, ##__VA_ARGS__)
#else
#define log_info(...)
#endif

#ifdef LOG_DEBUG_ENABLE
#define log_debug(fmt, ...)     log_print(__LOG_DEBUG, NULL, LOG_TAG fmt, ##__VA_ARGS__)
#define log_debug_hexdump(x, y) log_put_buf((x), (y))
#else
#define log_debug(...)
#define log_debug_hexdump(x, y)
#endif

#ifdef LOG_ERROR_ENABLE
#define log_warn(fmt, ...)      log_print(__LOG_WARN,  NULL, "<warning>:" LOG_TAG fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)     log_print(__LOG_ERROR, NULL, "<error>:"   LOG_TAG fmt, ##__VA_ARGS__)
#define log_error_hexdump(x, y) log_put_buf((x), (y))
#else
#define log_warn(...)
#define log_error(...)
#define log_error_hexdump(...)
#endif

#ifdef LOG_DUMP_ENABLE
#define log_info_hexdump(x, y)  log_put_buf((x), (y))
#else
#define log_info_hexdump(...)
#endif

#ifdef LOG_CHAR_ENABLE
#define log_char(x)             (void)fputc((x), stdout)
#else
#define log_char(x)
#endif

/* 框架里有一处 log_d, 是 log_debug 的简写 */
#define log_d       log_debug
#define log_e       log_error
#define log_w       log_warn
#define log_i       log_info

/* ui/includes.h 里 UI_ONTOUCH_DEBUG 会指向 log_d */

/* ---- 断言 -----------------------------------------------------------
 * config_asser 为真 = 断言失败时打印详情;
 * cpu_assert 是最终落点, 由 liba/common/jl_debug.c 决定停机还是继续。
 *
 * 保持原厂两级结构(而不是直接 while(1)), 因为框架里有 161 处 ASSERT,
 * 其中不少是"参数不该为空但为空了也能降级运行"的软断言。 */
extern const int config_asser;
void cpu_assert(char *file, int line, bool condition, char *cond_str);

/** 单核, 恒为 0。原厂 ASSERT 宏里会打印它 */
#define current_cpu_id()    0

#define ASSERT(a, ...) \
    do { \
        if (config_asser) { \
            if (!(a)) { \
                printf("cpu %d file:%s, line:%d", current_cpu_id(), __FILE__, __LINE__); \
                printf("ASSERT-FAILD: "#a" " __VA_ARGS__); \
                cpu_assert(__FILE__, __LINE__, (a) ? true : false, #a); \
            } \
        } else { \
            if (!(a)) { \
                cpu_assert(NULL, __LINE__, (a) ? true : false, #a); \
            } \
        } \
    } while (0)

#endif /* __JL_DEBUG_H__ */
