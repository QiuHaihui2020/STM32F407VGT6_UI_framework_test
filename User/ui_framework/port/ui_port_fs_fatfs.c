/**
 * @file    ui_port_fs_fatfs.c
 * @brief   UI 框架文件系统实现 —— FATFS
 *
 * 实现 compat/jl_fs.h 声明的全部接口。框架读资源和字库只经过这一层,
 * 没有第二层封装。
 *
 * 换文件系统时: 复制本文件改名, 重新实现同一组函数即可, 框架源码与
 * 其余 port/ 文件都不用动。
 *
 * @note 句柄用【静态池】而不是 malloc: 嵌入式下开文件的数量是可数的,
 *       静态池能在启动后杜绝堆碎片, 也让"句柄泄漏"在调试期就暴露
 *       (池满会打印告警), 而不是悄悄吃掉堆。
 */
#include "jl_fs.h"
/* 打开本文件的分级日志。jl_debug.h 的 log_* 是靠这几个宏开关的,
 * 不定义就是空实现 —— port 层是上板排查的关键路径, 必须留着。 */
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE
#include "jl_debug.h"
#include "jl_os_api.h"
#include "ui_port_config.h"

#include "ff.h"
#include "fatfs.h"

#include <stdio.h>      /* printf / snprintf: 目录树诊断用 */
#include <string.h>     /* strlen */

/* ==================================================================== *
 *  句柄池
 * ==================================================================== */

/**
 * 同时打开的文件数上限。
 *
 * 框架的并发开文件情况(实测最少 6 个常驻):
 *     .res 图片资源      res_file1  (res/resfile.c)
 *     .str 字符串图片    str_file1  (res/resfile.c)
 *     .sty 窗口布局      ui_file1   (platform/ui_resources_manager.c)
 *     ASCII 字库         file       (res/ascii.c)
 *     中文字库 .PIX/.TAB font/font_gbk.c 各持一个
 * 再留点余量给资源管理器的临时探测, 取 8。
 *
 * ⚠ 必须与 FATFS/Target/ffconf.h 的 _FS_LOCK 一致。两处不一致时:
 *     _FS_LOCK 小  -> 本层还以为有空位, f_open 却回 FR_TOO_MANY_OPEN_FILES(18);
 *     _FS_LOCK 大  -> 本层先报"句柄池已满", FatFs 那边其实还有余量。
 *   改一处记得改另一处。
 *
 * 每个 FIL 在 _FS_TINY=0 且 _MAX_SS=512 时约 550 字节, 8 个约 4.4KB。
 */
#define UI_FS_MAX_OPEN      8

struct ui_fs_file {
    FIL  fil;
    u8   is_used;
    char name[16];      /* 只存文件名(不含路径), 供 resfile_get_name 用 */
};

static struct ui_fs_file s_files[UI_FS_MAX_OPEN];
static u8 s_is_mounted = 0;

/** 从池里取一个空闲槽 */
static struct ui_fs_file *Ui_Fs_Alloc(void)
{
    u32 i;

    /* 池的分配/释放可能被 UI 任务和资源管理定时器同时碰到, 用临界区保护。
     * 临界区里只做标志位翻转, 不做文件 IO, 所以极短 */
    local_irq_disable();
    for (i = 0; i < UI_FS_MAX_OPEN; i++) {
        if (!s_files[i].is_used) {
            s_files[i].is_used = 1;
            local_irq_enable();
            return &s_files[i];
        }
    }
    local_irq_enable();

    log_error("ui_fs: 句柄池已满(%d), 检查是否有文件没关\r\n", UI_FS_MAX_OPEN);
    return NULL;
}

/** 校验句柄是否确实来自本池且在用 —— 防止框架传进野指针 */
static struct ui_fs_file *Ui_Fs_Check(RESFILE *fp)
{
    struct ui_fs_file *f = (struct ui_fs_file *)fp;
    u32 i;

    if (f == NULL) {
        return NULL;
    }
    for (i = 0; i < UI_FS_MAX_OPEN; i++) {
        if ((&s_files[i] == f) && s_files[i].is_used) {
            return f;
        }
    }
    return NULL;
}


/* ==================================================================== *
 *  挂载
 * ==================================================================== */

/* ==================================================================== *
 *  目录树诊断
 *
 *  资源路径对不对、文件到底在不在盘上, 靠猜最费时间。挂载成功后直接把
 *  盘上实际内容打出来, 和 ui_port_config.h 里的 RES_PATH 一对照就清楚了。
 *
 *  不需要时把 UI_PORT_FS_DUMP_TREE 置 0, 整段代码会被编译掉。
 * ==================================================================== */
#ifndef UI_PORT_FS_DUMP_TREE
#define UI_PORT_FS_DUMP_TREE    1
#endif

#if UI_PORT_FS_DUMP_TREE

