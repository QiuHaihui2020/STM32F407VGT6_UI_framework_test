/**
 * @file    ui_port_log.c
 * @brief   UI 框架日志 / 断言落点
 *
 * 本文件【只补工程没有的东西】, 不重复实现打印:
 *
 *   - log_print()  : 框架需要一个【函数形式】的输出落点。工程 RTT/log_debug.h
 *                    里全是宏, 宏接不了 va_list, 所以必须有这一个函数。
 *                    输出前缀(颜色 + 时间戳)直接复用 log_debug.h 的宏,
 *                    保证 UI 的日志和工程其它日志格式一致。
 *   - cpu_assert() : 框架 ASSERT 的语义与工程 configASSERT 不同 ——
 *                    config_asser 为假时只记录不停机, 所以不能直接用前者。
 *
 *   - log_put_buf(): jl_debug.h 里 *_hexdump 的落点。没有直接复用工程
 *                    log_debug.h 的 put_buf 宏, 理由见 jl_debug.h 里
 *                    该声明的注释(宏展开位置 + NULL 判空)。
 */
#include "jl_debug.h"
#include "ui_port_config.h"
#include <stdarg.h>
#include "stdio.h"

/** 断言开关。
 * 为真 = 断言失败时打印文件/行号后停机(调试用);
 * 为假 = 只记录一行, 让设备继续跑(出厂用)。
 *
 * 框架里有 161 处 ASSERT, 相当一部分是"参数不该为空但为空了也能降级
 * 运行"的软断言, 所以【不要】无脑改成 while(1)。 */
const int config_asser = 1;

void log_print(u32 level, const char *tag, const char *format, ...)
{
    va_list args;

    (void)tag;      /* 框架恒传 NULL, 分级信息已在 format 里 */

    /* 前缀(颜色 + 时间戳)照抄 log_debug.h 里那几个宏的展开结果 ——
     * 用它自己的 TIME_INFO_STR / TIME_INFO_ARGS, 这样
     * LOG_TIME_INFO_ENABLE 一关, UI 日志也跟着不带时间戳, 不会走样。 */
    switch (level) {
    case __LOG_ERROR:
        printf("%s" TIME_INFO_STR, RTT_CTRL_TEXT_BRIGHT_RED, TIME_INFO_ARGS);
        break;
    case __LOG_WARN:
        printf("%s" TIME_INFO_STR, RTT_CTRL_TEXT_BRIGHT_YELLOW, TIME_INFO_ARGS);
        break;
    case __LOG_DEBUG:
        printf("%s" TIME_INFO_STR, RTT_CTRL_TEXT_BRIGHT_GREEN, TIME_INFO_ARGS);
        break;
    default:
        printf("%s" TIME_INFO_STR, RTT_CTRL_TEXT_BRIGHT_WHITE, TIME_INFO_ARGS);
        break;
    }

    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void cpu_assert(char *file, int line, bool condition, char *cond_str)
{
    if (condition) {
        return;     /* 条件成立, 什么都不用做 */
    }

    /* file 为 NULL 表示 config_asser 关闭 —— 此时 ASSERT 宏没打印过任何
     * 东西, 这里补一行最起码的定位信息, 但不停机 */
    if (file == NULL) {
        log_error("UI ASSERT line:%d (%s)\r\n", line, cond_str ? cond_str : "?");
        return;
    }

    log_error("\r\n!!! UI ASSERT FAILED !!!\r\n  file: %s\r\n  line: %d\r\n  cond: %s\r\n",
              file, line, cond_str ? cond_str : "?");

    /* config_asser 打开时停机, 便于用调试器抓现场。
     * 出厂固件把 config_asser 改成 0 即可让它只打印不停机。 */
    while (1) {
        /* 让看门狗把设备复位, 而不是死在这里没有任何反应 */
    }
}

void log_put_buf(const uint8_t *buf, uint32_t len)
{
    if (buf && len > 0) {
        printf(RTT_CTRL_TEXT_BRIGHT_WHITE);
        for (uint32_t i = 0; i < len; i++) {
            if (i % 16 == 0) {
                printf("\n");
            }
            printf("%02X ", buf[i]);
        }
        printf("\n");
    }
}

