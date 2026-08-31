/**
 * @file    ui_port.h
 * @brief   硬件桥接入口 —— 把框架里的杰理 SPI/GPIO 调用转成 ui_hal_* 调用
 *
 * 框架的 platform/ui_pushScreen_manager.c 与 lcd_drive 下的屏驱原本 include
 * 杰理的 spi.h / gpio.h / includes.h / asm/mcpwm.h, 现在统一改成 include
 * 本文件。
 *
 * 这些函数【全部是薄封装】, 定义在 port/ui_port_shim.c, 内部直接转调
 * hal/ui_hal.h。换 MCU 时只需重写 hal/ui_hal_<mcu>.c, 本文件不用动。
 */
#ifndef __UI_PORT_H__
#define __UI_PORT_H__

#include "ui_port_config.h"
#include "jl_typedef.h"
#include "jl_os_api.h"
#include "jl_debug.h"
#include "jl_lcd_drive.h"
#include "ui_hal.h"

/* ==================================================================== *
 *  GPIO
 * ==================================================================== */

/**
 * @brief 设置引脚电平
 * @param pin   抽象引脚号(ui_hal_pin_t)。框架从 lcd_platform_data 里取,
 *              值由 ui_port_config.h 的 TCFG_LCD_PIN_* 给出
 * @param unused 原厂第二参数是位掩码, 本移植不用 —— 见 IO_PORT_SPILT
 * @param value 0 = 低, 非 0 = 高
 * @note 未配置的引脚(UI_HAL_PIN_NONE 或 -1)静默忽略。框架里有些地方拿
 *       `!= -1` 判断、有些拿 `!= NO_CONFIG_PORT`, 两种都能被兜住。
 */
void gpio_set_mode(u32 pin, u32 unused, u32 value);

/** 读引脚电平。本移植无 TE 脚, 恒返回 0 */
int gpio_read(u32 pin);


/* ==================================================================== *
 *  SPI
 * ==================================================================== */

/**
 * @brief 打开 SPI 控制器
 * @param spi  控制器编号, 本移植忽略(实例固定在 ui_port_config.h)
 * @param cfg  板级配置, 本移植忽略
 * @return 0 成功, 负值失败
 */
int spi_open(hw_spi_dev spi, const void *cfg);

/** 取板级 SPI 配置。本移植返回 NULL, spi_open 不使用该参数 */
const void *get_hw_spi_config(hw_spi_dev spi);

/** 同步发单字节 */
int spi_send_byte(hw_spi_dev spi, u8 byte);

/** 批量发送(走 DMA), 发完才返回 */
int spi_dma_send(hw_spi_dev spi, const void *buf, u32 len);

/** 异步批量发送: 挂上 DMA 就返回, 由 spi_dma_wait_finish 等待 */
int spi_dma_set_addr_for_isr(hw_spi_dev spi, const void *buf, u32 len, u8 rw);

/** 等待异步传输结束 */
void spi_dma_wait_finish(void);

/* 中断使能 / 标志。本移植的 DMA 完成中断由 HAL 自己管, 这几个是空实现,
 * 留着是为了让框架的彩屏分支代码也能编过 */
void spi_set_ie(hw_spi_dev spi, u8 enable);
int  spi_get_pending(hw_spi_dev spi);
void spi_clear_pending(hw_spi_dev spi);


/* ==================================================================== *
 *  背光 PWM (原 asm/mcpwm.h)
 *
 *  SSD1306 自发光无背光, 本移植 TCFG_BACKLIGHT_PWM_MODE=0 走纯 GPIO,
 *  下面这些只为让框架的 PWM 分支能编过。
 * ==================================================================== */

#define MCPWM_CH0               0
#define MCPWM_EDGE_ALIGNED      0
#define MCPWM_EDGE_DEFAULT      0

struct mcpwm_config {
    u8  aligned_mode;
    u8  ch;
    u32 frequency;
    u32 duty;
    u32 h_pin;
    int l_pin;
    u8  complementary_en;
    int detect_port;
    u8  edge;
    void (*irq_cb)(void);
    u8  irq_priority;
    u8  pwm_ch_num;
};

void mcpwm_init(struct mcpwm_config *arg);
void mcpwm_set_duty(u8 ch, u32 duty);

#endif /* __UI_PORT_H__ */
