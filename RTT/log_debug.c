#include "log_debug.h"
#include <stdio.h>
#if UART_PRINTF_ENABLE
#include "usart.h"
#endif


uint32_t tick_us = 0;
uint32_t tick_ms = 0;
uint32_t tick_s = 0;
uint32_t tick_min = 0;
uint32_t tick_hour = 0;
uint64_t SysTick_us = 0;
uint64_t SysTick_ms = 0; 
void log_timer_calculation(void)
{
    tick_us += 1000;
    SysTick_us += 1000;
    if(tick_us >= 1000) {
        tick_us = 0;
        tick_ms++;
        SysTick_ms++;
        if(tick_ms >= 1000)
        {
            tick_ms = 0;
            tick_s++;
            if(tick_s >= 60)
            {
                tick_s = 0;
                tick_min++;
                if(tick_min >= 60)
                {
                    tick_min = 0;
                    tick_hour++;
                }
            }
        }
    }
}

//#include <unistd.h>  // 提供 _write 原型
#if (UART_PRINTF_ENABLE && USE_UART_DMA_TX)

#define UART_TX_BUF_SIZE (1024 * 4)

static uint8_t tx_buf[UART_TX_BUF_SIZE];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;
static volatile uint8_t dma_busy = 0;
static volatile uint16_t tx_dma_len = 0;

static void uart_write_buf(const uint8_t *data, uint16_t len)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((tx_head + 1U) % UART_TX_BUF_SIZE);
        if (next == tx_tail) {
            break;
        }
        tx_buf[tx_head] = data[i];
        tx_head = next;
    }
    __set_PRIMASK(primask);
}

static void uart_kick_tx(void)
{
    uint16_t tail;
    uint16_t head;
    uint16_t len;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (dma_busy || tx_head == tx_tail) {
        __set_PRIMASK(primask);
        return;
    }
    tail = tx_tail;
    head = tx_head;
    if (head > tail) {
        len = (uint16_t)(head - tail);
    } else {
        len = (uint16_t)(UART_TX_BUF_SIZE - tail);
    }
    dma_busy = 1;
    tx_dma_len = len;
    __set_PRIMASK(primask);

    if (HAL_UART_Transmit_DMA(&huart1, &tx_buf[tail], len) != HAL_OK) {
        primask = __get_PRIMASK();
        __disable_irq();
        dma_busy = 0;
        tx_dma_len = 0;
        __set_PRIMASK(primask);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart1) {
        return;
    }
    uint16_t len = tx_dma_len;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    tx_tail = (uint16_t)((tx_tail + len) % UART_TX_BUF_SIZE);
    dma_busy = 0;
    __set_PRIMASK(primask);

    uart_kick_tx();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart1) {
        return;
    }
    (void)HAL_UART_DMAStop(huart);
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    dma_busy = 0;
    __set_PRIMASK(primask);
    uart_kick_tx();
}
#endif

int _write(int file, char *ptr, int len)
{
    (void)file;  // 忽略文件描述符
    if (len <= 0) {
        return len;
    }
#if UART_PRINTF_ENABLE
#if USE_UART_DMA_TX
    uart_write_buf((const uint8_t *)ptr, (uint16_t)len);
    uart_kick_tx();
#else
    HAL_UART_Transmit(&huart1, (uint8_t*)ptr, len, HAL_MAX_DELAY);
#endif /*!USE_UART_DMA_TX*/
#else
    for (int i = 0; i < len; i++) {
        SEGGER_RTT_PutChar(0, ptr[i]);
    }
#endif /*!UART_PRINTF_ENABLE*/
    return len;
}

// 适配armclang的标准IO重定向：使 printf/fprintf(stderr, ...) 通过RTT输出
int fputc(int ch, FILE *f)
{
    (void)f;
#if UART_PRINTF_ENABLE
    uint8_t c = (uint8_t)ch;
    (void)_write(0, (char *)&c, 1);
#else
    SEGGER_RTT_PutChar(0, (char)ch);
#endif
    return ch;
}
