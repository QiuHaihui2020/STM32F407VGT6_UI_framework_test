#include "main.h"
#include "app_led.h"
#include "task_manager.h"
#include "stm32f4xx_hal.h"
#include "log_debug.h"
#include "apps.h"

/* 闪灯半周期。configTICK_RATE_HZ = 1000, 故 tick 数即毫秒数 */
#define LED_BLINK_HALF_PERIOD_MS    500U

/**
 * @brief 打印 FreeRTOS 堆(heap_4)的占用与剩余, 以及当前任务的栈余量
 *
 * 统计口径: 只算 FreeRTOS 管理的那一块堆(configTOTAL_HEAP_SIZE, 本工程 64KB),
 * 不含全局/静态变量占用的 RAM。各任务的栈和内核对象都是从这块堆里分配的,
 * 所以 task_create 的开销已经算进 used 里。
 *
 * @note 本函数是由 app_core 任务通过 Q_CALLBACK 回调执行的, 因此打印的栈水位
 *       属于 app_core 任务, 不是 led_task。
 * @note min_free 是系统启动至今出现过的最少剩余, 用来判断堆是否曾经接近耗尽,
 *       比瞬时 free 更有参考价值。
 */
void app_led_cb_log_test(void)
{
    const uint32_t heap_total    = (uint32_t)configTOTAL_HEAP_SIZE;
    const uint32_t heap_free     = (uint32_t)xPortGetFreeHeapSize();
    const uint32_t heap_min_free = (uint32_t)xPortGetMinimumEverFreeHeapSize();

    log_debug("heap: total %u Byte, used %u Byte, free %u Byte | min_free %u Byte (peak used %u Byte)\n",
              heap_total,
              heap_total - heap_free,
              heap_free,
              heap_min_free,
              heap_total - heap_min_free);

#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    /* 高水位是"历史最少剩余槽数", 单位为 StackType_t(4 字节), 越小越危险 */
    log_debug("stack: cur task free_min %u Byte\n",
              (uint32_t)uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t));
#endif
}

static void app_led_task(void *pvParameters)
{
    (void)pvParameters;
    int msg[2];

    while (1) {
        /* 用 os_time_dly 而非 HAL_Delay: HAL_Delay 是忙等, 在任务里会白占 CPU
           且不让出调度, 同优先级的其他任务会被饿死 */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
        os_time_dly(LED_BLINK_HALF_PERIOD_MS);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
        os_time_dly(LED_BLINK_HALF_PERIOD_MS);
        //log_debug("app_led_task running\n");

        msg[0] = (int)app_led_cb_log_test;
        msg[1] = 0;
        os_taskq_post_type("app_core", Q_CALLBACK, 2, msg);
    }
}

void app_led_init(void)
{
    int err = task_create(app_led_task, NULL, "led_task");
    if (err != OS_NO_ERR) {
        log_error("create app_led failed, err %d\n", err);
        return;
    }
    log_debug("create app_led succ\n");
}