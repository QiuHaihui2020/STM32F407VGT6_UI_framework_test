/**
 * @file    ui_lcd_stm32f4.c
 * @brief   UI 框架硬件抽象层 —— STM32F407VGT6 实现(SPI3 + DMA1_Stream5)
 *
 * 换 MCU 时【只需要重写本文件】+ board/ui_board_<mcu>.h。
 *
 * 本文件是整个工程里唯一同时看得见「ui_lcd_* 语义接口」和「STM32 HAL 符号」
 * 的地方。引脚、端口、SPI 实例、DMA 流这些概念【到本文件为止】——
 * 上面的框架只知道"把 CS 拉低"、"发一块显存"。
 */
#include "ui_lcd_if.h"
#include "ui_board_pins.h"          /* 引脚 token 与接线表。port 内部 */
#include "ui_board_stm32f4.h"       /* SPI/DMA 实例。仅本文件可 include */
#include "stm32f4xx_hal.h"
#include "spi.h"                    /* CubeMX 生成: UI_BOARD_SPI_HANDLE 的定义 */
#include "task_manager.h"           /* wdt_clear: 全工程唯一的喂狗入口 */

/* SPI 句柄由 CubeMX 生成的 Core/Src/spi.c 提供并已在 main() 里初始化。
 * 本层不重复调 MX_SPIx_Init(), 只补 DMA 通路与控制脚 */
extern SPI_HandleTypeDef UI_BOARD_SPI_HANDLE;
#define __SPI       (&UI_BOARD_SPI_HANDLE)

static DMA_HandleTypeDef s_hdma_spi_tx;
static uint8_t s_is_inited = 0;

/** DMA 传输是否在飞。ISR 里清零, 任务里轮询 —— 单字节单向读写,
 * 按规范只需 volatile 保证可见性, 不需要临界区 */
static volatile uint8_t s_is_dma_busy = 0;

/** 端口索引 -> 端口基址。索引与 ui_board_pins.h 的 UI_BOARD_PORT_* 一致 */
static GPIO_TypeDef *const s_gpio_ports[UI_BOARD_GPIO_PORT_NUM] = UI_BOARD_GPIO_PORT_TABLE;


/* ==================================================================== *
 *  引脚 —— 全部 static, 是本文件的实现细节而非对外接口
 * ==================================================================== */

/**
 * @brief 解码引脚 token
 * @param port 出参, 端口基址
 * @param bit  出参, 位掩码(1 << 位号)
 * @return 0 = 该 token 不是有效引脚(未配置 / 端口索引越界), 调用方静默跳过
 */
static uint8_t Ui_Lcd_PinResolve(ui_board_pin_t pin, GPIO_TypeDef **port, uint32_t *bit)
{
    uint32_t port_idx;

    if (!UI_BOARD_PIN_IS_VALID(pin)) {
        return 0;
    }

    port_idx = UI_BOARD_PIN_PORT_IDX(pin);
    if (port_idx >= UI_BOARD_GPIO_PORT_NUM) {
        /* 板级头写错了才会到这里。静默跳过而不是断言 —— 断言在推屏路径上
         * 每帧会打几十条, 反而看不出问题; 表现为该线不动, 一试就知道 */
        return 0;
    }

    *port = s_gpio_ports[port_idx];
    *bit  = 1U << UI_BOARD_PIN_BIT_IDX(pin);
    return 1;
}

/** 写引脚电平。传入 UI_BOARD_PIN_NONE(没接的 BL/EN)时静默返回 */
static void Ui_Lcd_PinWrite(ui_board_pin_t pin, uint8_t level)
{
    GPIO_TypeDef *port = NULL;
    uint32_t bit = 0;

    if (!Ui_Lcd_PinResolve(pin, &port, &bit)) {
        return;
    }

    /*
     * 用 BSRR 而不是 HAL_GPIO_WritePin():
     *   - 单次寄存器写, 天然原子, 不需要读改写(推屏每页要翻 CS/DC 各两次,
     *     整帧 8 页共 32 次, 这条路径值得省)
     *   - F4 的 BSRR 是 32 位: 低半字写 1 置位, 高半字写 1 复位
     */
    port->BSRR = level ? bit : (bit << 16);
}

/** 按端口索引使能 GPIO 时钟。
 * @note 必须用 HAL 的宏而不是手写 RCC->AHB1ENR |= —— 宏内部有读回延时,
 *       缺了它在某些 F4 上使能后【首次】访问该端口会丢失。 */
