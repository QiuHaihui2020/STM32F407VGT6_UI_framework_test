/**
  ******************************************************************************
  * @file    flash_disk.c
  * @brief   基于 STM32F407VET6 片内 Flash 的 FatFs 物理层(diskio)驱动
  *
  * @note    只读 / 读写由 flash_disk.h 的 FLASH_DISK_READONLY 切换.
  *          只读模式下本文件中所有擦除、编程、中转搬移代码都被条件编译掉,
  *          只保留 memcpy 读取路径, 中转扇区那 128KB 可以让给代码区.
  *
  * @note    读写模式的设计要点(为什么这么写):
  *          1) 片内 Flash 只能"擦除后写入", 且擦除单位是整个物理扇区(此处 128KB),
  *             远大于 FatFs 的 512B 逻辑扇区. 而 F407 只有 128KB SRAM, 无法在 RAM
  *             中缓存整个物理扇区做 read-modify-write, 因此借用一个独立的 Flash
  *             扇区(FLASH_DISK_SWAP_ADDR)做中转. Flash 是内存映射的, 可直接读,
  *             搬移时不需要大块 RAM 缓冲.
  *          2) 三级快慢路径, 尽量避开昂贵的擦除:
  *             内容相同    -> 什么都不做
  *             目标全 0xFF -> 直接编程
  *             需要改写    -> 才走中转扇区(2 次 128KB 擦除)
  *             格式化后顺序写文件基本只命中前两条路径.
  *          3) 一次调用里落在同一物理扇区的多个逻辑扇区会合并成一次搬移,
  *             避免 FatFs 多扇区写时反复擦除同一个物理扇区.
  *
  * @warning 以下三条只在读写模式(FLASH_DISK_READONLY = 0)下成立:
  *          - 掉电风险: 走中转路径时, 在"擦除原扇区"到"从中转扇区搬回"之间掉电,
  *            该物理扇区数据会丢失(数据尚存于中转扇区, 但本驱动不做上电恢复).
  *            如需掉电安全, 需在中转扇区额外记录搬移标记并在 Init 时回滚.
  *          - 实时性: 128KB 扇区擦除耗时约 1~2s, 期间从 Flash 取指被硬件 stall,
  *            中断被严重延迟(USB 可能掉线, I2S 会断音). 不要在实时任务里频繁写盘.
  *          - 寿命: 片内 Flash 擦写寿命约 1 万次, 走中转路径的每次改写消耗 2 次.
  ******************************************************************************
  */

#include "flash_disk.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* 片内 Flash 按字(32bit)编程, 所有搬移与编程都以字为单位 */
#define FLASH_DISK_WORD_SIZE        (4UL)
#define FLASH_DISK_WORD_ERASED      (0xFFFFFFFFUL)

/* STM32F407 扇区分档: 前 64KB 为 16KB 扇区, 接着 64KB 为一个扇区, 其后均为 128KB */
#define FLASH_DISK_SMALL_REGION_END (64UL * 1024UL)
#define FLASH_DISK_MID_REGION_END   (128UL * 1024UL)
#define FLASH_DISK_SMALL_SECTOR     (16UL * 1024UL)
#define FLASH_DISK_MID_SECTOR       (64UL * 1024UL)
#define FLASH_DISK_LARGE_SECTOR     (128UL * 1024UL)

/* ---------------------------- 编译期配置自检 ---------------------------- */

#if (FLASH_DISK_SECTOR_SIZE % 4UL) != 0UL
#error "FLASH_DISK_SECTOR_SIZE 必须是 4 的整数倍: 片内 Flash 按字编程"
#endif

#if (FLASH_DISK_SIZE % FLASH_DISK_SECTOR_SIZE) != 0UL
#error "FLASH_DISK_SIZE 必须是 FLASH_DISK_SECTOR_SIZE 的整数倍"
#endif

