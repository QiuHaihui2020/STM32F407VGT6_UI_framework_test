/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_midi_if.c
  * @version        : v1.0_Cube
  * @brief          : USB Device MIDI interface file.
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

/* Includes ------------------------------------------------------------------*/
#include "usbd_midi_if.h"

/* USER CODE BEGIN INCLUDE */
#include "log_debug.h"

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device.
  * @{
  */

/** @addtogroup USBD_MIDI
  * @{
  */

/** @defgroup USBD_MIDI_Private_TypesDefinitions USBD_MIDI_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_Defines USBD_MIDI_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_Macros USBD_MIDI_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_Variables USBD_MIDI_Private_Variables
  * @brief Private variables.
  * @{
  */

/** Usb HID report descriptor. */
__ALIGN_BEGIN static uint8_t MIDI_ReportDesc_FS[USBD_MIDI_REPORT_DESC_SIZE] __ALIGN_END =
{
  /* USER CODE BEGIN 0 */
  0x00,
  /* USER CODE END 0 */
  0xC0    /*     END_COLLECTION	             */
};

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_MIDI_Exported_Variables USBD_MIDI_Exported_Variables
  * @brief Public variables.
  * @{
  */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */
/**
  * @}
  */

/** @defgroup USBD_MIDI_Private_FunctionPrototypes USBD_MIDI_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t MIDI_Init_FS(void);
static int8_t MIDI_DeInit_FS(void);
static int8_t MIDI_OutEvent_FS(uint8_t *data, uint16_t length);

/**
  * @}
  */

USBD_MIDI_ItfTypeDef USBD_MIDI_fops_FS =
{
  MIDI_ReportDesc_FS,
  MIDI_Init_FS,
  MIDI_DeInit_FS,
  MIDI_OutEvent_FS
};

/** @defgroup USBD_MIDI_Private_Functions USBD_MIDI_Private_Functions
  * @brief Private functions.
  * @{
  */

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes the CUSTOM HID media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t MIDI_Init_FS(void)
{
  /* USER CODE BEGIN 4 */
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  DeInitializes the CUSTOM HID media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t MIDI_DeInit_FS(void)
{
  /* USER CODE BEGIN 5 */
  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Manage the CUSTOM HID class events
  * @param  event_idx: Event index
  * @param  state: Event state
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t MIDI_OutEvent_FS(uint8_t *data, uint16_t length)
{
  /* USER CODE BEGIN 6 */
  UNUSED(data);
  UNUSED(length);
  put_buf(data, length);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/* USER CODE BEGIN 7 */
/**
  * @brief  Send the report to the Host
  * @param  report: The report to be sent
  * @param  len: The report length
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */

int8_t USBD_MIDI_SendPacket_FS(uint8_t *report, uint16_t len)
{
#ifdef USE_USBD_COMPOSITE
  /* MIDI 没有对应的 CLASS_TYPE_xxx, MX_USB_DEVICE_Init() 的 composite 分支里
     也没有 USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_MIDI, ...),
     即复合设备配置下 MIDI 接口/端点根本不存在.
     这里直接返回失败, 而不是随便传一个 ClassId 去索引 pClassDataCmsit[] --
     那会拿到别的类的句柄, 往别人的端点上发数据. */
  UNUSED(report);
  UNUSED(len);
  return (int8_t)USBD_FAIL;
#else
  return (int8_t)USBD_MIDI_SendPacket(&hUsbDeviceFS, report, len);
#endif /* USE_USBD_COMPOSITE */
}

/* USER CODE END 7 */

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

