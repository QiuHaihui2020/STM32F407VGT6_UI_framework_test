#ifndef __LOG_DEBUG_H__
#define __LOG_DEBUG_H__

#if defined(RAM_DEBUG) && (RAM_DEBUG == 1)
#define RTT_LOG_ENABLE 	1
#else
#define RTT_LOG_ENABLE 	1
#endif

#if 	RTT_LOG_ENABLE
#include "SEGGER_RTT.h"
#else
#include <stdint.h>
#define SEGGER_RTT_printf(...) 
#endif

#define LOG_TIME_INFO_ENABLE 1 //是否打开时间信息

extern uint32_t tick_us;
extern uint32_t tick_ms;
extern uint32_t tick_s;
extern uint32_t tick_min;
extern uint32_t tick_hour;

#if LOG_TIME_INFO_ENABLE
#define SYS_HOUR tick_hour
#define SYS_MIN  tick_min
#define SYS_SEC  tick_s
#define SYS_MS   tick_ms
#define SYS_US   tick_us

#define TIME_INFO_STR  "[%02d:%02d:%02d.%03d.%03d] "
#define TIME_INFO_ARGS SYS_HOUR,SYS_MIN,SYS_SEC,SYS_MS,SYS_US
#else
#define SYS_HOUR 0
#define SYS_MIN  0
#define SYS_SEC  0
#define SYS_MS   0
#define SYS_US   0

#define TIME_INFO_STR  "%s"
#define TIME_INFO_ARGS ""
#endif

#define w_printf(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_WHITE, TIME_INFO_ARGS, ##__VA_ARGS__)
#define g_printf(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_GREEN, TIME_INFO_ARGS, ##__VA_ARGS__) //green
#define r_printf(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_RED, TIME_INFO_ARGS, ##__VA_ARGS__)  //red
#define y_printf(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_YELLOW, TIME_INFO_ARGS, ##__VA_ARGS__)  //yellow

#define log_debug(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_GREEN, TIME_INFO_ARGS, ##__VA_ARGS__)
#define log_info(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_WHITE, TIME_INFO_ARGS, ##__VA_ARGS__) 
#define log_error(format, ...) SEGGER_RTT_printf(0, "%s"TIME_INFO_STR format, RTT_CTRL_TEXT_BRIGHT_RED, TIME_INFO_ARGS, ##__VA_ARGS__)  

#define configASSERT(x, ...) \
    do { \
        if (!(x)) { \
            SEGGER_RTT_printf(0, "%s"TIME_INFO_STR" Assertion failed in %s:%d | ", RTT_CTRL_TEXT_BRIGHT_RED, TIME_INFO_ARGS, __FILE__, __LINE__); \
			SEGGER_RTT_printf(0, ""__VA_ARGS__); \
            while (1); \
        } \
    } while (0)



#define put_buf(array, len) \
    do { \
        SEGGER_RTT_printf(0, RTT_CTRL_TEXT_BRIGHT_WHITE); \
        for (uint32_t i = 0; i < len; i++) { \
            if (i % 16 == 0) { \
                SEGGER_RTT_printf(0, "\n"); \
            } \
            SEGGER_RTT_printf(0, "%02X ", array[i]); \
        } \
        SEGGER_RTT_printf(0, "\n"); \
    } while (0)
  
#define LINE_INFO log_debug("func: %s, line: %d \r\n", __func__, __LINE__);
void log_timer_calculation(void);
	
#endif // __LOG_DEBUG_H__

