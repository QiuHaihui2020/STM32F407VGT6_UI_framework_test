/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
 /* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "flash_disk.h"          /* 片内 Flash 物理层 */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;

/* 本卷只有一个物理驱动器(片内 Flash) */
#define USER_DISK_PDRV      (0U)

/* ---- 配置一致性检查: 两侧不匹配会在运行期才暴露, 提前拦在编译期 ---- */
#if (FLASH_DISK_READONLY != 0) && (_FS_READONLY == 0)
#error "FLASH_DISK_READONLY=1 时必须把 ffconf.h 的 _FS_READONLY 也设为 1(在 CubeMX 的 FATFS 配置里改, 否则重新生成会被覆盖)"
#endif
#if (FLASH_DISK_READONLY == 0) && (_FS_READONLY != 0)
#error "ffconf.h 的 _FS_READONLY=1 时应把 flash_disk.h 的 FLASH_DISK_READONLY 也设为 1, 才能省下中转扇区那 128KB"
#endif
#if (FLASH_DISK_READONLY != 0) && (_USE_MKFS != 0)
#error "只读模式下请把 ffconf.h 的 _USE_MKFS 设为 0: f_mkfs 需要写盘"
#endif
#if (FLASH_DISK_READONLY != 0) && (_FS_LOCK != 0)
#error "只读模式下请把 ffconf.h 的 _FS_LOCK 设为 0: FatFs 的文件锁只在可写配置下有意义"
#endif
#if FLASH_DISK_SECTOR_SIZE != _MAX_SS
#error "FLASH_DISK_SECTOR_SIZE 必须与 ffconf.h 的 _MAX_SS 一致"
#endif

  /* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    /* 片内 Flash 不需要上电初始化时序, FlashDisk_Init() 只做地址配置自检 */
    if (pdrv != USER_DISK_PDRV)
    {
        Stat = STA_NOINIT;
        return Stat;
    }
    if (FlashDisk_Init() != FLASH_DISK_OK)
    {
        Stat = STA_NOINIT;
        return Stat;
    }
    Stat = 0U;
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    if (pdrv != USER_DISK_PDRV)
    {
        return STA_NOINIT;
    }
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    if ((pdrv != USER_DISK_PDRV) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((Stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    if (FlashDisk_Read((uint8_t *)buff, (uint32_t)sector, (uint32_t)count) != FLASH_DISK_OK)
    {
        return RES_ERROR;
    }
    return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    if ((pdrv != USER_DISK_PDRV) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((Stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }
    if (FlashDisk_Write((const uint8_t *)buff, (uint32_t)sector, (uint32_t)count) != FLASH_DISK_OK)
    {
        return RES_ERROR;
    }
    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    DRESULT res;

    if (pdrv != USER_DISK_PDRV)
    {
        return RES_PARERR;
    }
    if ((Stat & STA_NOINIT) != 0U)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
    case CTRL_SYNC:
        /* FlashDisk_Write() 是同步写, 没有需要冲刷的缓存 */
        res = RES_OK;
        break;

    case GET_SECTOR_COUNT:
        *(DWORD *)buff = (DWORD)FlashDisk_GetSectorCount();
        res = RES_OK;
        break;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = (WORD)FlashDisk_GetSectorSize();
        res = RES_OK;
        break;

    case GET_BLOCK_SIZE:
        /* 上报 1 = 无对齐要求.
           不上报真实擦除粒度(128KB/512B = 256): f_mkfs 会把数据区起始对齐到该
           粒度, 而本磁盘总共只有 512 个逻辑扇区, 对齐到 256 会白扔一半容量.
           FlashDisk_Write() 内部已做"同物理扇区合并 + 三级快路径", 不依赖对齐. */
        *(DWORD *)buff = 1UL;
        res = RES_OK;
        break;

    default:
        res = RES_PARERR;
        break;
    }

    return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

