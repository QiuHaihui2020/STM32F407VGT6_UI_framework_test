#include "iokey.h"
#include "main.h"
#include "key_driver.h"
#include "stm32f4xx_hal.h"

#include "typedef.h"

/*移植按键检测代码只要重新实现iokey_info_arg， get_io_key_value， iokey_init就可以了*/

//组合按键检测使能
#define MULT_KEY_ENABLE 1

#define IOKEY_NUM_MAX 1

//配置io按键
const struct iokey_info iokey_info_args[IOKEY_NUM_MAX] = {
    {KEY_IO_NUM0, (u32)KEY2_GPIO_Port, KEY2_Pin, ONE_PORT_TO_LOW},
};

static int get_io_key_value(u32 GPIO_Port, u32 GPIO_Pin)
{
    return HAL_GPIO_ReadPin((GPIO_TypeDef*)GPIO_Port, GPIO_Pin);
}

int iokey_init(void)
{
    return 0;
}

#if MULT_KEY_ENABLE
/* bit_mark 只有 32 位, 键值 >= 32 无法标记, 直接丢弃 */
#define MULT_KEY_BIT_MAX        32U

#define MARK_MULT_KEY_VALUE(b, v) \
do { \
    uint8_t tkey = (uint8_t)((v) & 0x7F); \
    if (tkey < MULT_KEY_BIT_MAX) { \
        (b) |= BIT(tkey); \
    } \
} while(0)


//组合按键映射按键值
struct key_remap {
    uint32_t bit_value;
    uint8_t remap_value;
};
struct key_remap_data {
    uint8_t remap_num;
    const struct key_remap *table;
};
//组合按键消息映射表
//配置注意事项:单个按键按键值需要按照顺序编号,如power:0, prev:1, next:2
//bit_value = BIT(0) | BIT(1) 指按键值为0和按键值为1的两个按键被同时按下,
//remap_value = 3指当这两个按键被同时按下后重新映射的按键值;
const struct key_remap mult_iokey_remap_table[] = {
	{.bit_value = BIT(0) | BIT(3), .remap_value = KEY_IO_NUM4},
	{.bit_value = BIT(1) | BIT(2), .remap_value = KEY_IO_NUM5},
};

const struct key_remap_data mult_iokey_remap_data = {
	.remap_num = ARRAY_SIZE(mult_iokey_remap_table),
	.table = mult_iokey_remap_table,
};

/* 形参必须是 32 位: bit_value 是 uint32_t 掩码, 用 u8 接会把键值 >= 8 的位截掉 */
static uint8_t mult_iokey_value_remap(uint32_t bit_mark)
{
    for (uint8_t i = 0; i < mult_iokey_remap_data.remap_num; i++) {
        if (mult_iokey_remap_data.table[i].bit_value == bit_mark) {
            return mult_iokey_remap_data.table[i].remap_value;
        }
    }

    return NO_KEY;
}
#endif

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

    uint8_t press_value = 0;
    enum connect_type connect_way;
    uint8_t pin_value = 0; //引脚电平
    enum key_value ret_value = NO_KEY;
    uint32_t bit_mark = 0;

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
#if MULT_KEY_ENABLE
            MARK_MULT_KEY_VALUE(bit_mark, ret_value);	//标记被按下的按键
#else
            goto _iokey_get_value_end;
#endif
        }
    }

#if MULT_KEY_ENABLE
    //组合按键重新映射按键值; 不在映射表里则保留单键值
    uint8_t remap_value = mult_iokey_value_remap(bit_mark);
    if (remap_value != NO_KEY) {
        ret_value = (enum key_value)remap_value;
    }
#else
_iokey_get_value_end:   /* 只有单键模式才会 goto 到这里 */
#endif
    return ret_value;
}

u8 get_iokey_pressed(enum key_value index)
{
    int i = index;
    /* 必须是 >=: 下标等于 IOKEY_NUM_MAX 时已经越过数组末尾 */
    if ((i < 0) || (i >= IOKEY_NUM_MAX)) {
        return 0;
    }

    u8 press_value = 0;
    enum connect_type connect_way;
    u8 pin_value = 0; //引脚电平
    enum key_value ret_value = NO_KEY;

    //判断按键连接方式
    connect_way = iokey_info_args[i].connect_way;
    if (connect_way == ONE_PORT_TO_HIGH) {
        press_value = 1; //高电平时按下
    } else if (connect_way == ONE_PORT_TO_LOW) {
        press_value = 0; //低电平是按下
    } else {
        return 0;
    }

    //读取引脚电平
    pin_value = get_io_key_value(iokey_info_args[i].gpio_port, iokey_info_args[i].gpio_pin);

    //判断按键是否按下
    if (pin_value == press_value) {
        ret_value = iokey_info_args[i].value;
        goto _iokey_get_value_end;
    }

_iokey_get_value_end:
    if (ret_value == NO_KEY) {
        return 0;
    } else {
        return 1;
    }
    
}


const struct key_driver_ops iokey_ops = {
    .param            = &iokey_scan_para,
    .get_value 	      = get_iokey_value,
    .key_init         = iokey_init,
};
