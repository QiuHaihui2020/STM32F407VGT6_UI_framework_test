#include "test.h"
#include "main.h"
#include "fatfs.h"
#include "sdio.h"
#include "log_debug.h"

void fs_test(void)
{
    // 假设SD卡挂载在逻辑驱动器0
    FATFS fs;
    //SD_Driver.disk_initialize(0);
    FRESULT result = f_mount(&fs, "", 1);  // 第三个参数1表示如果文件系统不存在则创建
    if (result != FR_OK) {
        // 挂载失败，处理错误
        r_printf("fatfs mount fail , err code: %d\n", result);
        // 可以根据res的值进行具体错误处理
    } else {
        r_printf("fat mount succ\n");
    }
    //SD_Driver.disk_initialize(0);
	//SD卡信息结构体变量
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

    FIL file;
    result = f_open(&file, "test.txt", FA_OPEN_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        log_debug("f_open() error\r\n");
        return;
    }
    log_debug("f_open() succ\r\n");
    result = f_write(&file, "Hello, world!", 13, NULL);
    if (result != FR_OK) {
        log_debug("f_write() error\r\n");
    }
    f_close(&file);
    log_debug("f_write() succ\r\n"); 
    /*卸载文件系统*/
    f_mount(NULL, "", 1);
    
}