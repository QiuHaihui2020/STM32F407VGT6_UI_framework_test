/**
 * @file    ui_board_pins.h
 * @brief   板级引脚分配 —— 屏的控制线接在哪
 *
 * 【本文件是 port 层内部的东西, 框架不 include 它, 也不知道它存在。】
 *
 * 框架看到的边界是 ui_hal.h 里那几个语义函数(ui_hal_lcd_cs/dc/rst/...),
 * 只说"把 CS 拉低", 从不持有引脚句柄。引脚这个概念到 port 层就终止了 ——
 * 所以改接线不会波及框架的任何一行。
 *
 * 为什么引脚和外设实例分两个文件:
 *   引脚 = 【板子怎么接线】, 改板不换芯片时只动这里;
 *   外设 = 【用 MCU 的哪套资源】(SPI/DMA/时钟), 在 ui_board_stm32f4.h,
 *          那个文件含 STM32 HAL 符号, 换 MCU 必须整份重写。
 *   两者变化的原因不同, 混在一个文件里会让"改块屏"和"换颗芯片"
 *   这两件事无法区分。
 *
 * @note token 的编码规则由本文件与 ui_hal_<mcu>.c 【共同约定】, 是纯粹的
 *       port 内部实现细节。换 MCU 时可以整套换掉(例如杰理直接用
 *       port*16+pin), 不需要通知任何人 —— ui_hal.h 对此完全无知。
 */
#ifndef __UI_BOARD_PINS_H__
#define __UI_BOARD_PINS_H__

#include <stdint.h>

/** 引脚 token。仅 port 层内部流通 */
typedef uint32_t ui_board_pin_t;

/** 该引脚未使用。所有 port 内部的引脚操作都必须静默忽略它 */
#define UI_BOARD_PIN_NONE       ((ui_board_pin_t)0)


/* ==================================================================== *
 *  一、token 编码 (STM32 版: 端口索引 + 位号)
 *
 *      bit31    : 有效标记。置位才是一个真引脚, 全 0 即 UI_BOARD_PIN_NONE
 *      bit[11:8]: 端口索引, GPIOA=0 GPIOB=1 ... 与 RCC_AHB1ENR 的位序一致
 *      bit[4:0] : 位号 0..15
 *
 *  加 bit31 是为了让"未配置"只有一种表示(0)。原先框架里有 -1 和 0 两种
 *  未配置标记, 判断散在十几处, 漏一处就是去操作一个不存在的引脚。
 * ==================================================================== */

#define UI_BOARD_PORT_A         0U
#define UI_BOARD_PORT_B         1U
#define UI_BOARD_PORT_C         2U
#define UI_BOARD_PORT_D         3U
#define UI_BOARD_PORT_E         4U

#define UI_BOARD_PIN_VALID_BIT  0x80000000U

/** 组装一个引脚 token */
#define UI_BOARD_PIN(port_idx, bit_idx) \
    ((ui_board_pin_t)(UI_BOARD_PIN_VALID_BIT | ((uint32_t)(port_idx) << 8) | (uint32_t)(bit_idx)))

/* 解码。只有 ui_hal_<mcu>.c 该用这三个 */
#define UI_BOARD_PIN_IS_VALID(p)    (((uint32_t)(p) & UI_BOARD_PIN_VALID_BIT) != 0U)
#define UI_BOARD_PIN_PORT_IDX(p)    (((uint32_t)(p) >> 8) & 0x0FU)
#define UI_BOARD_PIN_BIT_IDX(p)     ((uint32_t)(p) & 0x1FU)


/* ==================================================================== *
 *  二、本板接线 (STM32F407VGT6 + SSD1306 128x64)
 *
 *  SPI3: PB3 = SCK, PB5 = MOSI (MISO PB4 单色屏不用)
 *  下面三条控制线按实际接线改, 改完【不需要动任何别的文件】。
 * ==================================================================== */

#define UI_BOARD_PIN_LCD_CS     UI_BOARD_PIN(UI_BOARD_PORT_B, 6)    /**< 片选, 低有效 */
#define UI_BOARD_PIN_LCD_DC     UI_BOARD_PIN(UI_BOARD_PORT_B, 7)    /**< 低=命令 高=数据 */
#define UI_BOARD_PIN_LCD_RST    UI_BOARD_PIN(UI_BOARD_PORT_B, 8)    /**< 复位, 低有效 */

/* SSD1306 自发光无背光脚; 屏供电使能与撕裂同步本板也没接。
 * 配成 NONE 后对应的 ui_hal_lcd_bl()/power() 就是空操作, 框架照常调用 */
#define UI_BOARD_PIN_LCD_BL     UI_BOARD_PIN_NONE
#define UI_BOARD_PIN_LCD_EN     UI_BOARD_PIN_NONE
#define UI_BOARD_PIN_LCD_TE     UI_BOARD_PIN_NONE


/* ==================================================================== *
 *  三、需要配成推挽输出的引脚表
 *
 *  ui_hal_lcd_init() 遍历本表建 GPIO 并【按端口逐个使能时钟】。
 *  加一条控制线: 上面加一行 #define, 这里加一行, 别处都不用动。
 *
 *  ⚠ 原先 HAL 里是硬编码 __HAL_RCC_GPIOB_CLK_ENABLE(), 引脚挪到
 *    GPIOC 就静默失效(写寄存器不报错, 电平不动)。有了这张表就不会漏。
 *  ⚠ 表里允许出现 UI_BOARD_PIN_NONE, 遍历时会跳过 —— 没接的脚
 *    保持列在表里即可, 不用加 #if。
 * ==================================================================== */

#define UI_BOARD_OUTPUT_PIN_LIST    \
    UI_BOARD_PIN_LCD_CS,            \
    UI_BOARD_PIN_LCD_DC,            \
    UI_BOARD_PIN_LCD_RST,           \
    UI_BOARD_PIN_LCD_BL,            \
    UI_BOARD_PIN_LCD_EN

#endif /* __UI_BOARD_PINS_H__ */
