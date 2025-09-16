#ifndef _IOKEY_H__
#define _IOKEY_H__

#include "typedef.h"
#include "key_driver.h"

enum connect_type {
    ONE_PORT_TO_LOW = 0, 		//按键一个端口接低电平, 另一个端口接IO
    ONE_PORT_TO_HIGH = 1,		//按键一个端口接高电平, 另一个端口接IO
};

struct iokey_info {
    enum key_value value;                   //键值
    u32 gpio_port;                      //GPIO
    u16 gpio_pin;                      //GPIO
    enum connect_type connect_way; //0 低电平按下, 1 高电平按下
};


#endif // !_IOKEY_H__
