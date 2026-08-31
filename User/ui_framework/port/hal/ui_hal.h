/**
 * @file    ui_hal.h
 * @brief   点阵屏 UI 框架 —— MCU 无关硬件抽象层(HAL)接口
 *
 * 这是整个 UI 框架与具体 MCU 之间【唯一】的硬件边界。
 *
 * 移植到新 MCU 时:
 *   1. 复制 ui_hal_stm32f4.c 为 ui_hal_<你的芯片>.c
 *   2. 实现下面全部 ui_hal_* 函数
 *   3. 在 ui_port_config.h 里改引脚编号与屏参
 *   除此之外框架源码、compat/、port/ 其余文件【都不需要动】。
 *
 * @note 为什么不用回调结构体注册: 本层是单实例(一块屏一条 SPI),
 *       直接函数调用可让编译器内联, 且避免了 LTO 下函数指针同一性的坑。
 */
#ifndef __UI_HAL_H__
#define __UI_HAL_H__

#include <stdint.h>

/** 引脚抽象编号。具体值由 ui_hal_<mcu>.c 自己解释,
 *  框架层只透传 ui_port_config.h 里配的这几个常量 */
typedef enum {
    UI_HAL_PIN_NONE = 0,    /**< 该引脚未使用 */
    UI_HAL_PIN_LCD_CS,      /**< 片选,       低有效 */
    UI_HAL_PIN_LCD_DC,      /**< 命令/数据选择, 低=命令 高=数据 */
    UI_HAL_PIN_LCD_RST,     /**< 复位,       低有效 */
    UI_HAL_PIN_LCD_BL,      /**< 背光/使能 */
} ui_hal_pin_t;

/**
 * @brief 初始化推屏用的 SPI 与全部控制引脚
 * @return 0 成功, 负值失败
 * @note 由 UI 任务上下文调用一次。需保证返回后即可 send_byte/send_block。
 */
int32_t ui_hal_spi_init(void);

/**
 * @brief 同步发送单字节(发面板命令用, 数据量极小)
 * @param byte 待发送字节
 * @return 0 成功, 负值失败(超时)
 */
int32_t ui_hal_spi_send_byte(uint8_t byte);

/**
 * @brief 批量发送显存(推屏主通道, 实现里应尽量走 DMA)
 * @param buf 数据首地址
 * @param len 字节数
 * @param is_wait 非 0 = 发完才返回; 0 = 允许后台 DMA 搬运, 由
 *                ui_hal_spi_wait_done() 等待
 * @return 0 成功, 负值失败(超时)
 * @note 传入 buf 在 DMA 完成前必须保持有效。异步模式下调用方
 *       (ui_pushScreen_manager) 负责在复用该 buf 前先 wait_done。
 */
int32_t ui_hal_spi_send_block(const uint8_t *buf, uint32_t len, uint8_t is_wait);

/**
 * @brief 等待上一次异步 send_block 完成
 * @note 无未完成传输时必须立即返回。内部需喂狗, 因为整屏推送耗时较长。
 */
void ui_hal_spi_wait_done(void);

/**
 * @brief 设置引脚电平
 * @param pin   ui_hal_pin_t 之一
 * @param level 0 = 低, 非 0 = 高
 * @note 传入 UI_HAL_PIN_NONE 或未配置的引脚时必须静默返回, 不可断言。
 */
void ui_hal_pin_write(ui_hal_pin_t pin, uint8_t level);

/**
 * @brief 数据同步屏障
 * @note 配置完 DMA 寄存器、或写完要给 DMA 读的显存之后调用, 确保写已生效。
 *       没有写缓冲 / 乱序问题的平台可以留空。
 */
void ui_hal_memory_barrier(void);


/* ==================================================================== *
 *  以下这些【有意不在本接口里】, 因为它们不是"显示外设"而是系统服务,
 *  统一由工程的 OS 封装层 FreeRTOS/task_manager.{h,c} 提供:
 *
 *    关中断 / 临界区   local_irq_disable / local_irq_enable / spin_lock
 *    中断状态查询      cpu_in_irq / __cpu_irq_disabled
 *    延时              os_time_dly / delay_2ms
 *    喂狗              wdt_clear / wdt_clr
 *    清零分配          zalloc
 *
 *  这样换 MCU 时只需重写本文件的实现, 不会顺带把 OS 相关代码也拖过去;
 *  换 RTOS 时反过来也只动 task_manager。
 * ==================================================================== */

#endif /* __UI_HAL_H__ */