#if (FLASH_DISK_BASE_ADDR + FLASH_DISK_SIZE) > (FLASH_DISK_FLASH_BASE + FLASH_DISK_FLASH_SIZE)
#error "磁盘区超出片内 Flash 范围: VET6 只有 512KB(0x08000000..0x0807FFFF)"
#endif

#if FLASH_DISK_SWAP_USED
#if (FLASH_DISK_SWAP_ADDR + FLASH_DISK_LARGE_SECTOR) > \
    (FLASH_DISK_FLASH_BASE + FLASH_DISK_FLASH_SIZE)
#error "中转扇区超出片内 Flash 范围"
#endif
#if (FLASH_DISK_SWAP_ADDR >= FLASH_DISK_BASE_ADDR) && \
    (FLASH_DISK_SWAP_ADDR < (FLASH_DISK_BASE_ADDR + FLASH_DISK_SIZE))
#error "中转扇区与磁盘区重叠: 请把 FLASH_DISK_SWAP_ADDR 移到磁盘区之外"
#endif
#endif /* FLASH_DISK_SWAP_USED */

/* ------------------------- 内部工具函数(两种模式共用) ------------------------- */

/**
  * @brief  取地址所在物理扇区的大小(字节).
  */
static uint32_t FlashDisk_PhySectorSize(uint32_t addr)
{
    uint32_t offset = addr - FLASH_DISK_FLASH_BASE;

    if (offset < FLASH_DISK_SMALL_REGION_END)
    {
        return FLASH_DISK_SMALL_SECTOR;
    }
    if (offset < FLASH_DISK_MID_REGION_END)
    {
        return FLASH_DISK_MID_SECTOR;
    }
    return FLASH_DISK_LARGE_SECTOR;
}

/**
  * @brief  取地址所在物理扇区的起始地址.
  */
static uint32_t FlashDisk_PhySectorBase(uint32_t addr)
{
    uint32_t offset      = addr - FLASH_DISK_FLASH_BASE;
    uint32_t sector_size = FlashDisk_PhySectorSize(addr);
    uint32_t region_base;

    if (offset < FLASH_DISK_SMALL_REGION_END)
    {
        region_base = 0UL;
    }
    else if (offset < FLASH_DISK_MID_REGION_END)
    {
        region_base = FLASH_DISK_SMALL_REGION_END;
    }
    else
    {
        region_base = FLASH_DISK_MID_REGION_END;
    }

    /* 同一档内各扇区等长, 因此按档内偏移向下对齐即可 */
    return FLASH_DISK_FLASH_BASE + region_base +
           (((offset - region_base) / sector_size) * sector_size);
}

#if (FLASH_DISK_READONLY == 0)

/* --------------------- 内部工具函数(仅读写模式需要) --------------------- */

/**
  * @brief  取地址所在物理扇区的 HAL 扇区号(FLASH_SECTOR_x).
  */
static uint32_t FlashDisk_AddrToPhySectorId(uint32_t addr)
{
    uint32_t offset = addr - FLASH_DISK_FLASH_BASE;

    if (offset < FLASH_DISK_SMALL_REGION_END)
    {
        return FLASH_SECTOR_0 + (offset / FLASH_DISK_SMALL_SECTOR);
    }
    if (offset < FLASH_DISK_MID_REGION_END)
    {
        return FLASH_SECTOR_4;
    }
    return FLASH_SECTOR_5 +
           ((offset - FLASH_DISK_MID_REGION_END) / FLASH_DISK_LARGE_SECTOR);
}

/**
  * @brief  判断一段 Flash 是否处于擦除态(全 0xFF), 即可直接编程而无需擦除.
  */
static uint8_t FlashDisk_IsBlank(uint32_t addr, uint32_t len)
{
    uint32_t i;

    for (i = 0UL; i < len; i += FLASH_DISK_WORD_SIZE)
    {
        if (*(const uint32_t *)(addr + i) != FLASH_DISK_WORD_ERASED)
        {
            return 0U;
        }
    }
    return 1U;
}

