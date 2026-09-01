/**
 * @file    ui_hal_stm32f4.c
 * @brief   UI 框架硬件抽象层 —— STM32F407VGT6 实现(SPI3 + DMA1_Stream5)
 *
 * 换 MCU 时【只需要重写本文件】, 实现 ui_hal.h 里那 7 个函数即可。
 * 引脚与 DMA 通路的选择集中在 port/ui_port_config.h。
 */
#include "ui_hal.h"
#include "ui_port_config.h"
#include "stm32f4xx_hal.h"
#include "spi.h"
#include "task_manager.h"   /* wdt_clear: 全工程唯一的喂狗入口 */

/* SPI3 句柄由 CubeMX 生成的 Core/Src/spi.c 提供并已在 main() 里初始化。
 * 本层不重复调 MX_SPI3_Init(), 只补 DMA 通路与控制脚 —— 避免和
 * CubeMX 重新生成代码时打架 */
extern SPI_HandleTypeDef hspi3;

static DMA_HandleTypeDef s_hdma_spi3_tx;
static uint8_t s_is_inited = 0;

/** DMA 传输是否在飞。ISR 里清零, 任务里轮询 —— 单字节单向读写,
 * 按规范只需 volatile 保证可见性, 不需要临界区 */
static volatile uint8_t s_is_dma_busy = 0;


/* ------------------------------------------------------------------ */
/* 引脚                                                               */
/* ------------------------------------------------------------------ */

/** 把抽象引脚号翻译成 STM32 的 (port, pin)。
 * @return 0 = 该引脚未配置, 调用方应静默跳过 */
static uint8_t Ui_Hal_PinResolve(ui_hal_pin_t pin, GPIO_TypeDef **port, uint16_t *bit)
{
    switch (pin) {
    case UI_HAL_PIN_LCD_CS:
        *port = UI_PORT_PIN_CS_PORT;
        *bit  = UI_PORT_PIN_CS_PIN;
        return 1;
    case UI_HAL_PIN_LCD_DC:
        *port = UI_PORT_PIN_DC_PORT;
        *bit  = UI_PORT_PIN_DC_PIN;
        return 1;
    case UI_HAL_PIN_LCD_RST:
        *port = UI_PORT_PIN_RST_PORT;
        *bit  = UI_PORT_PIN_RST_PIN;
        return 1;
#if UI_PORT_PIN_BL_ENABLE
    case UI_HAL_PIN_LCD_BL:
        *port = UI_PORT_PIN_BL_PORT;
        *bit  = UI_PORT_PIN_BL_PIN;
        return 1;
#endif
    default:
        return 0;
    }
}

void ui_hal_pin_write(ui_hal_pin_t pin, uint8_t level)
{
    GPIO_TypeDef *port = NULL;
    uint16_t bit = 0;

    /* 未配置的脚静默返回: SSD1306 无背光脚, 框架仍会调 BL 控制 */
    if (!Ui_Hal_PinResolve(pin, &port, &bit)) {
        return;
    }
    HAL_GPIO_WritePin(port, bit, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/** 把 CS/DC/RST 配成推挽输出, 并置到安全的空闲电平 */
static void Ui_Hal_PinInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio.Pin = UI_PORT_PIN_CS_PIN;
    HAL_GPIO_Init(UI_PORT_PIN_CS_PORT, &gpio);
    gpio.Pin = UI_PORT_PIN_DC_PIN;
    HAL_GPIO_Init(UI_PORT_PIN_DC_PORT, &gpio);
    gpio.Pin = UI_PORT_PIN_RST_PIN;
    HAL_GPIO_Init(UI_PORT_PIN_RST_PORT, &gpio);

    /* CS 高 = 未选中; RST 高 = 不复位。DC 电平无所谓, 每次传输前都会设 */
    ui_hal_pin_write(UI_HAL_PIN_LCD_CS,  1);
    ui_hal_pin_write(UI_HAL_PIN_LCD_RST, 1);
    ui_hal_pin_write(UI_HAL_PIN_LCD_DC,  1);
}


/* ------------------------------------------------------------------ */
/* SPI + DMA                                                          */
/* ------------------------------------------------------------------ */

/** 给 SPI3_TX 挂上 DMA。CubeMX 没生成这段(工程里 SPI3 是纯阻塞用法),
 * 所以在这里补, 不改 Core/Src/spi.c */
static int32_t Ui_Hal_SpiDmaInit(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_hdma_spi3_tx.Instance                 = UI_PORT_SPI_DMA_STREAM;
    s_hdma_spi3_tx.Init.Channel             = UI_PORT_SPI_DMA_CHANNEL;
    s_hdma_spi3_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    s_hdma_spi3_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_spi3_tx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_spi3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_hdma_spi3_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    s_hdma_spi3_tx.Init.Mode                = DMA_NORMAL;
    s_hdma_spi3_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma_spi3_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_hdma_spi3_tx) != HAL_OK) {
        return -1;
    }
    __HAL_LINKDMA(&hspi3, hdmatx, s_hdma_spi3_tx);

    /* 优先级必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 的数值
     * (即优先级更低), 否则 ISR 里不能调 FreeRTOS API。本 ISR 只写一个
     * volatile 标志, 不调 OS, 但仍留出余量避免干扰 I2S 音频流 */
    HAL_NVIC_SetPriority(UI_PORT_SPI_DMA_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UI_PORT_SPI_DMA_IRQn);

    return 0;
}

