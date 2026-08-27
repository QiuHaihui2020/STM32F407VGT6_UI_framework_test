/**
  ******************************************************************************
  * @file    flash_disk.h
  * @brief   基于 STM32F407VET6 片内 Flash 的 FatFs 物理层(diskio)驱动
  * @note    支持只读 / 读写两种模式, 由 FLASH_DISK_READONLY 一个宏切换.
  *          只读模式不需要中转扇区, 可把那 128KB 让给代码区.
  ******************************************************************************
  */

#ifndef __FLASH_DISK_H__
#define __FLASH_DISK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========================= 用户可配置区 开始 ========================= */

/**
  * @brief 只读模式开关. 这是本驱动最主要的取舍开关, 决定要不要中转扇区.
  *
  *        1 = 只读.
  *            - 不需要中转扇区, S5 那 128KB 可以让给代码区(代码区 256KB).
  *            - FlashDisk_Write() 直接返回 FLASH_DISK_ERR_READONLY, 写路径与
  *              中转搬移代码全部被条件编译掉, 不占 Flash.
  *            - 没有 128KB 擦除(1~2s 卡顿)、没有擦写寿命消耗、没有搬移掉电风险.
  *            - 磁盘内容必须离线预置: 在 PC 上做一个 FLASH_DISK_SIZE 大小的 FAT
  *              镜像, 烧写时下载到 FLASH_DISK_BASE_ADDR.
  *            - 必须同时把 ffconf.h 的 _FS_READONLY 设为 1、_USE_MKFS 设为 0
  *              (在 CubeMX 的 FATFS 配置里改, 否则重新生成会被覆盖).
  *              user_diskio.c 里有编译期检查会拦住不一致的配置.
  *            - Keil 工程 IROM1 大小可放宽到 0x40000 (256KB).
  *
  *        0 = 读写.
  *            - 需要中转扇区(见 FLASH_DISK_SWAP_ADDR), 代码区只剩 128KB.
  *            - 运行期可创建 / 修改 / 删除文件.
  *            - Keil 工程 IROM1 大小必须限制到 0x20000 (128KB), 否则代码可能
  *              被链接到中转扇区或磁盘区, 一写盘就把自己擦掉.
  */
#define FLASH_DISK_READONLY         (0)

/**
  * @brief 磁盘区起始地址, 必须与 Flash 物理扇区边界对齐.
  * @note  STM32F407VET6 只有 512KB Flash(0x08000000..0x0807FFFF), 物理扇区布局:
  *          S0..S3    16KB   0x08000000 .. 0x0800FFFF
  *          S4        64KB   0x08010000 .. 0x0801FFFF
  *          S5       128KB   0x08020000 .. 0x0803FFFF
  *          S6       128KB   0x08040000 .. 0x0805FFFF
  *          S7       128KB   0x08060000 .. 0x0807FFFF
  *        注意 VGT6(1MB) 才有 S8..S11, VET6 上 0x08080000 以上是无效地址.
  *
  *        两种模式下磁盘区都是 S6+S7, 差别只在 S5 的归属:
  *          FLASH_DISK_READONLY = 1: 代码 S0..S5(256KB) | 磁盘 S6..S7(256KB)
  *          FLASH_DISK_READONLY = 0: 代码 S0..S4(128KB) | 中转 S5 | 磁盘 S6..S7
  */
#define FLASH_DISK_BASE_ADDR        (0x08040000UL)

/**
  * @brief 磁盘容量, 必须是整数个物理扇区.
  * @note  改容量时必须同时调整 FLASH_DISK_BASE_ADDR, 让磁盘区始终落在 Flash 尾部.
  *        例如改成 128KB 磁盘: BASE=0x08060000 (仅 S7), 读写模式下中转可用 S6.
  */
#define FLASH_DISK_SIZE             (256UL * 1024UL)

/**
  * @brief 逻辑扇区大小, 必须与 ffconf.h 中的 _MIN_SS / _MAX_SS 保持一致.
  * @note  FatFs 只接受 512 / 1024 / 2048 / 4096.
  */
#define FLASH_DISK_SECTOR_SIZE      (512UL)

/**
  * @brief 中转(swap)扇区起始地址, 仅 FLASH_DISK_READONLY = 0 时有意义.
  * @note  用于改写已有数据时的 read-modify-write: 片内 Flash 擦除单位是 128KB,
  *        远大于 512B 逻辑扇区, 而 F407 只有 128KB SRAM 装不下整个物理扇区的备份,
  *        因此借一个 Flash 扇区周转(Flash 内存映射可直读, 不需要大 RAM 缓冲).
  *        必须是独立的物理扇区, 容量不小于磁盘区中最大的物理扇区(此处 128KB),
  *        且不能与磁盘区或代码区重叠. 默认 S5.
  */
#define FLASH_DISK_SWAP_ADDR        (0x08020000UL)

/**
  * @brief 读写模式下是否启用中转扇区, 仅 FLASH_DISK_READONLY = 0 时有意义.
  *        1 = 支持任意随机改写(推荐). 代价: 一次改写触发 2 次 128KB 擦除,
  *            耗时约 2~4s, 期间从 Flash 取指被硬件 stall, 中断被严重延迟
  *            (USB 会掉线, I2S 会断音).
  *        0 = 只允许写入尚未编程过(全 0xFF)的区域, 改写已有数据返回
  *            FLASH_DISK_ERR_NOT_BLANK.
  *            注意: FAT 表和目录项每次增删文件都要改写, 因此这个组合实际上
  *            只能写第一份数据, 之后就会失败. 想省下这 128KB 应该用
  *            FLASH_DISK_READONLY = 1, 而不是把本宏置 0.
  */