static void Ui_Lcd_PortClkEnable(uint32_t port_idx)
{
    switch (port_idx) {
    case UI_BOARD_PORT_A:
        __HAL_RCC_GPIOA_CLK_ENABLE();
        break;
    case UI_BOARD_PORT_B:
        __HAL_RCC_GPIOB_CLK_ENABLE();
        break;
    case UI_BOARD_PORT_C:
        __HAL_RCC_GPIOC_CLK_ENABLE();
        break;
    case UI_BOARD_PORT_D:
        __HAL_RCC_GPIOD_CLK_ENABLE();
        break;
    case UI_BOARD_PORT_E:
        __HAL_RCC_GPIOE_CLK_ENABLE();
        break;
    default:
        break;
    }
}

/**
 * @brief 把板级表里的控制线全部配成推挽输出, 并置到安全的空闲电平
 *
 * @note 原先这里是硬编码 __HAL_RCC_GPIOB_CLK_ENABLE() 加三条写死的
 *       HAL_GPIO_Init(), 引脚一挪到别的端口就【静默失效】(写寄存器不报错,
 *       电平不动)。现在遍历 UI_BOARD_OUTPUT_PIN_LIST, 逐个按端口使能时钟,
 *       加引脚只需在板级头的表里加一行。
 */
static void Ui_Lcd_PinInit(void)
{
    static const ui_board_pin_t pin_list[] = { UI_BOARD_OUTPUT_PIN_LIST };
    GPIO_InitTypeDef gpio = {0};
    GPIO_TypeDef *port = NULL;
    uint32_t bit = 0;
    uint32_t i;

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    for (i = 0; i < (sizeof(pin_list) / sizeof(pin_list[0])); i++) {
        /* 表里允许有 UI_BOARD_PIN_NONE(没接的 BL/EN), 直接跳过 */
        if (!Ui_Lcd_PinResolve(pin_list[i], &port, &bit)) {
            continue;
        }
        Ui_Lcd_PortClkEnable(UI_BOARD_PIN_PORT_IDX(pin_list[i]));
        gpio.Pin = bit;
        HAL_GPIO_Init(port, &gpio);
    }

    /* CS 高 = 未选中; RST 高 = 不复位。DC 电平无所谓, 每次传输前都会设 */
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_CS,  1);
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_RST, 1);
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_DC,  1);
}

/** TE 脚配成浮空输入。没接 TE 时什么都不做 */
static void Ui_Lcd_TePinInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    GPIO_TypeDef *port = NULL;
    uint32_t bit = 0;

    if (!Ui_Lcd_PinResolve(UI_BOARD_PIN_LCD_TE, &port, &bit)) {
        return;
    }

    Ui_Lcd_PortClkEnable(UI_BOARD_PIN_PORT_IDX(UI_BOARD_PIN_LCD_TE));
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = bit;
    HAL_GPIO_Init(port, &gpio);
}


/* ==================================================================== *
 *  控制线 —— ui_lcd_if.h 的对外实现。每个只是"哪条线"的具名化
 * ==================================================================== */

void ui_lcd_cs(uint8_t level)
{
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_CS, level);
}

void ui_lcd_dc(uint8_t level)
{
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_DC, level);
}

void ui_lcd_rst(uint8_t level)
{
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_RST, level);
}

void ui_lcd_bl(uint8_t level)
{
    /* SSD1306 自发光, UI_BOARD_PIN_LCD_BL 配的是 NONE, 这里是空操作。
     * 换成带背光的屏时只改 board/ui_board_pins.h, 本函数不用动 */
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_BL, level);
}

int32_t ui_lcd_has_backlight(void)
{
    /* 编译期常量: 本板 UI_BOARD_PIN_LCD_BL 配的是 NONE, 恒返回 0,
     * 调用方那个分支会被优化掉 */
    return UI_BOARD_PIN_IS_VALID(UI_BOARD_PIN_LCD_BL) ? 1 : 0;
}

void ui_lcd_power(uint8_t level)
{
    Ui_Lcd_PinWrite(UI_BOARD_PIN_LCD_EN, level);
}

int32_t ui_lcd_te_read(void)
{
    GPIO_TypeDef *port = NULL;
    uint32_t bit = 0;

    /* 没接 TE 时返回 -1, 让框架走不等 TE 的直推路径 */
    if (!Ui_Lcd_PinResolve(UI_BOARD_PIN_LCD_TE, &port, &bit)) {
        return -1;
    }
    return (port->IDR & bit) ? 1 : 0;
}


/* ==================================================================== *
 *  数据通道
 * ==================================================================== */

/** 给 SPI_TX 挂上 DMA。CubeMX 没生成这段(工程里 SPI 是纯阻塞用法),
 * 所以在这里补, 不改 Core/Src/spi.c */
