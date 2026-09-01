/**
 * @file    ui_res_fatfs.c
 * @brief   资源读取后端 —— FatFs
 *
 * 只实现 ui_res_backend.h 那 8 个函数。句柄池、句柄校验、参数检查、
 * seek 的 whence 换算、挂载幂等全在 ui_res_core.c 里, 与本文件无关。
 *
 * 换文件系统: 照本文件的样子写一个 ui_res_<介质>.c, 在 ui_res_backend.h
 * 的 CTX_SIZE 表里加一行, Keil 工程里把本文件换成新的那个。
 */
#include "ui_res_backend.h"

#if defined(UI_RES_BACKEND_FATFS) && (UI_RES_BACKEND_FATFS)

#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE
#include "jl_debug.h"

#include "ff.h"
#include "fatfs.h"

#include <stdio.h>      /* printf / snprintf: 目录树诊断用 */
#include <string.h>     /* strlen */

/* ---- FatFs 卷绑定 ------------------------------------------------
 * 默认用 CubeMX 生成的 FATFS/App/fatfs.c 里的两个全局对象:
 *   USERFatFS  卷工作区
 *   USERPath   盘符字符串, MX_FATFS_Init() 里由 FATFS_LinkDriver 填好,
 *              形如 "0:/" —— 不自己写死盘号, 免得 CubeMX 改配置后失配
 *
 * 抽成宏是为了把对【CubeMX 生成代码的依赖】收在一处: 改用裸 FatFs
 * (不走 CubeMX)、或换到另一个卷时, 只改这两行。 */
#ifndef UI_RES_FATFS_OBJ
#define UI_RES_FATFS_OBJ    USERFatFS
#endif
#ifndef UI_RES_FATFS_PATH
#define UI_RES_FATFS_PATH   USERPath
#endif

/**
 * ctx 就是一个 FIL。
 *
 * 编译期校验 core 静态分配的 UI_RES_BE_CTX_SIZE 够装下 FIL ——
 * 写小了在这里就报错, 不会变成运行期越界。改了 ffconf.h 的 _USE_LFN /
 * _MAX_SS 之后如果这行报错, 去 ui_res_backend.h 把 CTX_SIZE 调大。
 */
typedef char ui_res_fatfs_ctx_size_check[(UI_RES_BE_CTX_SIZE >= sizeof(FIL)) ? 1 : -1];

#define __FIL(ctx)      ((FIL *)(ctx))


/* ==================================================================== *
 *  挂载
 * ==================================================================== */

int32_t ui_res_be_mount(void)
{
    FRESULT res;

    /* core 保证只在未挂载时调用, 这里不用自己做幂等 */
    res = f_mount(&UI_RES_FATFS_OBJ, UI_RES_FATFS_PATH, 1);  /* 1 = 立即挂载并检查 */
    if (res != FR_OK) {
        log_error("ui_res_fatfs: f_mount(\"%s\") 失败, FRESULT=%d\r\n",
                  UI_RES_FATFS_PATH, res);
        return -1;
    }
    return 0;
}


/* ==================================================================== *
 *  文件操作
 * ==================================================================== */

int32_t ui_res_be_open(void *ctx, const char *path)
{
    /* 打不开是正常情况(框架在逐条试探路径), 所以这里不打日志 ——
     * core 会用 debug 级别记一条 */
    return (f_open(__FIL(ctx), path, FA_READ) == FR_OK) ? 0 : -1;
}

int32_t ui_res_be_read(void *ctx, void *buf, uint32_t len)
{
    UINT br = 0;

    if (f_read(__FIL(ctx), buf, (UINT)len, &br) != FR_OK) {
        return -1;
    }
    /* br < len 是合法的(读到文件尾), 由 core 原样传给框架 */
    return (int32_t)br;
}

int32_t ui_res_be_seek(void *ctx, uint32_t pos)
{
    /* whence 换算和越界检查已在 core 里做完, 这里只管绝对定位 */
    return (f_lseek(__FIL(ctx), (FSIZE_t)pos) == FR_OK) ? 0 : -1;
}

int32_t ui_res_be_tell(void *ctx)
{
    return (int32_t)f_tell(__FIL(ctx));
}

int32_t ui_res_be_size(void *ctx)
{
    return (int32_t)f_size(__FIL(ctx));
}

int32_t ui_res_be_close(void *ctx)
{
    return (f_close(__FIL(ctx)) == FR_OK) ? 0 : -1;
}


/* ==================================================================== *
 *  目录树诊断
 *
 *  资源路径对不对、文件到底在不在盘上, 靠猜最费时间。挂载成功后直接把
 *  盘上实际内容打出来, 和 config/ui_port_config.h 里的 RES_PATH 一对照
 *  就清楚了。core 里受 UI_PORT_FS_DUMP_TREE 控制, 置 0 时不会调进来。
 * ==================================================================== */

/** 递归深度上限。资源目录只有 0:/JL 和 0:/font 两层, 给 3 层够用;
 * 限深也顺便防住目录环(损坏的 FAT 上可能出现) */
#define UI_RES_DUMP_MAX_DEPTH   3

static void Ui_Res_DumpDir(const char *path, int depth)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char sub[128];
    int i;

    if (depth > UI_RES_DUMP_MAX_DEPTH) {
        return;
    }

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        log_error("ui_res_fatfs:   <打不开目录 %s, FRESULT=%d>\r\n", path, res);
        return;
    }

    for (;;) {
        res = f_readdir(&dir, &fno);
        /* f_readdir 读到结尾时返回 FR_OK 且 fname[0] == 0 */
        if ((res != FR_OK) || (fno.fname[0] == 0)) {
            break;
        }

        for (i = 0; i < depth; i++) {
            printf("  ");
        }

        if (fno.fattrib & AM_DIR) {
            printf("  [%s]\r\n", fno.fname);
            /* 拼子目录路径。path 末尾可能已经带 '/'(盘根 "0:/"), 分开处理 */
            if (path[strlen(path) - 1] == '/') {
                (void)snprintf(sub, sizeof(sub), "%s%s", path, fno.fname);
            } else {
                (void)snprintf(sub, sizeof(sub), "%s/%s", path, fno.fname);
            }
            Ui_Res_DumpDir(sub, depth + 1);
        } else {
            printf("  %-14s %lu\r\n", fno.fname, (unsigned long)fno.fsize);
        }
    }

    (void)f_closedir(&dir);
}

/**
 * @brief 打印 FAT 盘上的实际目录树
 * @note ⚠ _USE_LFN = 0, 所以这里显示的是 8.3 短文件名(全大写)。
 */
void ui_res_be_dump_tree(void)
{
    log_info("ui_res_fatfs: ---- FAT 盘实际内容 (root=%s) ----\r\n", UI_RES_FATFS_PATH);
    Ui_Res_DumpDir(UI_RES_FATFS_PATH, 0);
    log_info("ui_res_fatfs: ---- 目录树结束 ----\r\n");
}

#endif /* UI_RES_BACKEND_FATFS */