/**
  * @brief  擦除 addr 所在的整个物理扇区.
  * @note   调用方须已 HAL_FLASH_Unlock().
  */
static FlashDisk_StatusTypeDef FlashDisk_ErasePhySector(uint32_t addr)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0UL;

    erase_init.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks        = FLASH_BANK_1;
    erase_init.Sector       = FlashDisk_AddrToPhySectorId(addr);
    erase_init.NbSectors    = 1UL;
    /* VDD 为 2.7V~3.6V 时才允许按字(32bit)编程, 与本驱动的编程宽度一致 */
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        return FLASH_DISK_ERR_HAL;
    }
    return FLASH_DISK_OK;
}

/**
  * @brief  把一段 RAM 数据编程到已处于擦除态的 Flash.
  * @note   调用方须已 HAL_FLASH_Unlock(), 且 dst_addr 与 len 都是 4 字节对齐.
  */
static FlashDisk_StatusTypeDef FlashDisk_ProgramBytes(uint32_t dst_addr,
                                                      const uint8_t *p_src,
                                                      uint32_t len)
{
    uint32_t i;
    uint32_t word;

    for (i = 0UL; i < len; i += FLASH_DISK_WORD_SIZE)
    {
        /* p_src 来自 FatFs, 不保证 4 字节对齐, 用 memcpy 取字避免非对齐访问 */
        (void)memcpy(&word, &p_src[i], FLASH_DISK_WORD_SIZE);
        if (word == FLASH_DISK_WORD_ERASED)
        {
            continue;   /* 擦除态本就是 0xFFFFFFFF, 跳过可明显加快写入 */
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + i, (uint64_t)word) != HAL_OK)
        {
            return FLASH_DISK_ERR_HAL;
        }
    }
    return FLASH_DISK_OK;
}

#if FLASH_DISK_SWAP_USED
/**
  * @brief  借中转扇区改写一个物理扇区内的 n 个连续逻辑扇区.
  * @param  phy_base   物理扇区起始地址
  * @param  phy_size   物理扇区大小
  * @param  first_addr 待改写的第一个逻辑扇区地址(必在 phy_base 所属扇区内)
  * @param  n          待改写的逻辑扇区个数
  * @param  p_new      n 个逻辑扇区的新数据
  * @note   流程: 擦中转 -> 原扇区(替换掉待改写部分)搬入中转 -> 擦原扇区 ->
  *         中转搬回原扇区. 期间掉电会丢失该物理扇区数据, 见文件头 @warning.
  */
static FlashDisk_StatusTypeDef FlashDisk_RewriteViaSwap(uint32_t phy_base,
                                                        uint32_t phy_size,
                                                        uint32_t first_addr,
                                                        uint32_t n,
                                                        const uint8_t *p_new)
{
    FlashDisk_StatusTypeDef status;
    uint32_t new_begin = first_addr - phy_base;
    uint32_t new_len   = n * FLASH_DISK_SECTOR_SIZE;
    uint32_t offset;
    uint32_t word;

    status = FlashDisk_ErasePhySector(FLASH_DISK_SWAP_ADDR);
    if (status != FLASH_DISK_OK)
    {
        return status;
    }

    /* 原扇区整块搬入中转扇区, 落在改写范围内的字用新数据替换 */
    for (offset = 0UL; offset < phy_size; offset += FLASH_DISK_WORD_SIZE)
    {
        if ((offset >= new_begin) && (offset < (new_begin + new_len)))
        {
            (void)memcpy(&word, &p_new[offset - new_begin], FLASH_DISK_WORD_SIZE);
        }
        else
        {
            word = *(const uint32_t *)(phy_base + offset);
        }
        if (word == FLASH_DISK_WORD_ERASED)
        {
            continue;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              FLASH_DISK_SWAP_ADDR + offset, (uint64_t)word) != HAL_OK)
        {
            return FLASH_DISK_ERR_HAL;
        }
    }

    status = FlashDisk_ErasePhySector(phy_base);
    if (status != FLASH_DISK_OK)
    {
        return status;
    }

    /* 中转扇区搬回原扇区 */
    for (offset = 0UL; offset < phy_size; offset += FLASH_DISK_WORD_SIZE)
    {
        word = *(const uint32_t *)(FLASH_DISK_SWAP_ADDR + offset);
        if (word == FLASH_DISK_WORD_ERASED)
        {
            continue;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              phy_base + offset, (uint64_t)word) != HAL_OK)
        {
            return FLASH_DISK_ERR_HAL;
        }
    }

    /* 搬回后逐字校验: Flash 编程失败未必置错误标志, 必须回读确认 */
    for (offset = 0UL; offset < new_len; offset += FLASH_DISK_WORD_SIZE)
    {
        (void)memcpy(&word, &p_new[offset], FLASH_DISK_WORD_SIZE);
        if (*(const uint32_t *)(first_addr + offset) != word)
        {
            return FLASH_DISK_ERR_VERIFY;
        }
    }

    return FLASH_DISK_OK;
}
#endif /* FLASH_DISK_SWAP_USED */

