#include "iokey.h"
#include "key_driver.h"
#include "stm32f4xx_hal.h"
#include "main.h"

/*移植按键检测代码只要重新实现iokey_info_arg， get_io_key_value， iokey_init就可以了*/

#define IOKEY_NUM_MAX 3

//配置io按键
const struct iokey_info iokey_info_args[IOKEY_NUM_MAX] = {
    {KEY_IO_NUM0, (u32)KEY2_GPIO_Port, KEY2_Pin, ONE_PORT_TO_HIGH},
    {KEY_IO_NUM1, (u32)KEY3_GPIO_Port, KEY3_Pin, ONE_PORT_TO_LOW},
    {KEY_IO_NUM2, (u32)KEY4_GPIO_Port, KEY4_Pin, ONE_PORT_TO_LOW},
};

static int get_io_key_value(u32 GPIO_Port, u16 GPIO_Pin)
{
    return HAL_GPIO_ReadPin((GPIO_TypeDef*)GPIO_Port, GPIO_Pin);
}

int iokey_init() 
{
    return 0;
}

//按键驱动扫描参数列表
struct key_driver_para iokey_scan_para = {
    .last_key 		  = NO_KEY,  		//上一次get_value按键值, 初始化为NO_KEY;

    .key_type		  = KEY_DRIVER_TYPE_IO,
    .filter_time  	  = 4,				//按键消抖延时间：scan_time * filter_time 毫秒
    .long_time 		  = 75,  			//按键判定长按数量：scan_time * long_time 毫秒
    .hold_time 		  = (75 + 15),  	//按键判定HOLD数量：scan_time * hold_time 毫秒
    .click_delay_time = 20,				//按键被抬起后等待连击延时数量：scan_time * click_delay_time 毫秒
    .scan_time 	  	  = 10,				//按键扫描频率, 单位: ms
};


enum key_value get_iokey_value(void)
{
    int i;

    u8 press_value = 0;
    enum connect_type connect_way;
    u8 pin_value = 0; //引脚电平
    enum key_value ret_value = NO_KEY;

    for (i = 0; i < IOKEY_NUM_MAX; i++) {
        //判断按键连接方式
        connect_way = iokey_info_args[i].connect_way;
        if (connect_way == ONE_PORT_TO_HIGH) {
            press_value = 1; //高电平时按下
        } else if (connect_way == ONE_PORT_TO_LOW) {
            press_value = 0; //低电平是按下
        } else {
            continue;
        }

        //读取引脚电平
        pin_value = get_io_key_value(iokey_info_args[i].gpio_port, iokey_info_args[i].gpio_pin);

        //判断按键是否按下
        if (pin_value == press_value) {
            ret_value = iokey_info_args[i].value;
            goto _iokey_get_value_end;
        }
    }

_iokey_get_value_end:
    return ret_value;
}


const struct key_driver_ops iokey_ops = {
    .param            = &iokey_scan_para,
    .get_value 	      = get_iokey_value,
    .key_init         = iokey_init,
};
