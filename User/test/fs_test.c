#include "test.h"
#include "main.h"
#include "fatfs.h"
#include "flash_disk.h"    /* FlashDisk_EraseAll() / FLASH_DISK_* 配置 */
#if SDIO_ENABLE            /* main.h 中的开关 */
#include "sdio.h"
#endif /* SDIO_ENABLE */
#include "log_debug.h"
#include <string.h>        /* memcmp / strlen */

/* 回读比对缓冲大小, 同时限制单次测试数据的长度上限 */
#define FS_TEST_BUF_SIZE        (64U)

#define FS_TEST_FILE_PATH       "test.txt"
#define FS_TEST_DATA_ROUND1     "Hello, world!"
#define FS_TEST_DATA_ROUND2     "Round2: rewrite via swap sector."

#if !_FS_READONLY

#if _USE_MKFS
/* f_mkfs 工作缓冲. 文件级 static: 首次格式化与 round 0 共用一份,
   避免两处各占 512B; 放静态区也免得在栈上开这么大的数组 */
static BYTE s_mkfs_work[_MAX_SS];

/**
  * @brief  强制重新格式化: 卸载 -> 整区擦除 -> f_mkfs -> 重新挂载.
  * @param  p_fs 重新挂载时使用的 FATFS 对象
  * @retval FR_OK 表示格式化并挂载成功.
  * @note   为什么测试前要强制格式化:
  *         只要 Flash 里残留着上次运行的 test.txt, FA_CREATE_ALWAYS 就会去改写
  *         已有扇区, 从而走 FlashDisk_Write() 的中转搬移路径(耗时秒级),
  *         "直接编程"快路径永远测不到. 先擦成全 0xFF, 紧接着的写入才会命中快路径,
  *         耗时应从秒级降到毫秒级 —— 这个耗时差就是快路径生效的证据.
  * @note   擦除前必须先 f_mount(NULL,...) 卸载, 否则 FatFs 的窗口缓存与被擦空的
  *         Flash 不一致, 后续操作可能读到过期数据.
  */
static FRESULT FsTest_ForceFormat(FATFS *p_fs)
{
    FRESULT result;

    (void)f_mount(NULL, "", 1);

    if (FlashDisk_EraseAll() != FLASH_DISK_OK)
    {
        log_error("FlashDisk_EraseAll() fail\r\n");
        return FR_DISK_ERR;
    }

    result = f_mkfs("", FM_ANY, 0, s_mkfs_work, sizeof(s_mkfs_work));
    if (result != FR_OK)
    {
        log_error("f_mkfs() fail: %d\r\n", result);
        return result;
    }

    result = f_mount(p_fs, "", 1);
    if (result != FR_OK)
    {
        log_error("remount after mkfs fail: %d\r\n", result);
    }
    return result;
}
#endif /* _USE_MKFS */

/**
  * @brief  写入一个文件, 关闭后重新打开回读, 并与写入内容逐字节比对.
  * @param  p_path 文件路径
  * @param  p_data 待写入内容(以 NUL 结尾的字符串, 结尾 NUL 本身不写入)
  * @retval FR_OK 表示写入成功且回读内容完全一致.
  * @note   为什么写完必须先 f_close 再回读:
  *         FatFs 的 FAT 表与目录项是在 f_close() 时才刷盘的. 不关闭就回读,
  *         可能读到的是 FatFs 内部窗口缓存, 而不是真正落到 Flash 的数据,
  *         那样就验证不到 FlashDisk_Write() 这条路径.
  */
