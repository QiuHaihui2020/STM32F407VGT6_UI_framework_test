#include "FreeRTOS.h"
#include "task.h"
#include "log_debug.h"

extern uint32_t SystemCoreClock;

/**
* @brief 启动系统滴答定时器SysTick
* @param 无
* @retval 无
*/

#if !defined(xPortSysTickHandler) || (xPortSysTickHandler != SysTick_Handler)
#define TICK_CNT_TIME_US (1000000 / configTICK_RATE_HZ)
void SysTick_Init()
{
#if 1
    uint32_t tick = 0;

    //读取TENMS值,表示AHB/8时钟时，10ms的重装载值
    uint32_t tenms_value = SysTick->CALIB & SysTick_CALIB_TENMS_Msk;

    if ((SysTick->CALIB & SysTick_CALIB_SKEW_Msk) == 0) {
    // TENMS值准确，可安全使用
        tick = (tenms_value+1) / (10000 / TICK_CNT_TIME_US); 
    } else {
        // TENMS值可能不准确，需手动校准或采用其他方法
        tick = SystemCoreClock / (1000000 / TICK_CNT_TIME_US); 
    }
    if(SysTick_Config(tick))
    {
        /* 错误处理*/
        while(1);
    }
    //使用AHB/8时钟
    SysTick->CTRL  &= ~SysTick_CTRL_CLKSOURCE_Msk; 
#else
    if(SysTick_Config(SystemCoreClock / 1000000))
    {
        /* 错误处理*/
        while(1);
    }
#endif
}


void SysTick_Handler(void)
{
#if (INCLUDE_xTaskGetSchedulerState == 1)
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
#endif
        extern void xPortSysTickHandler( void );
        xPortSysTickHandler();
        
#if (INCLUDE_xTaskGetSchedulerState == 1)
    }
#endif
}
#endif

#if configCHECK_FOR_STACK_OVERFLOW
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;  // 避免未使用参数警告
    (void)pcTaskName;
    
    // 在这里处理栈溢出情况
    // 例如：打印错误信息、记录日志、系统复位等
    r_printf("Stack overflow in task: %s\n", pcTaskName);
    // configASSERT 失败分支内部已死循环，此处无需再加 while(1)
    configASSERT(0, "stack overflow: %s", pcTaskName);

    // TODO(haihui.qiu): 需要时在此改为系统复位
}
#endif

#if configUSE_MALLOC_FAILED_HOOK
void vApplicationMallocFailedHook(void)
{
    /* 内存分配失败时的处理 */
    r_printf("Memory allocation failed!\n");
    
    /* 可能采取的措施：
     * 1. 记录错误日志
     * 2. 复位系统
     * 3. 进入安全模式
     */
    // configASSERT 失败分支内部已死循环，此处无需再加 while(1)
    configASSERT(0, "malloc failed");
}
#endif

/* 可以使用SysTick作为运行时间统计的时基 */
void vConfigureTimerForRunTimeStats(void)
{

}
extern uint32_t HAL_GetTick(void);
uint32_t xGetRunTimeCounterValue(void)
{
    return HAL_GetTick();
}

#if configUSE_IDLE_HOOK
void vApplicationIdleHook(void)
{
    // 示例：喂狗
    // HAL_IWDG_Refresh(&hiwdg);

}
#endif
