/**
 * @file    ui_board_stm32f4.h
 * @brief   板级外设资源 —— 用 MCU 的哪套 SPI / DMA / 中断
 *
 * 本文件【含 STM32 HAL 符号】(DMA1_Stream5 / DMA_CHANNEL_0 ...), 所以
 * 只允许 port/hal/ui_hal_stm32f4.c 一个文件 include。框架层若引到它,
 * 就等于让 MCU 符号漏进了 MCU 无关的代码 —— 换芯片时会编不过。
 *
 * 换 MCU 时: 本文件整份重写(连文件名一起), 引脚表 ui_board_pins.h
 * 只需改 token 值。
 */
#ifndef __UI_BOARD_STM32F4_H__
#define __UI_BOARD_STM32F4_H__

/* ==================================================================== *
 *  SPI 实例
 *
 *  句柄由 CubeMX 生成的 Core/Src/spi.c 提供, 已在 main() 里 MX_SPI3_Init()。
 *  本层不重复初始化, 只补 DMA 通路 —— 避免和 CubeMX 重新生成时打架。
 *
 *  ⚠ 换用别的 SPI 时【三处一起改】: BUS_ID / HANDLE / 下面的 DMA 通路。
 *    原先 BUS_ID 这个值是装饰品(配置里写 3, HAL 里硬编码 hspi3),
 *    改 SPI 得去动 HAL 源码。现在三者都在本文件里。
 * ==================================================================== */

#define UI_BOARD_SPI_BUS_ID         3
#define UI_BOARD_SPI_HANDLE         hspi3
#define UI_BOARD_SPI_INSTANCE       SPI3

/* ==================================================================== *
 *  SPI_TX 的 DMA 通路
 *
 *  DMA1_Stream3/4 已被 I2S2 占用, Stream5 空闲。
 *  查《RM0090》Table 42: SPI3_TX = DMA1 Stream5 Ch0 或 Stream7 Ch0
 *
 *  ⚠ IRQHandler 名也在这里给出: 它定义在 ui_hal_stm32f4.c, 靠启动文件的
 *    弱符号接住。若哪天 CubeMX 勾上了 SPI3 的 DMA, Core/Src/stm32f4xx_it.c
 *    里会生成同名函数 —— 那时是【重复定义链接错误】, 不是静默故障,
 *    删掉本层的那个定义即可。
 * ==================================================================== */

#define UI_BOARD_SPI_DMA_STREAM     DMA1_Stream5
#define UI_BOARD_SPI_DMA_CHANNEL    DMA_CHANNEL_0
#define UI_BOARD_SPI_DMA_IRQn       DMA1_Stream5_IRQn
#define UI_BOARD_SPI_DMA_IRQHandler DMA1_Stream5_IRQHandler

/** DMA ISR 的抢占优先级。数值必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 *  (即优先级更低), 否则 ISR 里不能调 FreeRTOS API。本 ISR 只写一个 volatile
 *  标志不调 OS, 但仍留余量避免干扰 I2S 音频流 */
#define UI_BOARD_SPI_DMA_IRQ_PRIO   6

/** 一帧的超时保护(ms)。128 字节/page @ SPI 21MHz 约 50us, 整屏 8 page
 *  也远小于 10ms, 取 100ms 足够宽松 */
#define UI_BOARD_SPI_TIMEOUT_MS     100

/* ==================================================================== *
 *  GPIO 端口
 *
 *  与 ui_board_pins.h 里 UI_BOARD_PORT_A..E 的索引【一一对应】。
 *  F407VGT6(LQFP100) 只引出 GPIOA..GPIOE, 没有 PF/PG。
 * ==================================================================== */

#define UI_BOARD_GPIO_PORT_TABLE    { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE }
#define UI_BOARD_GPIO_PORT_NUM      5

#endif /* __UI_BOARD_STM32F4_H__ */
