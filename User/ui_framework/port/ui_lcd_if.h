/**
 * @file    ui_lcd_if.h
 * @brief   UI 框架 <-> 屏硬件的【唯一边界】
 *
 * 分两组, 正好对应屏的两类引脚:
 *
 *   数据通道   CLK / DI 由 SPI 控制器自己驱动, 框架只管"发这一串字节"
 *              -> ui_lcd_write_byte / ui_lcd_write_block / ui_lcd_wait_done
 *
 *   控制线     CS / DC / RST / BL / EN / TE 是普通 GPIO, 框架只管
 *              "把这条线拉高/拉低"
 *              -> ui_lcd_cs / dc / rst / bl / power / te_read
 *
 * 【框架不知道任何引脚】: 没有引脚号、没有端口号、没有引脚句柄类型, 也没有
 * SPI 控制器编号。"CS 接在 PB6" 这件事只写在 port/board/ui_board_pins.h 里,
 * 只有 port/lcd/ui_lcd_<mcu>.c 读得到它。所以改接线、换 SPI、换 MCU 都
 * 波及不到框架的任何一行。
 *
 * 本文件里不允许出现芯片厂商符号(GPIOB / DMA1_Stream5 / hspi3 之类)。
 *
 * 移植到新 MCU:
 *   1. 复制 lcd/ui_lcd_stm32f4.c 为 lcd/ui_lcd_<你的芯片>.c, 实现下面全部函数
 *   2. 复制 board/ui_board_stm32f4.h 为 board/ui_board_<你的芯片>.h, 填外设实例
 *   3. 在 board/ui_board_pins.h 里改接线
 *   框架源码、compat/、port/ 其余文件【一个字都不用动】。
 *
 * @note 为什么是一组直接函数而不是回调结构体: 本层是单实例(一块屏一条
 *       总线), 直接调用可让编译器内联, 且避开了 LTO 下函数指针同一性的坑。
 */
#ifndef __UI_LCD_IF_H__
#define __UI_LCD_IF_H__

#include <stdint.h>

/* ==================================================================== *
 *  初始化
 * ==================================================================== */

/**
 * @brief 建立屏用到的全部硬件: 控制线 + SPI + DMA
 * @return 0 成功, 负值失败
 * @note 幂等 —— 重复调用直接返回 0。由 UI 任务上下文调用。
 *       返回后即可调用本文件其余任何函数; 返回前所有控制线已被置到安全的
 *       空闲电平(CS 未选中、RST 不复位)。
 */
int32_t ui_lcd_init(void);


/* ==================================================================== *
 *  控制线 —— 拉高 / 拉低
 *
 *  参数 level 一律 0 = 低电平, 非 0 = 高电平。
 *  【不是】逻辑有效/无效 —— 片选是低有效、DC 低表示命令, 各条线极性不同,
 *  由屏驱决定, 本层不做解释。
 *
 *  本板没接的线(多数板子的 BL / EN / TE)必须【静默忽略】, 不可断言 ——
 *  框架会无条件调用它们。
 * ==================================================================== */

/** 片选。0 = 选中(低有效) */
void ui_lcd_cs(uint8_t level);

/** 命令/数据选择。0 = 命令, 1 = 数据 */
void ui_lcd_dc(uint8_t level);

/** 复位。0 = 复位中(低有效) */
void ui_lcd_rst(uint8_t level);

/** 背光。0 = 灭。无背光脚的屏(如 SSD1306 自发光)是空操作 */
void ui_lcd_bl(uint8_t level);

/**
 * @brief 本板能不能控背光
 * @return 0 = 不能(没接背光脚), 非 0 = 能
 * @note 这是【设备能力】而不是引脚 —— 框架需要区分"背光已设好"与
 *       "这块屏没背光可调", 后者要给上层回一个失败。具体接在哪个
 *       引脚仍然只有 port 层知道。实现侧应是编译期常量, 可被完全优化掉。
 */
int32_t ui_lcd_has_backlight(void);

/** 屏供电使能。0 = 断电。没接使能脚时是空操作 */
void ui_lcd_power(uint8_t level);

/**
 * @brief 读撕裂同步(TE)信号
 * @return 0 / 1 = 引脚电平; 【-1 = 本板没接 TE】
 * @note 返回 -1 时框架走不等 TE 的直推路径, 这是没接 TE 时的正确行为。
 */
int32_t ui_lcd_te_read(void);


/* ==================================================================== *
 *  数据通道 (CLK / DI)
 * ==================================================================== */

/**
 * @brief 发送单字节(发面板命令用, 数据量极小)
 * @return 0 成功, 负值失败(超时)
 * @note 调用方负责先用 ui_lcd_dc() / ui_lcd_cs() 摆好时序。
 *       实现可以是纯阻塞的 —— 命令总量极小, 用 DMA 是纯开销。
 */
int32_t ui_lcd_write_byte(uint8_t byte);

/**
 * @brief 发送一块显存(推屏主通道, 实现里应尽量走 DMA)
 * @param buf     数据首地址
 * @param len     字节数
 * @param is_wait 非 0 = 发完才返回; 0 = 允许后台搬运, 由 ui_lcd_wait_done() 等
 * @return 0 成功, 负值失败(超时)
 * @note 异步模式下 buf 在传输完成前【必须保持有效】, 调用方负责在复用或
 *       释放该 buf 之前先 wait_done。
 */
int32_t ui_lcd_write_block(const uint8_t *buf, uint32_t len, uint8_t is_wait);

/**
 * @brief 等待上一次异步 write_block 完成
 * @note 无未完成传输时必须立即返回。内部需喂狗, 因为整屏推送耗时较长。
 *       返回后必须保证【最后一个字节已经上线】, 而不只是"已写进数据寄存器"
 *       —— 调用方紧接着就会拉高 CS, 见 ui_lcd_stm32f4.c 里的说明。
 */
void ui_lcd_wait_done(void);

/**
 * @brief 数据同步屏障
 * @note 写完要交给 DMA 读的显存之后、启动传输之前调用, 确保写已落到内存。
 *       没有写缓冲 / 乱序问题的平台可以留空。
 */
void ui_lcd_memory_barrier(void);


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
 *  这样换 MCU 时只需重写实现文件, 不会顺带把 OS 相关代码也拖过去;
 *  换 RTOS 时反过来也只动 task_manager。
 *
 *  ⚠ 唯一的例外是喂狗: ui_lcd_wait_done() 内部会调 wdt_clear(), 因为整屏
 *    推送的忙等时间足以触发看门狗。也就是说实现层【确实】依赖 OS 层的这一个
 *    符号, 依赖方向是 lcd -> os。这点原先只体现在源码的 #include 里、
 *    和这段注释相矛盾, 现在明写出来。
 * ==================================================================== */

#endif /* __UI_LCD_IF_H__ */
