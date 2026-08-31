#include "apps.h"
#include "typedef.h"
#include "task_manager.h"
#include "log_debug.h"
#include "key_driver.h"
#include "app_led.h"
#include "test.h"

const int iokey_table[3][KEY_ACTION_TRIPLE_CLICK + 1] = {
  // KEY_ACTION_CLICK,  KEY_ACTION_LONG,    KEY_ACTION_HOLD,    KEY_ACTION_UP,  KEY_ACTION_DOUBLE_CLICK,    KEY_ACTION_TRIPLE_CLICK,
    {KEY_PAGE_PREV,     KEY_NULL,           KEY_NULL,           KEY_NULL,       KEY_NULL,                   KEY_NULL},
    {KEY_PAGE_ENTER,    KEY_NULL,           KEY_NULL,           KEY_NULL,       KEY_PAGE_BACK,              KEY_NULL},
    {KEY_PAGE_NEXT,     KEY_NULL,           KEY_NULL,           KEY_NULL,       KEY_NULL,                   KEY_NULL},
};

/* 组合按键 remap 后的键值可以超出 iokey_table 的行数(见 iokey.c 的
 * mult_iokey_remap_table), 不做范围检查会越界读常量区 */
int get_key_event_deal(int key_val, int key_action)
{
    if ((key_val < 0) || (key_val >= (int)ARRAY_SIZE(iokey_table))) {
        return KEY_NULL;
    }
    if ((key_action <= 0) || (key_action >= (int)ARRAY_SIZE(iokey_table[0]))) {
        return KEY_NULL;
    }
    return iokey_table[key_val][key_action - 1];
}

extern const struct key_driver_ops iokey_ops;
void iokey_scan_timer(void *priv)
{
    Key_Scan((struct key_driver_ops *)(&iokey_ops));
}

int app_key_event_handle(int key_val, int key_action)
{
    int ret = -1;
    /* 用 int 而非 u8: KEY_NULL = 0xFFFF, 截成 u8 会变成 0xFF 落进 default 之外的键值 */
    int key_event = get_key_event_deal(key_val, key_action);
    log_debug("key_val: %d, key_action: %d, key event: %d\n", key_val, key_action, key_event);
    switch (key_event) {

    case KEY_PAGE_PREV:
        log_debug("KEY_PAGE_PREV\n");

        /* code */
        break;
    case KEY_PAGE_ENTER:
        log_debug("KEY_PAGE_ENTER\n");

        /* code */
        break;
    case KEY_PAGE_NEXT:
        log_debug("KEY_PAGE_NEXT\n");

        /* code */
        break;

    default:
        break;
    }

    return ret;
}


static u32 key_scan_timer_id = 0;
void app_key_init(void)
{
    Key_Init();
    u32 time = iokey_ops.param->scan_time;
    key_scan_timer_id = sys_timer_add(NULL, iokey_scan_timer, time);
}

void app_key_uinit(void)
{
    if (key_scan_timer_id) {
        sys_timer_del(key_scan_timer_id);
    }
}

static void app_core_function(void *priv)
{
    (void)priv;

    /* USB 设备栈在 main() 里已由 MX_USB_DEVICE_Init() 初始化, 此处不重复 */
    // iis_tx_test();
    app_key_init();
    app_led_init();

    int msg[16];
    while (1)
    {
        /* Q_CALLBACK 类型的消息已经在 os_taskq_pend 内部执行掉了,
         * 这里拿到的只会是 Q_MSG/Q_EVENT/Q_USER */
        if (os_taskq_pend(msg, sizeof(msg), portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (Q_TYPE(msg[0]) != Q_MSG) {
            continue;
        }
        //log_debug("msg :%d, key event: %d, key val %d\n", msg[1], msg[2], msg[3]);

        switch (msg[1]) {
        case APP_MSG_SYS_EVENT:
            log_debug("APP_MSG_SYS_EVENT\n");
            /* code */
            break;
        case APP_MSG_KEY_EVENT:
            log_debug("APP_MSG_KEY_EVENT\n");
            app_key_event_handle(msg[2], msg[3]);
            break;
        default:
            break;
        }

    }
}

void app_core_init(void)
{
    BaseType_t xReturn = task_create(app_core_function, NULL, "app_core");
    if (xReturn != pdPASS) {
        log_debug("create app_core failed\n");
    }
    log_debug("create app_core succ\n");
    os_start();
}
