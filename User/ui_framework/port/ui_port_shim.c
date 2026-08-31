/**
 * @file    ui_port_shim.c
 * @brief   杰理 SPI/GPIO/PWM 接口 -> ui_hal_* 的薄封装
 *
 * 框架的 platform/ui_pushScreen_manager.c 与 lcd_drive 下的屏驱里保留了
 * 原厂 spi_ / gpio_ 系列的调用写法(源码基本没动),
 * 由本文件把它们接到 hal/ui_hal.h。
 *
 * 本文件【不含任何芯片相关代码】—— 换 MCU 时只重写 hal/ui_hal_<mcu>.c。
 */
/* 打开本文件的分级日志。jl_debug.h 的 log_* 是靠这几个宏开关的,
 * 不定义就是空实现 —— port 层是上板排查的关键路径, 必须留着。 */
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE

#include "ui_port.h"
#include "ui_hal.h"

/* ==================================================================== *
 *  GPIO
 * ==================================================================== */

void gpio_set_mode(u32 pin, u32 unused, u32 value)
{
    (void)unused;   /* 原厂第二参是位掩码, 见 jl_lcd_drive.h 的 IO_PORT_SPILT */

    /* 框架里判"引脚未配置"有两种写法: `== NO_CONFIG_PORT`(-1) 和 `!= -1`,
     * 而 ui_port_config.h 给未接的脚填的是 UI_HAL_PIN_NONE(0)。
     * 两种值都要当成"没这个脚"处理, 否则会去操作一个不存在的引脚。 */
    if ((pin == (u32)UI_HAL_PIN_NONE) || (pin == (u32)NO_CONFIG_PORT)) {
        return;
    }

    ui_hal_pin_write((ui_hal_pin_t)pin, (uint8_t)value);
}

int gpio_read(u32 pin)
{
    (void)pin;
    /* 只有读 TE(撕裂同步)脚会用到, 本板没接 TE。
     * 返回 0 让框架当"TE 无效", 走不等 TE 的直推路径 */
    return 0;
}


/* ==================================================================== *
 *  SPI
 * ==================================================================== */

int spi_open(hw_spi_dev spi, const void *cfg)
{
    (void)spi;      /* SPI 实例固定在 ui_port_config.h */
    (void)cfg;

    return (int)ui_hal_spi_init();
}

const void *get_hw_spi_config(hw_spi_dev spi)
{
    (void)spi;
    /* spi_open 忽略该参数, 所以返回 NULL 是安全的 */
    return NULL;
}

int spi_send_byte(hw_spi_dev spi, u8 byte)
{
    (void)spi;

    return (int)ui_hal_spi_send_byte(byte);
}

int spi_dma_send(hw_spi_dev spi, const void *buf, u32 len)
{
    (void)spi;

    /* 同步版本: 发完才返回。OLED 推屏是按 page 逐块发的, 每块 128 字节,
     * 同步等待的开销可忽略, 换来的是调用方不用管 buf 生命周期 */
    return (int)ui_hal_spi_send_block((const uint8_t *)buf, len, 1);
}

int spi_dma_set_addr_for_isr(hw_spi_dev spi, const void *buf, u32 len, u8 rw)
{
    (void)spi;
    (void)rw;       /* 推屏只有写方向 */

    /* 【有意做成同步】: 本函数唯一的调用方是彩屏路径的 __spi_dma_send,
     * 它随后调的框架版 spi_dma_wait_finish() 在本移植里是空转(见上),
     * 真异步会让调用方在 DMA 还在搬运时就复用 buf。
     * OLED 路径不走这里, 所以同步带来的开销也无实际影响。 */
    return (int)ui_hal_spi_send_block((const uint8_t *)buf, len, 1);
}

/* spi_dma_wait_finish() 有意【不在这里实现】——
 * 框架 platform/ui_pushScreen_manager.c 自己定义了一份(靠 spi_get_pending
 * 轮询), 两边都定义会在链接期重复符号。
 *
 * 那份实现在本移植里等于空转(spi_get_pending 恒返回 1), 但这不影响正确性:
 * 真正的等待发生在下面两个发送函数内部, 它们都是同步返回的。 */

void spi_set_ie(hw_spi_dev spi, u8 enable)
{
    (void)spi;
    (void)enable;
    /* DMA 完成中断由 HAL 在 ui_hal_spi_init() 里配好并常开,
     * 不需要框架层再开关 */
}

int spi_get_pending(hw_spi_dev spi)
{
    (void)spi;
    /* 只被彩屏分支(lcd_spi_*)用到。返回 1 表示"传输已完成", 让那条
     * 路径上的忙等循环不会卡死 —— 本移植不走那条路径 */
    return 1;
}

void spi_clear_pending(hw_spi_dev spi)
{
    (void)spi;
}


/* ==================================================================== *
 *  背光 PWM
 *
 *  SSD1306 自发光无背光, TCFG_BACKLIGHT_PWM_MODE = 0 走纯 GPIO 分支,
 *  下面两个编译上要有, 运行时到不了。
 * ==================================================================== */

void mcpwm_init(struct mcpwm_config *arg)
{
    (void)arg;
}

void mcpwm_set_duty(u8 ch, u32 duty)
{
    (void)ch;
    (void)duty;
}
