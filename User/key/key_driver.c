#include "key_driver.h"
#include "log_debug.h"
#include "apps.h"

struct key_event global_key = {0};

/* --------------------------------------------------------------------------*/
/**
 * @brief 多击按键判断
 *
 * @param key：基础按键动作（mono_click、long、hold、up）和键值
 *
 * @return 0：不拦截按键事件
 *         1：拦截按键事件
 */
/* ----------------------------------------------------------------------------*/
static int multi_clicks_analyze(struct key_event *key)
{
    static u8 click_cnt;
    static u8 host_cnt;
    static enum key_value notify_value = NO_KEY;

    // 判断长按和按住多少秒
    if (key->event == KEY_ACTION_LONG)
    {
        host_cnt = 1;
    }
    else if (key->event == KEY_ACTION_HOLD)
    {
        host_cnt++;
        // log_debug("host_cnt=%d\n", host_cnt);
        // 长按时间大的判断要放在前面
        if (host_cnt * key->scan_hold_time >= 5000)
        {
            key->event = KEY_ACTION_HOLD_5SEC;
            host_cnt = 5000 / key->scan_hold_time + 1; // 防止host_cnt一直增加溢出
        }
        else if (host_cnt * key->scan_hold_time >= 3000)
        {
            key->event = KEY_ACTION_HOLD_3SEC;
        }
    }
    else
    {
        host_cnt = 0;
    }

    // 判断单击/多击
    if (key->event == KEY_ACTION_CLICK)
    {

        if (key->value != notify_value)
        {
            click_cnt = 1;
            notify_value = key->value;
        }
        else
        {
            click_cnt++;
        }
        return 1;
    }
    if (key->event == KEY_ACTION_NO_KEY)
    {
        if (click_cnt >= 2)
        {

            if (click_cnt > MULTI_CLICK_MAX_CNT)
            {
                click_cnt = MULTI_CLICK_MAX_CNT; // 限制多击按键最大连击次数，避险出现连击次数超过按键表定义的事件导致发到其他按键消息或者引发内存越界访问
            }
            key->event = KEY_ACTION_DOUBLE_CLICK + (click_cnt - 2);
        }
        else
        {
            key->event = KEY_ACTION_CLICK; // 短按一次抬起超时后，认为单击
        }
        key->value = notify_value;
        click_cnt = 0;
        notify_value = NO_KEY;
    }
    else if (key->event > KEY_ACTION_CLICK)
    {
        click_cnt = 0; // 非短按时间，计数清零
        notify_value = NO_KEY;
    }
    return 0;
}
static int combination_key_analyze(struct key_event *key)
{
    return 0;
}

void key_event_handler(struct key_event *key)
{

    if (multi_clicks_analyze(key))
    {
        return;
    }
    else if (combination_key_analyze(key))
    {
        return;
    }

    // if (key->event == KEY_ACTION_NO_KEY) {
    //     key->value = NO_KEY;
    // }

    global_key.event = key->event;
    global_key.value = key->value;
    //log_debug("key event:%d, value:%d\n", key->event, key->value);
    /* 队列满时消息会丢, 按键响应会"吞键", 必须报出来而不是静默忽略 */
    if (app_msg_post(2, key->value, key->event) != pdPASS) {
        log_error("app_msg_post fail, key %d event %d\n", key->value, key->event);
    }
}

void Key_Init(void)
{

}

struct key_event Key_Scan(struct key_driver_ops *key_ops)
{
    struct key_driver_ops *key_handler = (struct key_driver_ops *)key_ops;
    struct key_driver_para *scan_para = (struct key_driver_para *)key_handler->param;

    enum key_action key_event = KEY_ACTION_NO_KEY;
    enum key_value cur_key_value = NO_KEY;
    enum key_value key_value = NO_KEY;
    struct key_event key = {0};

    key.init = 1;
    // 区分按键类型
    key.type = scan_para->key_type;
    cur_key_value = key_handler->get_value();