#endif /* FLASH_DISK_READONLY == 0 */

/* -------------------------------- 对外接口 -------------------------------- */

uint32_t FlashDisk_GetSectorCount(void)
{
    return (uint32_t)(FLASH_DISK_SIZE / FLASH_DISK_SECTOR_SIZE);
}

uint32_t FlashDisk_GetSectorSize(void)
{
    return (uint32_t)FLASH_DISK_SECTOR_SIZE;
}

uint32_t FlashDisk_GetEraseBlockSectors(void)
{
    return FlashDisk_PhySectorSize(FLASH_DISK_BASE_ADDR) / (uint32_t)FLASH_DISK_SECTOR_SIZE;
}

FlashDisk_StatusTypeDef FlashDisk_Init(void)
{
#if FLASH_DISK_SWAP_USED
    /* 中转扇区必须不小于磁盘区里最大的物理扇区, 否则搬移会越界 */
    uint32_t disk_last_addr = FLASH_DISK_BASE_ADDR + FLASH_DISK_SIZE - 1UL;

    if (FlashDisk_PhySectorSize(FLASH_DISK_SWAP_ADDR) <
        FlashDisk_PhySectorSize(disk_last_addr))
    {
        return FLASH_DISK_ERR_PARAM;
    }
#endif

    /* 磁盘区必须落在物理扇区边界上, 否则擦除会破坏区外数据 */
    if (FlashDisk_PhySectorBase(FLASH_DISK_BASE_ADDR) != (uint32_t)FLASH_DISK_BASE_ADDR)
    {
        return FLASH_DISK_ERR_PARAM;
    }

    return FLASH_DISK_OK;
}

FlashDisk_StatusTypeDef FlashDisk_Read(uint8_t *p_buf, uint32_t sector, uint32_t count)
{
    if ((p_buf == NULL) || (count == 0UL) ||
        ((sector + count) > FlashDisk_GetSectorCount()))
    {
        return FLASH_DISK_ERR_PARAM;
    }

    /* 片内 Flash 是内存映射的, 读直接 memcpy 即可 */
    (void)memcpy(p_buf,
                 (const void *)(FLASH_DISK_BASE_ADDR + (sector * FLASH_DISK_SECTOR_SIZE)),
                 count * FLASH_DISK_SECTOR_SIZE);

    return FLASH_DISK_OK;
}

#if FLASH_DISK_READONLY

FlashDisk_StatusTypeDef FlashDisk_Write(const uint8_t *p_buf, uint32_t sector, uint32_t count)
{
    /* 只读模式: 保留符号以免调用方需要跟着改条件编译, 但一律拒绝写入 */
    (void)p_buf;
    (void)sector;
    (void)count;

    return FLASH_DISK_ERR_READONLY;
}

#else /* 读写模式 */

