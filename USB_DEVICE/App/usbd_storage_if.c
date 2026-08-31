/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_storage_if.c
  * @version        : v1.0_Cube
  * @brief          : Memory management layer.
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
#include "usbd_storage_if.h"

/* USER CODE BEGIN INCLUDE */
#include "main.h"          /* SDIO_ENABLE */
#if SDIO_ENABLE
#include "sdio.h"
#include "sd_diskio.h"
#else
#include "flash_disk.h"    /* SDIO 去掉后, MSC 的后端介质换成片内 Flash 磁盘 */
#endif /* SDIO_ENABLE */
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

/** @defgroup USBD_STORAGE
  * @brief Usb mass storage device module
  * @{
  */

/** @defgroup USBD_STORAGE_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Defines
  * @brief Private defines.
  * @{
  */

#define STORAGE_LUN_NBR                  1

/* USER CODE BEGIN PRIVATE_DEFINES */

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Variables
  * @brief Private variables.
  * @{
  */

/* USER CODE BEGIN INQUIRY_DATA_FS */
/** USB Mass storage Standard Inquiry Data. */
const int8_t STORAGE_Inquirydata_FS[] = {/* 36 */

  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  'S', 'T', 'M', ' ', ' ', ' ', ' ', ' ', /* Manufacturer : 8 bytes */
  'P', 'r', 'o', 'd', 'u', 'c', 't', ' ', /* Product      : 16 Bytes */
  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
  '0', '.', '0' ,'1'                      /* Version      : 4 Bytes */
};
/* USER CODE END INQUIRY_DATA_FS */

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t STORAGE_Init_FS(uint8_t lun);
static int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t STORAGE_IsReady_FS(uint8_t lun);
static int8_t STORAGE_IsWriteProtected_FS(uint8_t lun);
static int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun_FS(void);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_StorageTypeDef USBD_Storage_Interface_fops_FS =
{
  STORAGE_Init_FS,
  STORAGE_GetCapacity_FS,
  STORAGE_IsReady_FS,
  STORAGE_IsWriteProtected_FS,
  STORAGE_Read_FS,
  STORAGE_Write_FS,
  STORAGE_GetMaxLun_FS,
  (int8_t *)STORAGE_Inquirydata_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the storage unit (medium) over USB FS IP
  * @param  lun: Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Init_FS(uint8_t lun)
{
  /* USER CODE BEGIN 2 */
#if !SDIO_ENABLE
    /* 后端为片内 Flash: 无上电时序, FlashDisk_Init() 只做配置自检, 幂等,
       与 FatFs 的 USER_initialize() 各调各的, 重复调用无副作用. */
    (void)lun;
    if (FlashDisk_Init() != FLASH_DISK_OK)
    {
        log_debug("STORAGE_Init_FS: FlashDisk_Init fail\n");
        return USBD_FAIL;
    }
    return USBD_OK;
#else
    //需要提取初始化sd卡，这里初始化sd卡会卡死
    if(HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER)
    {
        log_debug("STORAGE_Init_FS\n");
        return USBD_OK;
    }
    log_debug("STORAGE_Init_FS fail\n");
    return USBD_FAIL;
#endif /* SDIO_ENABLE */
  /* USER CODE END 2 */
}

/**
  * @brief  Returns the medium capacity.
  * @param  lun: Logical unit number.
  * @param  block_num: Number of total block number.
  * @param  block_size: Block size.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  /* USER CODE BEGIN 3 */
#if !SDIO_ENABLE
    (void)lun;
    /* 容量直接问驱动, 不要在这里另写一份常量: 与 flash_disk.h 的
       FLASH_DISK_SIZE / FLASH_DISK_SECTOR_SIZE 保持单一事实来源 */
    *block_num  = FlashDisk_GetSectorCount();
    *block_size = (uint16_t)FlashDisk_GetSectorSize();
    /* 主机发出 READ_CAPACITY 才会走到这里, 是 MSC 真正跑通的第一个标志 */
    // log_debug("STORAGE_GetCapacity_FS: %u blocks x %u B\n",
    //           (unsigned int)*block_num, (unsigned int)*block_size);
    return USBD_OK;
#else
	HAL_SD_CardInfoTypeDef info;
	if(HAL_SD_GetCardState(&hsd) ==  HAL_SD_CARD_TRANSFER)
	{
		HAL_SD_GetCardInfo(&hsd, &info);
		*block_num = info.LogBlockNbr;
		*block_size = info.LogBlockSize;
        //log_debug("SD_GetCardInfo: %d, %d\r\n", *block_num, *block_size);
		return  USBD_OK;
	}
    log_debug("STORAGE_GetCapacity_FS Fail !!!\n");
	return  USBD_FAIL;
    
#endif /* SDIO_ENABLE */
  /* USER CODE END 3 */
}

/**
  * @brief   Checks whether the medium is ready.
  * @param  lun:  Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_IsReady_FS(uint8_t lun)
{
  /* USER CODE BEGIN 4 */
#if !SDIO_ENABLE
    /* 片内 Flash 焊在板上, 介质恒定在位 */
    (void)lun;
    return USBD_OK;
#else
    if(HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER)
    {
        return USBD_OK;
    }
    log_debug("STORAGE_IsReady_FS Fail !!!\n");
    return USBD_FAIL;

#endif /* SDIO_ENABLE */
  /* USER CODE END 4 */
}

