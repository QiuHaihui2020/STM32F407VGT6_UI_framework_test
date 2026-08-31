#ifndef _IOKEY_H__
#define _IOKEY_H__

#include "typedef.h"
#include "key_driver.h"

//按键按下时io的电平
enum connect_type {
    ONE_PORT_TO_LOW = 0, 		//按键一个端口接低电平, 另一个端口接IO
    ONE_PORT_TO_HIGH = 1,		//按键一个端口接高电平, 另一个端口接IO
};

struct iokey_info {
    enum key_value value;                   //键值
    u32 gpio_port;                      //GPIO
    u32 gpio_pin;                      //GPIO
    enum connect_type connect_way; //按下时io的电平，0 低电平按下, 1 高电平按下
};

u8 get_iokey_pressed(enum key_value index);

#endif // !_IOKEY_H__