/** 递归深度上限。资源目录只有 0:/JL 和 0:/font 两层, 给 3 层够用;
 * 限深也顺便防住目录环(损坏的 FAT 上可能出现) */
#define UI_FS_DUMP_MAX_DEPTH    3

static void Ui_Fs_DumpDir(const char *path, int depth)
{
    DIR dir;
    FILINFO fno;
    FRESULT res;
    char sub[128];
    int i;

    if (depth > UI_FS_DUMP_MAX_DEPTH) {
        return;
    }

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        log_error("ui_fs:   <打不开目录 %s, FRESULT=%d>\r\n", path, res);
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
            Ui_Fs_DumpDir(sub, depth + 1);
        } else {
            printf("  %-14s %lu\r\n", fno.fname, (unsigned long)fno.fsize);
        }
    }

    (void)f_closedir(&dir);
}

/**
 * @brief 打印 FAT 盘上的实际目录树
 * @note 只用于排查"资源找不到"。对照 ui_port_config.h 的 UI_PORT_RES_ROOT /
 *       UI_PORT_FONT_ROOT 看路径是否一致。
 *       ⚠ _USE_LFN = 0, 所以这里显示的是 8.3 短文件名(全大写)。
 */
void ui_fs_dump_tree(void)
{
    log_info("ui_fs: ---- FAT 盘实际内容 (root=%s) ----\r\n", USERPath);
    Ui_Fs_DumpDir(USERPath, 0);
    log_info("ui_fs: ---- 目录树结束 ----\r\n");
}

#else
void ui_fs_dump_tree(void) {}
#endif /* UI_PORT_FS_DUMP_TREE */


int ui_fs_mount(void)
{
    FRESULT res;

    if (s_is_mounted) {
        return 0;
    }

    /* USERPath 由 MX_FATFS_Init() 里的 FATFS_LinkDriver 填好,
     * 内容形如 "0:/"。这里不自己写死盘号, 免得 CubeMX 改配置后失配。 */
    res = f_mount(&USERFatFS, USERPath, 1);   /* 1 = 立即挂载并检查 */
    if (res != FR_OK) {
        log_error("ui_fs: f_mount(\"%s\") 失败, FRESULT=%d\r\n", USERPath, res);
        return -1;
    }

    s_is_mounted = 1;
    log_info("ui_fs: 已挂载 %s\r\n", USERPath);

    /* 把盘上实际内容打出来 —— "资源找不到" 是上板阶段最常见的问题,
     * 有这份清单就不用靠猜路径。不需要时置 UI_PORT_FS_DUMP_TREE 为 0。 */
    ui_fs_dump_tree();

    return 0;
}

int mount(const char *dev, const char *dir_name, const char *fs_type,
          u32 dev_num, void *arg)
{
    /* 原厂的 mount 是杰理 VFS 的多设备挂载, 参数在本移植里都没有意义:
     * 只有一个 FATFS 卷。保留这个函数是因为框架 lcd_ui_api.c 调了它。 */
    (void)dev;
    (void)dir_name;
    (void)fs_type;
    (void)dev_num;
    (void)arg;

    return ui_fs_mount();
}


/* ==================================================================== *
 *  文件操作
 * ==================================================================== */

RESFILE *resfile_open(const char *path)
{
    struct ui_fs_file *f;
    FRESULT res;
    const char *base;
    u32 i;

    if (path == NULL) {
        return NULL;
    }

    /* 没挂上就先挂 —— 框架有些探测流程会在 lcd_ui_init 之前就来开文件 */
    if (!s_is_mounted) {
        if (ui_fs_mount() != 0) {
            return NULL;
        }
    }

    f = Ui_Fs_Alloc();
    if (f == NULL) {
        return NULL;
    }

    res = f_open(&f->fil, path, FA_READ);
    if (res != FR_OK) {
        /* 打不开是【正常情况】: 框架会逐个试探 UI_STY_CHECK_PATH 等多条
         * 路径, 找到第一个能开的为止。所以这里用 debug 级别, 不是 error */
        log_debug("ui_fs: 打不开 %s (FRESULT=%d)\r\n", path, res);
        f->is_used = 0;
        return NULL;
    }

    /* 记下文件名(路径最后一段), 供 resfile_get_name */
    base = path;
    for (i = 0; path[i] != 0; i++) {
        if ((path[i] == '/') || (path[i] == 0x5C)) {   /* 0x5C = 反斜杠 */
            base = &path[i + 1];
        }
    }
    for (i = 0; (i < (sizeof(f->name) - 1U)) && (base[i] != 0); i++) {
        f->name[i] = base[i];
    }
    f->name[i] = 0;

    return (RESFILE *)f;
}

