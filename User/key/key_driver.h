#ifndef  __KEY_H__
#define  __KEY_H__
#include "typedef.h"

//#define NO_KEY 		     0xff

/*最大多击次数限制, 目前默认支持按键三击，如果需要更大的按键连击次数，改动此处需要同步修改*/
/*对应模式的key_msg_table.c的按键消息表,增加对应事件的消息映射*/
#define MULTI_CLICK_MAX_CNT     3

enum key_value {
    KEY_IO_NUM0  = 0,
    KEY_IO_NUM1 ,
    KEY_IO_NUM2 ,
    KEY_IO_NUM3 ,
    KEY_IO_NUM4 ,
    KEY_IO_NUM5 ,
    KEY_IO_NUM6 ,
    KEY_IO_NUM7 ,
    KEY_IO_NUM8 ,
    KEY_IO_NUM9 ,
    KEY_IO_NUM10,
    KEY_IO_NUM11,
    KEY_IO_NUM12,
    KEY_IO_NUM13,
    KEY_IO_NUM14,

    KEY_AD_NUM0 ,
    KEY_AD_NUM1 ,
    KEY_AD_NUM2 ,
    KEY_AD_NUM3 ,
    KEY_AD_NUM4 ,
    KEY_AD_NUM5 ,
    KEY_AD_NUM6 ,
    KEY_AD_NUM7 ,
    KEY_AD_NUM8 ,
    KEY_AD_NUM9 ,
    KEY_AD_NUM10,
    KEY_AD_NUM11,
    KEY_AD_NUM12,
    KEY_AD_NUM13,
    KEY_AD_NUM14,
    KEY_AD_NUM15,
    KEY_AD_NUM16,
    KEY_AD_NUM17,
    KEY_AD_NUM18,
    KEY_AD_NUM19,

    NO_KEY = 0xFF,
};

typedef enum __KEY_DRIVER_TYPE {
    KEY_DRIVER_TYPE_IO = 0x0,
    KEY_DRIVER_TYPE_AD,
    KEY_DRIVER_TYPE_RTCVDD_AD,
    KEY_DRIVER_TYPE_IR,
    KEY_DRIVER_TYPE_TOUCH,
    KEY_DRIVER_TYPE_CTMU_TOUCH,
    KEY_DRIVER_TYPE_RDEC,
    KEY_DRIVER_TYPE_SLIDEKEY,
    KEY_DRIVER_TYPE_SOFTKEY,
    KEY_DRIVER_TYPE_BRIGHTNESS,
    KEY_DRIVER_TYPE_VOICE,

    KEY_DRIVER_TYPE_MAX,
} KEY_DRIVER_TYPE;

enum key_action {
    KEY_ACTION_CLICK,//短按抬起/单击
    KEY_ACTION_LONG,//长按
    KEY_ACTION_HOLD,//长按按着
    KEY_ACTION_UP,//长按抬起
    KEY_ACTION_DOUBLE_CLICK,
    KEY_ACTION_TRIPLE_CLICK,
    KEY_ACTION_FOURTH_CLICK,
    KEY_ACTION_FIRTH_CLICK,
    KEY_ACTION_HOLD_3SEC,
    KEY_ACTION_HOLD_5SEC,
    /*=======新增按键动作请在此处之上增加=======*/
    KEY_ACTION_NO_KEY, //按键抬起超时没有按键动作
    KEY_ACTION_MAX,
};


enum key_event_type {
    KEY_CLICK_EVENT,
    KEY_COMB_EVENT,
};

struct key_driver_ops {
    const u8 idle_query_en;
    const struct key_driver_para *param;
    enum key_value (*get_value)(void);
    int (*key_init)(void);
};

struct key_event {
    u8 init;
    KEY_DRIVER_TYPE type;
    enum key_action event;
    enum key_value value;
    u32 scan_hold_time;	//hold识别间隔, 计算长按时间使用
};

struct key_driver_para {
    enum key_value last_key;  			//上一次get_value按键值
//== 用于消抖类参数
    enum key_value filter_last_value; 		//用于按键消抖
    u8 filter_cnt;  		//用于按键消抖时的累加值
//== 用于判定连击事件参数
    u8 click_delay_cnt;  	//按键被抬起后等待连击事件延时计数
    u8 press_cnt;  		 	//与long_time和hold_time对比, 判断long_event和hold_event
    u16 press_sum_cnt;
    const KEY_DRIVER_TYPE key_type;
    const u8 filter_time;	//当filter_cnt累加到base_cnt值时, 消抖有效
    const u8 long_time;  	//按键判定长按数量
    const u8 hold_time;  	//按键判定HOLD数量
    const u8 click_delay_time;	//按键被抬起后等待连击事件延时数量
    const u32 scan_time;	//按键扫描频率, 单位ms
};

void Key_Init(void);
void Key_Scan(struct key_driver_ops *key_ops);

#endif // ! __KEY_H__


