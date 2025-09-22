/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2s.h
  * @brief   This file contains all the function prototypes for
  *          the i2s.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2S_H__
#define __I2S_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include "typedef.h"

/* USER CODE END Includes */

extern I2S_HandleTypeDef hi2s2;

/* USER CODE BEGIN Private defines */
#define IIS_TX_FRAME_POINTS (48 * 1)//1 channel 48k 48 points
#define IIS_CHANNELS 2
/* USER CODE END Private defines */

void MX_I2S2_Init(void);

/* USER CODE BEGIN Prototypes */

enum I2S_TX_STATE{
	I2S_TX_STOP_STA,
	I2S_TX_START_STA,
	I2S_TX_HALF_IRQ_STA,
	I2S_TX_FULL_IRQ_STA,
};
void HAL_I2s_tx_start(void(*irq_callback)(void *data, uint16_t len));
void HAL_I2s_tx_stop(void);
void HAL_I2S_set_tx_irq_handler(void(*irq_callback)(void *data, uint16_t len));
uint8_t get_i2s_tx_state(void);
void set_i2s_tx_dma_data(void *data, uint16_t len);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2S_H__ */