int resfile_read(RESFILE *fp, void *buf, u32 len)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);
    UINT br = 0;

    if ((f == NULL) || (buf == NULL)) {
        return RESFILE_ERR_HANDLE;
    }
    if (len == 0) {
        return 0;
    }

    if (f_read(&f->fil, buf, (UINT)len, &br) != FR_OK) {
        return RESFILE_ERR_HANDLE;
    }

    /* 返回实际读到的字节数。读到文件尾时 br < len 是合法的,
     * 框架的调用点都会拿返回值和期望长度比对 */
    return (int)br;
}

int resfile_write(RESFILE *fp, void *buf, u32 len)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);
    UINT bw = 0;

    if ((f == NULL) || (buf == NULL)) {
        return RESFILE_ERR_HANDLE;
    }

    /* 资源文件是以 FA_READ 打开的, 所以这里必然失败 —— 这是有意的:
     * 唯一的写调用方是歌词索引回写, 而本移植的资源盘可能是只读的片内
     * Flash 镜像。如实返回错误, 框架会放弃缓存索引而不是写坏资源。 */
    if (f_write(&f->fil, buf, (UINT)len, &bw) != FR_OK) {
        return RESFILE_ERR_OPS_NO_SUPPORT;
    }
    return (int)bw;
}

int resfile_seek(RESFILE *fp, u32 offset, u32 fromwhere)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);
    FSIZE_t target;
    FSIZE_t size;

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }

    size = f_size(&f->fil);

    switch (fromwhere) {
    case RESFILE_SEEK_SET:
        target = (FSIZE_t)offset;
        break;
    case RESFILE_SEEK_CUR:
        /* offset 形参是 u32, 但框架有按有符号语义传负偏移的地方,
         * 所以按 s32 解释再累加 —— 直接当无符号加会跳到天上去 */
        target = f_tell(&f->fil) + (FSIZE_t)(s32)offset;
        break;
    case RESFILE_SEEK_END:
        target = size + (FSIZE_t)(s32)offset;
        break;
    default:
        return RESFILE_ERR_OPS_NO_SUPPORT;
    }

    if (target > size) {
        /* 越界定位当错误报上去, 而不是让 f_lseek 把文件撑大 */
        return RESFILE_ERR_HANDLE;
    }

    if (f_lseek(&f->fil, target) != FR_OK) {
        return RESFILE_ERR_HANDLE;
    }

    return (int)f_tell(&f->fil);
}

int resfile_get_len(RESFILE *fp)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }
    return (int)f_size(&f->fil);
}

int resfile_get_pos(RESFILE *fp)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }
    return (int)f_tell(&f->fil);
}

int resfile_get_attrs(RESFILE *fp, struct resfile_attrs *attrs)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);

    if ((f == NULL) || (attrs == NULL)) {
        return RESFILE_ERR_HANDLE;
    }

    attrs->attr  = 0;                       /* 只读资源, 无特殊属性位 */
    attrs->fsize = (u32)f_size(&f->fil);
    /* sclust 是杰理 VFS 的起始簇号, 框架只在 flash 直存路径用它。
     * FATFS 不对外暴露簇号, 且本移植不做 flash 直存, 填 0。 */
    attrs->sclust = 0;

    return RESFILE_ERR_NONE;
}

int resfile_get_name(RESFILE *fp, void *name, u32 len)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);
    char *out = (char *)name;
    u32 i;

    if ((f == NULL) || (out == NULL) || (len == 0)) {
        return RESFILE_ERR_HANDLE;
    }

    for (i = 0; (i < (len - 1U)) && (f->name[i] != 0); i++) {
        out[i] = f->name[i];
    }
    out[i] = 0;

    return RESFILE_ERR_NONE;
}

int resfile_close(RESFILE *fp)
{
    struct ui_fs_file *f = Ui_Fs_Check(fp);

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }

    (void)f_close(&f->fil);
    f->name[0] = 0;
    f->is_used = 0;     /* 最后一步才释放槽位, 避免被别处抢先复用 */

    return RESFILE_ERR_NONE;
}


/* ==================================================================== *
 *  表盘背景专用(点阵屏下无调用点)
 * ==================================================================== */

int fread_fast(void *fp, void *buf, u32 len)
{
    /* watch_bgp.c 的表盘背景功能才用, 且它拿的是 fopen 返回的句柄,
     * 与本文件的句柄池不是一套。点阵屏下 UI_WATCH_RES_ENABLE=0,
     * 调用点被整块编译掉。真要启用表盘功能需要另接一套读写。 */
    (void)fp;
    (void)buf;
    (void)len;
    return RESFILE_ERR_OPS_NO_SUPPORT;
}

int fseek_fast(void *fp, u32 offset, int fromwhere)
{
    (void)fp;
    (void)offset;
    (void)fromwhere;
    return RESFILE_ERR_OPS_NO_SUPPORT;
}