#define FLASH_DISK_SWAP_ENABLE      (1)

/* ========================= 用户可配置区 结束 ========================= */

/** @brief 片内 Flash 可用地址范围, 用于配置越界检查(VET6 = 512KB) */
#define FLASH_DISK_FLASH_BASE       (0x08000000UL)
#define FLASH_DISK_FLASH_SIZE       (512UL * 1024UL)

/**
  * @brief 派生宏: 是否真正编译中转搬移代码.
  * @note  只读模式下无论 FLASH_DISK_SWAP_ENABLE 取何值都不使用中转扇区,
  *        用这个派生宏统一判断, 避免在实现里到处写两个条件.
  */
#if (FLASH_DISK_READONLY == 0) && (FLASH_DISK_SWAP_ENABLE != 0)
#define FLASH_DISK_SWAP_USED        (1)
#else
#define FLASH_DISK_SWAP_USED        (0)
#endif

/** @brief FlashDisk 各 API 的返回状态 */
typedef enum
{
    FLASH_DISK_OK = 0,          /*!< 操作成功 */
    FLASH_DISK_ERR_PARAM,       /*!< 参数非法: 扇区越界 / 配置不合法 */
    FLASH_DISK_ERR_HAL,         /*!< HAL Flash 擦除或编程失败 */
    FLASH_DISK_ERR_NOT_BLANK,   /*!< 目标区域非空, 且未启用中转扇区 */
    FLASH_DISK_ERR_VERIFY,      /*!< 写入后回读校验不一致 */
    FLASH_DISK_ERR_READONLY     /*!< 只读模式下尝试写入 */
} FlashDisk_StatusTypeDef;

/**
  * @brief  初始化 Flash 磁盘, 校验配置的合法性.
  * @retval FLASH_DISK_OK 表示配置合法, 磁盘可用.
  * @note   片内 Flash 无需上电初始化时序, 本函数只做配置自检.
  */
FlashDisk_StatusTypeDef FlashDisk_Init(void);

/**
  * @brief  读取若干个连续逻辑扇区.
  * @param  p_buf  目标缓冲区, 长度不小于 count * FLASH_DISK_SECTOR_SIZE.
  * @param  sector 起始逻辑扇区号(从 0 开始).
  * @param  count  扇区个数.
  * @retval 见 FlashDisk_StatusTypeDef.
  */
FlashDisk_StatusTypeDef FlashDisk_Read(uint8_t *p_buf, uint32_t sector, uint32_t count);

/**
  * @brief  写入若干个连续逻辑扇区.
  * @param  p_buf  源数据, 长度不小于 count * FLASH_DISK_SECTOR_SIZE.
  * @param  sector 起始逻辑扇区号(从 0 开始).
  * @param  count  扇区个数.
  * @retval 见 FlashDisk_StatusTypeDef; 只读模式恒返回 FLASH_DISK_ERR_READONLY.
  * @note   读写模式下分三级路径以避开昂贵的擦除:
  *           内容与 Flash 现有数据一致 -> 什么都不做
  *           目标区域仍为全 0xFF       -> 直接编程
  *           需要改写已有数据          -> 才走中转扇区搬移
  *         这样 FatFs 反复回写同一份 FAT / 目录项时不会每次都擦除.
  */
FlashDisk_StatusTypeDef FlashDisk_Write(const uint8_t *p_buf, uint32_t sector, uint32_t count);

/**
  * @brief  获取磁盘的逻辑扇区总数.
  */
uint32_t FlashDisk_GetSectorCount(void);

/**
  * @brief  获取逻辑扇区大小(字节).
  */
uint32_t FlashDisk_GetSectorSize(void);

/**
  * @brief  获取真实擦除块大小, 以逻辑扇区个数表示(128KB / 512B = 256).
  * @note   仅供诊断参考, 不上报给 diskio 的 GET_BLOCK_SIZE.
  *         原因: f_mkfs 会把数据区起始对齐到这个粒度, 而本磁盘总共只有
  *         512 个逻辑扇区, 对齐到 256 会白扔掉一半容量. 本驱动的写路径已经
  *         做了"同物理扇区合并 + 三级快路径", 不依赖 FAT 表对齐, 因此
  *         user_diskio.c 里 GET_BLOCK_SIZE 直接上报 1(无对齐要求).
  */
uint32_t FlashDisk_GetEraseBlockSectors(void);

#if (FLASH_DISK_READONLY == 0)
/**
  * @brief  擦除整个磁盘区, 把所有物理扇区恢复为 0xFF.
  * @retval 见 FlashDisk_StatusTypeDef.
  * @note   建议在 f_mkfs() 之前调用: 空白区域上写 FAT 全程走"直接编程"快路径,
  *         不触发中转搬移, 格式化快很多. 已处于擦除态的扇区会被跳过以省寿命.
  * @warning 会丢弃磁盘上的全部数据.
  */
FlashDisk_StatusTypeDef FlashDisk_EraseAll(void);
#endif /* FLASH_DISK_READONLY == 0 */

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_DISK_H__ */
