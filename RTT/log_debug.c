#include "log_debug.h"

uint32_t tick_us = 0;
uint32_t tick_ms = 0;
uint32_t tick_s = 0;
uint32_t tick_min = 0;
uint32_t tick_hour = 0;
uint64_t SysTick_us = 0;
uint64_t SysTick_ms = 0; 

void log_timer_calculation(void)
{
    tick_us += 1000;
    SysTick_us += 1000;
    if(tick_us >= 1000) {
        tick_us = 0;
        tick_ms++;
        SysTick_ms++;
        if(tick_ms >= 1000)
        {
            tick_ms = 0;
            tick_s++;
            if(tick_s >= 60)
            {
                tick_s = 0;
                tick_min++;
                if(tick_min >= 60)
                {
                    tick_min = 0;
                    tick_hour++;
                }
            }
        }
    }
}

int _write(int file, char *ptr, int len)
{
    (void)file;  // ???????
    for (int i = 0; i < len; i++) {
        SEGGER_RTT_PutChar(0, ptr[i]);
    }
    return len;
}