/**
  * @brief  Checks whether the medium is write protected.
  * @param  lun: Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_IsWriteProtected_FS(uint8_t lun)
{
  /* USER CODE BEGIN 5 */
    //log_debug("STORAGE_IsWriteProtected_FS: %d\r\n", lun);
  (void)lun;
#if (!SDIO_ENABLE) && (FLASH_DISK_READONLY != 0)
  /* 返回非 0 = 写保护. SCSI 层据此在 MODE SENSE 里置 WP 位, 并让 WRITE(10)
     直接回 NOT_READY/WRITE_PROTECTED. 不上报的话 Windows 挂载后会去写
     System Volume Information, 要等写失败才报错, 现象比写保护提示难查得多. */
  return (USBD_FAIL);
#else
  return (USBD_OK);
#endif
  /* USER CODE END 5 */
}

/**
  * @brief  Reads data from the medium.
  * @param  lun: Logical unit number.
  * @param  buf: data buffer.
  * @param  blk_addr: Logical block address.
  * @param  blk_len: Blocks number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 6 */
#if !SDIO_ENABLE
    (void)lun;
    if (FlashDisk_Read(buf, blk_addr, blk_len) != FLASH_DISK_OK)
    {
        log_debug("STORAGE_Read_FS fail: blk %u, len %u\n",
                  (unsigned int)blk_addr, (unsigned int)blk_len);
        return USBD_FAIL;
    }
    return USBD_OK;
#else
    int8_t ret = USBD_FAIL;  
    if( HAL_SD_ReadBlocks_DMA(&hsd, buf, blk_addr, blk_len) == HAL_OK )
    // if( HAL_SD_ReadBlocks(&hsd, buf, blk_addr,  blk_len, HAL_MAX_DELAY) == HAL_OK )
    {
        ret = USBD_OK;
        //while(HAL_SD_GetState(&hsd) == HAL_SD_STATE_BUSY){};
        while( HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER ){};
    } else {
        ret = USBD_FAIL;
        log_debug("STORAGE_Read_FS Fail !!!\n");
    } 
    return ret;
#endif /* SDIO_ENABLE */
  /* USER CODE END 6 */
}

/**
  * @brief  Writes data into the medium.
  * @param  lun: Logical unit number.
  * @param  buf: data buffer.
  * @param  blk_addr: Logical block address.
  * @param  blk_len: Blocks number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 7 */
#if !SDIO_ENABLE
    (void)lun;
#if FLASH_DISK_READONLY
    /* 只读配置下正常不该走到这里: STORAGE_IsWriteProtected_FS() 已上报写保护,
       SCSI 层在 WRITE(10) 阶段就用 WRITE_PROTECTED 挡掉了. 留个日志兜底. */
    (void)buf; (void)blk_addr; (void)blk_len;
    log_debug("STORAGE_Write_FS: medium is read-only\n");
    return USBD_FAIL;
#else
    if (FlashDisk_Write(buf, blk_addr, blk_len) != FLASH_DISK_OK)
    {
        log_debug("STORAGE_Write_FS fail: blk %u, len %u\n",
                  (unsigned int)blk_addr, (unsigned int)blk_len);
        return USBD_FAIL;
    }
    return USBD_OK;
#endif /* FLASH_DISK_READONLY */
#else

    int8_t ret = USBD_FAIL; 
    if( HAL_SD_WriteBlocks_DMA(&hsd, buf, blk_addr, blk_len) == HAL_OK )
    // if( HAL_SD_WriteBlocks(&hsd, buf, blk_addr, blk_len, HAL_MAX_DELAY) == HAL_OK )
    {
        ret = USBD_OK;
        //while(HAL_SD_GetState(&hsd) == HAL_SD_STATE_BUSY){};
        while( HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER ){};
    }  else {
        ret = USBD_FAIL;
        log_debug("STORAGE_Write_FS Fail !!!\n");
    }
    return ret;
#endif /* SDIO_ENABLE */
  /* USER CODE END 7 */
}

/**
  * @brief  Returns the Max Supported LUNs.
  * @param  None
  * @retval Lun(s) number.
  */
int8_t STORAGE_GetMaxLun_FS(void)
{
  /* USER CODE BEGIN 8 */
//   log_debug("STORAGE_GetMaxLun_FS: %d\r\n", STORAGE_LUN_NBR);
    return (STORAGE_LUN_NBR - 1);
  /* USER CODE END 8 */
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */

