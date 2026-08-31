#include "main.h"
#include "app_led.h"
#include "task_manager.h"
#include "stm32f4xx_hal.h"
#include "log_debug.h"

/* 闪灯半周期。configTICK_RATE_HZ = 1000, 故 tick 数即毫秒数 */
#define LED_BLINK_HALF_PERIOD_MS    500U

static void app_led_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        /* 用 os_time_dly 而非 HAL_Delay: HAL_Delay 是忙等, 在任务里会白占 CPU
           且不让出调度, 同优先级的其他任务会被饿死 */
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
        os_time_dly(LED_BLINK_HALF_PERIOD_MS);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
        os_time_dly(LED_BLINK_HALF_PERIOD_MS);
        log_debug("app_led_task running\n");
    }
}

void app_led_init(void)
{
    BaseType_t xReturn = task_create(app_led_task, NULL, "led_task");
    if (xReturn != pdPASS) {
        log_debug("create app_led failed\n");
    }
    log_debug("create app_led succ\n");
}