    //===== 按键消抖处理
    // 当前按键值与上一次按键值如果不相等, 重新消抖处理, 注意filter_time != 0;
    // log_info("cur_key_value:%d, last_value:%d, filter_time:%d\n", cur_key_value, scan_para->filter_last_value, scan_para->filter_time);
    if (cur_key_value != scan_para->filter_last_value && scan_para->filter_time)
    {
        // 消抖次数清0, 重新开始消抖
        scan_para->filter_cnt = 0;
        // 记录上一次的按键值
        scan_para->filter_last_value = cur_key_value;
        // 第一次检测, 返回不做处理
        return key;
    }
    // 当前按键值与上一次按键值相等, filter_cnt开始累加
    if (scan_para->filter_cnt < scan_para->filter_time)
    {
        scan_para->filter_cnt++;
        return key;
    }
    //===== 按键消抖结束, 开始判断按键类型(单击, 双击, 长按, 多击, HOLD, (长按/HOLD)抬起)

    if (cur_key_value != scan_para->last_key)
    {
        if (cur_key_value != NO_KEY)
        {
            // 当前按键值与上一次按键值不相等, 且当前按键值不为NO_KEY, 说明按键被按下
            scan_para->press_cnt = 1; // 用于判断long和hold事件的计数器重新开始计时;
            scan_para->press_sum_cnt = 1;
            scan_para->click_delay_cnt = 0;
            // key_down_event_handler(cur_key_value);
        }
        else
        {
            // 当前按键值与上一次按键值不相等, 且当前按键值为NO_KEY, 说明按键被抬起
            if (scan_para->press_cnt >= scan_para->long_time)
            {
                // 长按/HOLD状态之后被按键抬起;
                key_event = KEY_ACTION_UP;
                key_value = scan_para->last_key;
                goto __notify;
            }
            else
            {
                // 单击/双击状态之后被按键抬起;
                key_event = KEY_ACTION_CLICK;
                key_value = scan_para->last_key;
                scan_para->click_delay_cnt = 1; // 开始延时计数, 用于判断双击事件;
                goto __notify;
            }
        }
        // 返回, 等待延时时间到
        goto __scan_end;
    }
    else
    {
        // cur_key = last_key -> 没有按键按下/按键长按(HOLD)
        if (cur_key_value == NO_KEY)
        {
            // 当前按键值与上一次按键值相等, 且当前按键值为NO_KEY, 说明按键没有被按下
            if (scan_para->click_delay_cnt > 0)
            {
                scan_para->click_delay_cnt++;
                if (scan_para->click_delay_cnt > scan_para->click_delay_time)
                {
                    // 超过双击间隔时间，清除标志
                    key_event = KEY_ACTION_NO_KEY;
                    scan_para->click_delay_cnt = 0;
                    goto __notify; // 有按键需要消息需要处理
                }
            }
            goto __scan_end; // 没有按键需要处理
        }
        else
        {
            // 当前按键值与上一次按键值相等, 且当前按键值不为NO_KEY, 说明按键被按住
            // last_key = valid_key; cur_key = valid_key, press_cnt累加用于判断long和hold
            scan_para->press_cnt++; // 按键被按住的时候，开始累计次数
            if (scan_para->press_sum_cnt)
            {
                scan_para->press_sum_cnt++;
            }
            if (scan_para->press_cnt == scan_para->long_time)
            {
                key_event = KEY_ACTION_LONG;
            }
            else if (scan_para->press_cnt >= scan_para->hold_time)
            {
                key_event = KEY_ACTION_HOLD;
                scan_para->press_cnt = scan_para->long_time; // 避免一直按住计数溢出
            }
            else
            {
                goto __scan_end; // press_cnt没到长按和HOLD次数, 返回
            }
            key_value = cur_key_value;
            goto __notify;
        }
    }

__notify:
    // log_info("key_value: 0x%x, event: %d\n", key_value, key_event);
    key.event = key_event;
    key.value = key_value;
    key.scan_hold_time = (scan_para->hold_time - scan_para->long_time) * scan_para->scan_time;
    /* printf("key_value: 0x%x, event: %d\n", key_value, key_event); */
    key_event_handler(&key);
__scan_end:
    scan_para->last_key = cur_key_value;
    return key;
}