FlashDisk_StatusTypeDef FlashDisk_Write(const uint8_t *p_buf, uint32_t sector, uint32_t count)
{
    FlashDisk_StatusTypeDef status = FLASH_DISK_OK;
    uint32_t done = 0UL;

    if ((p_buf == NULL) || (count == 0UL) ||
        ((sector + count) > FlashDisk_GetSectorCount()))
    {
        return FLASH_DISK_ERR_PARAM;
    }

    HAL_FLASH_Unlock();

    while ((done < count) && (status == FLASH_DISK_OK))
    {
        uint32_t cur_addr = FLASH_DISK_BASE_ADDR +
                            ((sector + done) * FLASH_DISK_SECTOR_SIZE);
        uint32_t phy_base = FlashDisk_PhySectorBase(cur_addr);
        uint32_t phy_size = FlashDisk_PhySectorSize(cur_addr);
        uint32_t group    = ((phy_base + phy_size) - cur_addr) / FLASH_DISK_SECTOR_SIZE;
        uint8_t  need_swap = 0U;
        uint32_t i;

        /* 同一物理扇区内的逻辑扇区合并处理, 一次搬移解决整组 */
        if (group > (count - done))
        {
            group = count - done;
        }

        for (i = 0UL; i < group; i++)
        {
            uint32_t       addr  = cur_addr + (i * FLASH_DISK_SECTOR_SIZE);
            const uint8_t *p_src = &p_buf[(done + i) * FLASH_DISK_SECTOR_SIZE];

            if (memcmp((const void *)addr, p_src, FLASH_DISK_SECTOR_SIZE) == 0)
            {
                continue;   /* 内容一致, 不需要动 Flash */
            }
            if (FlashDisk_IsBlank(addr, FLASH_DISK_SECTOR_SIZE) == 0U)
            {
                need_swap = 1U;
                break;
            }
        }

        if (need_swap == 0U)
        {
            /* 快路径: 目标处于擦除态, 直接编程, 内容相同的跳过 */
            for (i = 0UL; (i < group) && (status == FLASH_DISK_OK); i++)
            {
                uint32_t       addr  = cur_addr + (i * FLASH_DISK_SECTOR_SIZE);
                const uint8_t *p_src = &p_buf[(done + i) * FLASH_DISK_SECTOR_SIZE];

                if (memcmp((const void *)addr, p_src, FLASH_DISK_SECTOR_SIZE) != 0)
                {
                    status = FlashDisk_ProgramBytes(addr, p_src, FLASH_DISK_SECTOR_SIZE);
                }
            }
        }
        else
        {
#if FLASH_DISK_SWAP_USED
            status = FlashDisk_RewriteViaSwap(phy_base, phy_size, cur_addr, group,
                                              &p_buf[done * FLASH_DISK_SECTOR_SIZE]);
#else
            /* 未启用中转扇区: 无法在不擦除整个物理扇区的前提下改写已有数据 */
            status = FLASH_DISK_ERR_NOT_BLANK;
#endif
        }

        done += group;
    }

    HAL_FLASH_Lock();

    return status;
}

FlashDisk_StatusTypeDef FlashDisk_EraseAll(void)
{
    FlashDisk_StatusTypeDef status = FLASH_DISK_OK;
    uint32_t addr = FLASH_DISK_BASE_ADDR;
    uint32_t end  = FLASH_DISK_BASE_ADDR + FLASH_DISK_SIZE;

    HAL_FLASH_Unlock();

    while ((addr < end) && (status == FLASH_DISK_OK))
    {
        uint32_t phy_size = FlashDisk_PhySectorSize(addr);

        /* 已是擦除态的扇区直接跳过: 擦一次就消耗一次寿命, 能省则省 */
        if (FlashDisk_IsBlank(addr, phy_size) == 0U)
        {
            status = FlashDisk_ErasePhySector(addr);
        }
        addr += phy_size;
    }

    HAL_FLASH_Lock();

    return status;
}

#endif /* FLASH_DISK_READONLY */