int32_t ui_hal_spi_init(void)
{
    if (s_is_inited) {
        return 0;
    }

    Ui_Hal_PinInit();

    if (Ui_Hal_SpiDmaInit() != 0) {
        return -1;
    }

    s_is_inited = 1;
    return 0;
}

int32_t ui_hal_spi_send_byte(uint8_t byte)
{
    /* 单字节走阻塞发送: 命令序列总量极小(初始化代码 + 每 page 三条),
     * 用 DMA 反而是纯开销 */
    ui_hal_spi_wait_done();

    if (HAL_SPI_Transmit(&hspi3, &byte, 1, UI_PORT_SPI_TIMEOUT_MS) != HAL_OK) {
        return -1;
    }
    return 0;
}

int32_t ui_hal_spi_send_block(const uint8_t *buf, uint32_t len, uint8_t is_wait)
{
    HAL_StatusTypeDef ret;

    if ((buf == NULL) || (len == 0)) {
        return -1;
    }

    /* 上一笔没走完就不能改 DMA 寄存器 */
    ui_hal_spi_wait_done();

    s_is_dma_busy = 1;
    /* HAL_SPI_Transmit_DMA 的形参没有 const, 但 DMA 只读该内存 */
    ret = HAL_SPI_Transmit_DMA(&hspi3, (uint8_t *)buf, (uint16_t)len);
    if (ret != HAL_OK) {
        s_is_dma_busy = 0;
        return -1;
    }

    if (is_wait) {
        ui_hal_spi_wait_done();
    }
    return 0;
}

void ui_hal_spi_wait_done(void)
{
    uint32_t tickstart = HAL_GetTick();

    while (s_is_dma_busy) {
        wdt_clear();
        /* 超时兜底: 屏没接 / DMA 配错时不能把 UI 任务永久卡死,
         * 否则整个 FreeRTOS 里这个优先级以下的任务全部饿死 */
        if ((HAL_GetTick() - tickstart) > UI_PORT_SPI_TIMEOUT_MS) {
            HAL_SPI_DMAStop(&hspi3);
            s_is_dma_busy = 0;
            break;
        }
    }

    /*
     * 【为什么还要等 TXE/BSY】DMA 的传输完成中断是在"最后一字节被写进 SPI
     * 数据寄存器"时触发的, 此刻移位寄存器里还有最多两字节尚未发上线。
     * 调用方 oled_spi_draw() 紧跟着就拉高 CS —— 那两字节会被整片丢掉,
     * 表现为每页右边少 1~2 列。所以必须等到 TXE 置起且 BSY 清零。
     */
    while ((__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_TXE) == RESET) ||
           (__HAL_SPI_GET_FLAG(&hspi3, SPI_FLAG_BSY) != RESET)) {
        wdt_clear();
        if ((HAL_GetTick() - tickstart) > UI_PORT_SPI_TIMEOUT_MS) {
            break;
        }
    }
}

/** DMA 发送完成回调。HAL 在 DMA ISR 里调到这里 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI3) {
        s_is_dma_busy = 0;
    }
}

/** DMA/SPI 出错也要放行等待方, 否则 wait_done 只能靠超时退出 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI3) {
        s_is_dma_busy = 0;
    }
}

/** DMA1_Stream5 中断入口。
 * @note Core/Src/stm32f4xx_it.c 里没有这个 handler, 定义在这里即可被
 *       启动文件的弱符号向量表接住 */
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_spi3_tx);
}


/* ------------------------------------------------------------------ */
/* CPU 原语                                                           */
/* ------------------------------------------------------------------ */

void ui_hal_memory_barrier(void)
{
    __DSB();
}
