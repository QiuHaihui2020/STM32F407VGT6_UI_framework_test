/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* SDIO/SD 卡驱动开关.
 * 0 = 不使用 SDIO. CubeMX 已关闭 SDIO 并移除 sdio.c / stm32f4xx_hal_sd.c 等源文件,
 *     且 stm32f4xx_hal_conf.h 中 HAL_SD_MODULE_ENABLED 已注释, 此时必须为 0.
 * 1 = 使用 SDIO. 需先在 CubeMX 重新使能 SDIO 并重新生成代码后再置 1. */
#define SDIO_ENABLE   0

/* FATFS 文件系统开关. 与 SDIO_ENABLE 相互独立:
 * FATFS 的物理层由 diskio 驱动决定, 不必是 SD 卡. */
#define FATFS_ENABLE  1

#define FSMC_ENABLE   0

/* LCD 显示开关.
 * 0 = 不使用 LCD. lcd.c / text.c 的实现体整体不参与编译, 连同 font.h 里的
 *     两张 ASCII 字库表(asc2_1206 + asc2_1608, 合计约 2.6KB RO-data)一起省掉.
 *     lcd.h / text.h 的声明仍然保留, 因此调用方必须自己用本宏包住调用点,
 *     否则会在链接期才报 undefined symbol.
 * 1 = 使用 LCD. */
#define LCD_ENABLE    0


/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED0_Pin GPIO_PIN_5
#define LED0_GPIO_Port GPIOE
#define LED1_Pin GPIO_PIN_6
#define LED1_GPIO_Port GPIOE
#define KEY2_Pin GPIO_PIN_0
#define KEY2_GPIO_Port GPIOA
#define LCD_BL_Pin GPIO_PIN_1
#define LCD_BL_GPIO_Port GPIOB
#define KEY4_Pin GPIO_PIN_0
#define KEY4_GPIO_Port GPIOE
#define KEY3_Pin GPIO_PIN_1
#define KEY3_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
