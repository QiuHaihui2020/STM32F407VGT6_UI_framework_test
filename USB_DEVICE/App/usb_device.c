/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB Device
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

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_customhid.h"
#include "usbd_custom_hid_if.h"

/* USER CODE BEGIN Includes */
#include "usbd_msc.h"
#include "usbd_storage_if.h"
#include "usbd_audio.h"
#include "usbd_audio_if.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "log_debug.h"
#include "usbd_midi.h"
#include "usbd_midi_if.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* usbd_conf.c 里定义, 下面重新划分 FIFO 时要用 */
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Device Core handle declaration. */
USBD_HandleTypeDef hUsbDeviceFS;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB device Library, add supported class and start the library
  * @retval None
  */
void MX_USB_DEVICE_Init(void)
{
  /* USER CODE BEGIN USB_DEVICE_Init_PreTreatment */
#ifndef USE_USBD_COMPOSITE

  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_MIDI) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_MIDI_RegisterInterface(&hUsbDeviceFS, &USBD_MIDI_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }
return;


  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_AUDIO) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_AUDIO_RegisterInterface(&hUsbDeviceFS, &USBD_AUDIO_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }
    return;
#endif

#if 0 //MSC
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    log_debug("USB init error\n");
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_MSC) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_Storage_Interface_fops_FS) != USBD_OK)
  {
	  log_debug("USBD_MSC_RegisterStorage error\n");
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
	  log_debug("USBD_Start error\n");
    Error_Handler();
  }
  return;
#endif

#ifdef USE_USBD_COMPOSITE
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    log_debug("USB init error\n");
    Error_Handler();
  }

  /* ------------------------------------------------------------------
     重新划分 OTG_FS 的 FIFO. 必须放在这里而不是 usbd_conf.c:
     usbd_conf.c 的 USBD_LL_Init() 属于 CubeMX 生成区, 没有 USER CODE 包裹,
     重新生成就会被打回只分配到 Tx1 的模板值. 本函数这一段在
     USER CODE BEGIN/END USB_DEVICE_Init_PreTreatment 之间, 不会被覆盖.

     USBD_Init() 内部已经调过 USBD_LL_Init() 铺好模板值, 这里再覆盖一遍;
     此时端点尚未打开(要等 SET_CONFIGURATION 才 OpenEP), 改 FIFO 寄存器安全.

     OTG_FS 专用 FIFO RAM 共 1.25KB = 320 个 32bit word, 下列各项之和必须
     <= 320. 每个用到的 IN 端点都要显式分配: HAL_PCDEx_SetTxFiFo() 的偏移是
     把 DIEPTXF[0..fifo-2] 的 size 累加出来的, 跳过任何一个, 它读到的是复位
     值而不是 0, 后面所有 FIFO 的起始地址就全错位了.

       Rx    0x80 = 128   共享接收
       Tx0   0x40 =  64   EP0 IN  控制传输
       Tx1   0x20 =  32   EP1 IN  预留(MIDI / CDC)
       Tx2   0x20 =  32   EP2 IN  预留
       Tx3   0x40 =  64   EP3 IN  MSC BULK IN (MSC_EPIN_ADDR = 0x83)
                   ----
                    320
     漏掉 Tx3 的现象: 枚举能走完(EP0 有 FIFO), 但 SCSI 阶段设备发不出 CSW,
     主机反复重试 READ_CAPACITY, 表现为枚举后没有下文. */
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x40);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x20);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x20);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 3, 0x40);

#if USBD_CDC_CMPSIT_ENABLE
    if(USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
  {
    log_debug("USBD_CDC_RegisterInterface error\n");
    Error_Handler();
  }

if(USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_CDC,CLASS_TYPE_CDC,0) != USBD_OK)
  {
    log_debug("USBD_RegisterClassComposite USBD_CDC error\n");
    Error_Handler();
  }
#endif 

#if USBD_MSC_CMPSIT_ENABLE
  if (USBD_MSC_RegisterStorage(&hUsbDeviceFS, &USBD_Storage_Interface_fops_FS) != USBD_OK)
  {
	  log_debug("USBD_MSC_RegisterStorage error\n");
    Error_Handler();
  }


  if(USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_MSC,CLASS_TYPE_MSC,0) != USBD_OK)
  {
    log_debug("USBD_RegisterClassComposite USBD_MSC error\n");
    Error_Handler();
  }
#endif

#if USBD_AUDIO_CMPSIT_ENABLE
  if (USBD_AUDIO_RegisterInterface(&hUsbDeviceFS, &USBD_AUDIO_fops_FS) != USBD_OK)
  {
	  log_debug("USBD_MSC_RegisterStorage error\n");
    Error_Handler();
  }


  if(USBD_RegisterClassComposite(&hUsbDeviceFS, &USBD_AUDIO,CLASS_TYPE_AUDIO,0) != USBD_OK)
  {
    log_debug("USBD_RegisterClassComposite USBD_MSC error\n");
    Error_Handler();
  }

#endif
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
	  log_debug("USBD_Start error\n");
    Error_Handler();
  }

  return;
#endif
  /* USER CODE END USB_DEVICE_Init_PreTreatment */

  /* Init Device Library, add supported class and start the library. */
  if (USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CUSTOM_HID) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_CUSTOM_HID_RegisterInterface(&hUsbDeviceFS, &USBD_CustomHID_fops_FS) != USBD_OK)
  {
    Error_Handler();
  }
  if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */

  /* USER CODE END USB_DEVICE_Init_PostTreatment */
}

/**
  * @}
  */

/**
  * @}
  */

