#include "main.h"
#include "app_led.h"
#include "task_manager.h"
#include "stm32f4xx_hal.h"
#include "log_debug.h"
#include "apps.h"

/* 闪灯半周期。configTICK_RATE_HZ = 1000, 故 tick 数即毫秒数 */
#define LED_BLINK_HALF_PERIOD_MS    500U

void app_led_cb_log_test(void)
{
    log_debug("app_led_cb_log_test\n");
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