static int32_t Ui_Lcd_SpiDmaInit(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_hdma_spi_tx.Instance                 = UI_BOARD_SPI_DMA_STREAM;
    s_hdma_spi_tx.Init.Channel             = UI_BOARD_SPI_DMA_CHANNEL;
    s_hdma_spi_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_hdma_spi_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_spi_tx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_spi_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_hdma_spi_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    s_hdma_spi_tx.Init.Mode                = DMA_NORMAL;
    s_hdma_spi_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma_spi_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_hdma_spi_tx) != HAL_OK) {
        return -1;
    }
    __HAL_LINKDMA(__SPI, hdmatx, s_hdma_spi_tx);

    HAL_NVIC_SetPriority(UI_BOARD_SPI_DMA_IRQn, UI_BOARD_SPI_DMA_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(UI_BOARD_SPI_DMA_IRQn);

    return 0;
}

int32_t ui_lcd_init(void)
{
    if (s_is_inited) {
        return 0;
    }

    Ui_Lcd_PinInit();
    Ui_Lcd_TePinInit();

    if (Ui_Lcd_SpiDmaInit() != 0) {
        return -1;
    }

    s_is_inited = 1;
    return 0;
}

int32_t ui_lcd_write_byte(uint8_t byte)
{
    /* 单字节走阻塞发送: 命令序列总量极小(初始化代码 + 每 page 三条),
     * 用 DMA 反而是纯开销 */
    ui_lcd_wait_done();

    if (HAL_SPI_Transmit(__SPI, &byte, 1, UI_BOARD_SPI_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }
    return 0;
}

int32_t ui_lcd_write_block(const uint8_t *buf, uint32_t len, uint8_t is_wait)
{
    HAL_StatusTypeDef ret;

    if ((buf == NULL) || (len == 0)) {
        return -1;
    }

    /* 上一笔没走完就不能改 DMA 寄存器 */
    ui_lcd_wait_done();

    s_is_dma_busy = 1;
    /* HAL_SPI_Transmit_DMA 的形参没有 const, 但 DMA 只读该内存 */
    ret = HAL_SPI_Transmit_DMA(__SPI, (uint8_t *)buf, (uint16_t)len);
    if (ret != HAL_OK) {
        s_is_dma_busy = 0;
        return -1;
    }

    if (is_wait) {
        ui_lcd_wait_done();
    }
    return 0;
}

void ui_lcd_wait_done(void)
{
    uint32_t tickstart = HAL_GetTick();

    while (s_is_dma_busy) {
        wdt_clear();
        /* 超时兜底: 屏没接 / DMA 配错时不能把 UI 任务永久卡死,
         * 否则整个 FreeRTOS 里这个优先级以下的任务全部饿死 */
        if ((HAL_GetTick() - tickstart) > UI_BOARD_SPI_TIMEOUT_MS) {
            HAL_SPI_DMAStop(__SPI);
            s_is_dma_busy = 0;
            break;
        }
    }

    /*
     * 【为什么还要等 TXE/BSY】DMA 的传输完成中断是在"最后一字节被写进 SPI
     * 数据寄存器"时触发的, 此刻移位寄存器里还有最多两字节尚未发上线。
     * 调用方紧跟着就拉高 CS —— 那两字节会被整片丢掉, 表现为每页右边
     * 少 1~2 列。所以必须等到 TXE 置起且 BSY 清零。
     */
    while ((__HAL_SPI_GET_FLAG(__SPI, SPI_FLAG_TXE) == RESET) ||
           (__HAL_SPI_GET_FLAG(__SPI, SPI_FLAG_BSY) != RESET)) {
        wdt_clear();
        if ((HAL_GetTick() - tickstart) > UI_BOARD_SPI_TIMEOUT_MS) {
            break;
        }
    }
}

/** DMA 发送完成回调。HAL 在 DMA ISR 里调到这里。
 * @note 判 Instance 是必须的 —— 本回调是全工程共享的弱符号, 别处也用
 *       SPI DMA 时会一起进来 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == UI_BOARD_SPI_INSTANCE) {
        s_is_dma_busy = 0;
    }
}

/** DMA/SPI 出错也要放行等待方, 否则 wait_done 只能靠超时退出 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == UI_BOARD_SPI_INSTANCE) {
        s_is_dma_busy = 0;
    }
}

/** DMA 中断入口。
 * @note Core/Src/stm32f4xx_it.c 里没有这个 handler, 定义在这里即可被
 *       启动文件的弱符号向量表接住。若哪天 CubeMX 勾上了本 SPI 的 DMA,
 *       这里会和它生成的同名函数【重复定义】—— 那是链接错误而非静默故障,
 *       删掉本函数即可。 */
void UI_BOARD_SPI_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_spi_tx);
}


/* ==================================================================== *
 *  CPU 原语
 * ==================================================================== */

void ui_lcd_memory_barrier(void)
{
    __DSB();
}