static FRESULT FsTest_WriteVerify(const char *p_path, const char *p_data)
{
    FIL      file;
    FRESULT  result;
    UINT     len      = (UINT)strlen(p_data);
    UINT     written  = 0U;
    UINT     read_len = 0U;
    uint32_t tick;
    char     read_buf[FS_TEST_BUF_SIZE];

    if (len >= FS_TEST_BUF_SIZE)
    {
        log_error("FsTest_WriteVerify: data too long, %u >= %u\r\n",
                  len, (UINT)FS_TEST_BUF_SIZE);
        return FR_INVALID_PARAMETER;
    }
    (void)memset(read_buf, 0, sizeof(read_buf));

    /* ------------------------------ 写入 ------------------------------ */
    /* FA_CREATE_ALWAYS: 每次都重建文件. 目标扇区已有数据时会走
       FlashDisk_Write() 的 read-modify-write 中转搬移路径 */
    log_debug("f_open(WRITE) start\r\n");
    tick   = HAL_GetTick();
    result = f_open(&file, p_path, FA_CREATE_ALWAYS | FA_WRITE);
    log_debug("f_open(WRITE) end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));
    if (result != FR_OK)
    {
        log_error("f_open(WRITE) fail: %d\r\n", result);
        return result;
    }

    log_debug("f_write() start, %u bytes\r\n", len);
    tick   = HAL_GetTick();
    result = f_write(&file, p_data, len, &written);
    log_debug("f_write() end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));
    if (result != FR_OK)
    {
        log_error("f_write() fail: %d\r\n", result);
        (void)f_close(&file);
        return result;
    }
    if (written != len)
    {
        log_error("f_write() short write: %u/%u\r\n", written, len);
        (void)f_close(&file);
        return FR_DISK_ERR;
    }
    log_debug("f_write() succ, %u bytes\r\n", written);

    /* f_close 才把 FAT 表和目录项刷盘, 所以这一步常常比 f_write 本身还慢 */
    log_debug("f_close(WRITE) start\r\n");
    tick   = HAL_GetTick();
    result = f_close(&file);
    log_debug("f_close(WRITE) end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));
    if (result != FR_OK)
    {
        log_error("f_close(WRITE) fail: %d\r\n", result);
        return result;
    }

    /* ---------------------------- 回读比对 ---------------------------- */
    log_debug("f_open(READ) start\r\n");
    tick   = HAL_GetTick();
    result = f_open(&file, p_path, FA_READ);
    log_debug("f_open(READ) end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));
    if (result != FR_OK)
    {
        log_error("f_open(READ) fail: %d\r\n", result);
        return result;
    }

    log_debug("f_read() start, %u bytes\r\n", len);
    tick   = HAL_GetTick();
    result = f_read(&file, read_buf, len, &read_len);
    log_debug("f_read() end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));
    log_debug("read_buf: %s\r\n", read_buf);

    log_debug("f_close(READ) start\r\n");
    tick = HAL_GetTick();
    (void)f_close(&file);
    log_debug("f_close(READ) end, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick));

    if (result != FR_OK)
    {
        log_error("f_read() fail: %d\r\n", result);
        return result;
    }
    if (read_len != len)
    {
        log_error("f_read() short read: %u/%u\r\n", read_len, len);
        return FR_DISK_ERR;
    }

    if (memcmp(read_buf, p_data, len) != 0)
    {
        log_error("VERIFY FAIL, %u bytes mismatch\r\n", len);
        log_error("  write: %s\r\n", p_data);
        log_error("  read : %s\r\n", read_buf);
        return FR_DISK_ERR;
    }

    log_debug("VERIFY OK, %u bytes match: %s\r\n", read_len, read_buf);
    return FR_OK;
}
#endif /* !_FS_READONLY */

void fs_test(void)
{
    FATFS fs;

    log_debug("fs_test() start\r\n");
    FRESULT result = f_mount(&fs, "", 1);  // 第三个参数1表示立即挂载

#if (!_FS_READONLY) && (_USE_MKFS)
    /* 片内 Flash 擦除后是全 0xFF, 第 0 扇区没有 FAT 引导扇区(BPB), f_mount 会
       返回 FR_NO_FILESYSTEM. 首次使用必须先把文件系统结构写进去.
       先整区擦除再 f_mkfs: 空白区域上写 FAT 全程走直接编程快路径, 不触发中转搬移.
       只读模式下走不到这里, 镜像必须在 PC 上做好、烧写时下载进 Flash. */
    if (result == FR_NO_FILESYSTEM) {
        log_debug("no filesystem, formatting...\r\n");
        result = FsTest_ForceFormat(&fs);
        if (result != FR_OK) {
            return;
        }
        log_debug("format succ\r\n");
    }
#endif /* (!_FS_READONLY) && (_USE_MKFS) */

    if (result != FR_OK) {
        // 挂载失败，处理错误
        log_error("fatfs mount fail , err code: %d\n", result);
        // 可以根据res的值进行具体错误处理
        return;
    } else {
        log_debug("fat mount succ\n");
    }

	//SD卡信息结构体变量
#if SDIO_ENABLE
    /* 以下仅为 SD 卡信息打印, 与 FATFS 无关: 关闭 SDIO 时整段跳过 */
	HAL_SD_CardInfoTypeDef cardInfo;
	HAL_StatusTypeDef res = HAL_SD_GetCardInfo(&hsd, &cardInfo);

	if(res!=HAL_OK) {
		log_debug("HAL_SD_GetCardInfo() error\r\n");
		return ;
	}

	log_debug("\r\n*** HAL_SD_GetCardInfo() info ***\r\n");
	log_debug("Card Type= %d\r\n", cardInfo.CardType);
	log_debug("Card Version= %d\r\n", cardInfo.CardVersion);
	log_debug("Card Class= %d\r\n", cardInfo.Class);
	log_debug("Relative Card Address= %d\r\n", cardInfo.RelCardAdd);
	log_debug("Block Count= %d\r\n", cardInfo.BlockNbr);
	log_debug("Block Size(Bytes)= %d\r\n", cardInfo.BlockSize);
	log_debug("LogiBlockCount= %d\r\n", cardInfo.LogBlockNbr);
	log_debug("LogiBlockSize(Bytes)= %d\r\n", cardInfo.LogBlockSize);
    log_debug("SD Card Capacity(MB)= %d\r\n", cardInfo.BlockNbr>>1>>10);
#endif /* SDIO_ENABLE */

#if _FS_READONLY
    /* 只读模式: 磁盘镜像由烧写时预置, 内容未知, 只验证能否打开并读出.
       f_write / f_mkfs 在 _FS_READONLY=1 下不存在, 不能引用 */
    {
        FIL  file;
        char read_buf[FS_TEST_BUF_SIZE];
        UINT read_len = 0U;

        (void)memset(read_buf, 0, sizeof(read_buf));
        result = f_open(&file, FS_TEST_FILE_PATH, FA_READ);
        if (result != FR_OK) {
            log_error("f_open(FA_READ) error: %d\r\n", result);
            f_mount(NULL, "", 1);
            return;
        }
        result = f_read(&file, read_buf, sizeof(read_buf) - 1U, &read_len);
        (void)f_close(&file);
        if (result != FR_OK) {
            log_error("f_read() error: %d\r\n", result);
        } else {
            log_debug("f_read() succ, %u bytes: %s\r\n", read_len, read_buf);
        }
    }
#else
    /* 读写模式: "写入 -> 关闭 -> 回读 -> 逐字节比对", 分两轮打中两条不同的写路径.
       每轮打印耗时, 用耗时量级判断实际走的是哪条路径:
         round 1 目标是刚擦净的空白区 -> 直接编程快路径 -> 毫秒级
         round 2 改写同一文件已有扇区 -> 中转扇区搬移   -> 秒级 */
    {
        uint32_t tick_start;

#if _USE_MKFS
        /* round 0 必不可少: 不先擦净的话 Flash 里残留上次的 test.txt,
           round 1 也会走搬移路径, 快路径就测不到了 */
        log_debug("--- round 0: force format (so round 1 hits fast path) ---\r\n");
        tick_start = HAL_GetTick();
        if (FsTest_ForceFormat(&fs) != FR_OK) {
            f_mount(NULL, "", 1);
            return;
        }
        log_debug("round 0 done, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick_start));
#endif /* _USE_MKFS */

        log_debug("--- round 1: write to blank area (expect fast path, ms) ---\r\n");
        tick_start = HAL_GetTick();
        if (FsTest_WriteVerify(FS_TEST_FILE_PATH, FS_TEST_DATA_ROUND1) != FR_OK) {
            f_mount(NULL, "", 1);
            return;
        }
        log_debug("round 1 done, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick_start));

        log_debug("--- round 2: rewrite (expect swap sector, seconds) ---\r\n");
        tick_start = HAL_GetTick();
        if (FsTest_WriteVerify(FS_TEST_FILE_PATH, FS_TEST_DATA_ROUND2) != FR_OK) {
            f_mount(NULL, "", 1);
            return;
        }
        log_debug("round 2 done, %u ms\r\n", (unsigned int)(HAL_GetTick() - tick_start));
    }

    log_debug("fs_test() all rounds verified OK\r\n");
#endif /* _FS_READONLY */

    /*卸载文件系统*/
    f_mount(NULL, "", 1);

}
