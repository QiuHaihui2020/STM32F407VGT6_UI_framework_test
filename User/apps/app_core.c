#include "apps.h"
#include "typedef.h"
#include "task_manager.h"
#include "log_debug.h"
#include "key_driver.h"
#include "app_led.h"
#include "test.h"

/* 点阵屏 UI 框架。只用它的对外 API 头, 不碰内部实现。
 * @note 别在这里包含 jl_debug.h —— 它会用框架的分级日志覆盖本文件的
 *       log_debug, 而框架版在未定义 LOG_DEBUG_ENABLE 时是空实现。 */
#include "jl_ui_api.h"

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
    case KEY_PAGE_ENTER:
    case KEY_PAGE_NEXT:
    case KEY_PAGE_BACK:
        /* 交给 UI 框架: 它会投递到 ui 任务, 再按当前窗口分发给控件。
         * @note 这里传的是【应用层键值】(KEY_PAGE_*), 框架本身不解释键值,
         *       只是原样透传给风格层的 onkey 回调, 所以两边约定一致即可。 */
        ret = UI_KEY_MSG_POST(key_event);
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

/**
 * @brief 初始化点阵屏 UI 框架
 *
 * 必须在 os_start() 之后(即已经跑在任务里)调用 —— lcd_ui_init 内部会
 * task_create 出 "ui" 任务并等它的启动信号量。
 *
 * @note ui_cfg_data 是框架侧定义的板级配置实例(见 platform/lcd_ui_api.c),
 *       引脚取值来自 port/ui_port_config.h 的 TCFG_LCD_PIN_*。
 */
static void app_ui_init(void)
{
    /* 返回值就是内部 task_create 的结果, 杰理语义: 0 = 成功 */
    int err = UI_INIT((void *)&ui_cfg_data);

    if (err != OS_NO_ERR) {
        /* 常见原因: 1) 资源文件(0:/ui/JL/JL.res 等)还没放进 FATFS;
         *          2) OLED 没接好, 屏初始化命令发不出去。
         * 这里只报错不停机, 让其余功能照常跑。 */
        log_error("ui init failed: %d\n", err);
        return;
    }
    log_info("ui init succ\n");

    /* 显示首个窗口。ID 要与资源文件里的窗口编号对上, 见 port/ui_style.h */
    UI_SHOW_WINDOW(ID_WINDOW_BT);
}

static void app_core_function(void *priv)
{
    (void)priv;

    /* USB 设备栈在 main() 里已由 MX_USB_DEVICE_Init() 初始化, 此处不重复 */
    // iis_tx_test();
    app_key_init();
    app_led_init();
    app_ui_init();

    int msg[16];
    while (1)
    {
        /* Q_CALLBACK 类型的消息已经在 os_taskq_pend 内部执行掉了,
         * 这里拿到的只会是 Q_MSG/Q_EVENT/Q_USER */
        if (os_taskq_pend(msg, sizeof(msg), portMAX_DELAY) != OS_TASKQ) {
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
    int err = task_create(app_core_function, NULL, "app_core");
    if (err != OS_NO_ERR) {
        log_error("create app_core failed, err %d\n", err);
        return;                 /* 主任务建不出来, 再往下 os_start() 也没意义 */
    }
    log_debug("create app_core succ\n");
    os_start();
}
