/**
 * @file    ui_res_core.c
 * @brief   资源读取层 —— 介质无关的那一半
 *
 * 实现 include/common/jl_fs.h 声明的 resfile_* 接口, 内部转调 ui_res_backend.h 的
 * ui_res_be_*。换文件系统时【本文件一行都不用改】。
 *
 * 本文件承担的全是与介质无关的活:
 *   - 句柄池(静态分配)与槽位回收
 *   - 句柄合法性校验(防框架传进野指针)
 *   - NULL / len==0 之类参数检查
 *   - seek 的 whence 换算与越界判断
 *   - 挂载幂等 + open 时的懒挂载
 *   - 把后端的 <0 翻译成 jl_fs.h 的 RESFILE_ERR_*
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
#include "ui_res_backend.h"

#include <string.h>     /* memset */

/* ==================================================================== *
 *  句柄池
 * ==================================================================== */

/**
 * 同时打开的文件数上限。
 *
 * 框架的并发开文件情况(实测最少 6 个常驻):
 *     .res 图片资源      res_file1  (liba/res/resfile.c)
 *     .str 字符串图片    str_file1  (liba/res/resfile.c)
 *     .sty 窗口布局      ui_file1   (lcd_drive/middle/ui_resources_manager.c)
 *     ASCII 字库         file       (liba/res/ascii.c)
 *     中文字库 .PIX/.TAB liba/font/font_gbk.c 各持一个
 * 再留点余量给资源管理器的临时探测, 取 8。
 *
 * ⚠ 必须与后端自身的并发上限一致。FatFs 后端对应 FATFS/Target/ffconf.h
 *   的 _FS_LOCK, 两处不一致时:
 *     _FS_LOCK 小  -> 本层还以为有空位, 后端 open 却回"打开文件过多";
 *     _FS_LOCK 大  -> 本层先报"句柄池已满", 后端其实还有余量。
 *   改一处记得改另一处。
 */
#define UI_RES_MAX_OPEN     8

/**
 * 池槽 = 使用标志 + 一块后端上下文。
 *
 * struct __resfile 就是 jl_fs.h 里 `typedef struct __resfile RESFILE` 的
 * 那个不完整类型, 定义放在这里 —— 于是框架拿到的 RESFILE* 是真类型指针,
 * 本文件内不需要任何强制转换, 而框架侧看不到内部结构。
 *
 * ctx 用 u32 数组而不是 u8 数组, 是为了拿到 4 字节对齐 —— 后端会把它
 * 强转成 FIL 之类含 u32/指针成员的结构。
 */
struct __resfile {
    u8  is_used;
    u32 ctx[(UI_RES_BE_CTX_SIZE + 3U) / 4U];
};

static struct __resfile s_files[UI_RES_MAX_OPEN];
static u8 s_is_mounted = 0;

/** 从池里取一个空闲槽, 并清零它的后端上下文 */
static struct __resfile *Ui_Res_Alloc(void)
{
    u32 i;

    /* 池的分配/释放可能被 UI 任务和资源管理定时器同时碰到, 用临界区保护。
     * 临界区里只做标志位翻转, 不做文件 IO, 所以极短 */
    local_irq_disable();
    for (i = 0; i < UI_RES_MAX_OPEN; i++) {
        if (!s_files[i].is_used) {
            s_files[i].is_used = 1;
            local_irq_enable();
            memset(s_files[i].ctx, 0, sizeof(s_files[i].ctx));
            return &s_files[i];
        }
    }
    local_irq_enable();

    log_error("ui_res: 句柄池已满(%d), 检查是否有文件没关\r\n", UI_RES_MAX_OPEN);
    return NULL;
}

/** 校验句柄是否确实来自本池且在用 —— 防止框架传进野指针 */
static struct __resfile *Ui_Res_Check(RESFILE *fp)
{
    u32 i;

    if (fp == NULL) {
        return NULL;
    }
    for (i = 0; i < UI_RES_MAX_OPEN; i++) {
        if ((&s_files[i] == fp) && s_files[i].is_used) {
            return fp;
        }
    }
    return NULL;
}


/* ==================================================================== *
 *  挂载
 * ==================================================================== */

int ui_fs_mount(void)
{
    if (s_is_mounted) {
        return 0;
    }

    if (ui_res_be_mount() < 0) {
        log_error("ui_res: 后端(%s)挂载失败\r\n", UI_RES_BE_NAME);
        return -1;
    }

    s_is_mounted = 1;
    log_info("ui_res: 已挂载, 后端 = %s\r\n", UI_RES_BE_NAME);

    /* 把介质上实际内容打出来 —— "资源找不到" 是上板阶段最常见的问题,
     * 有这份清单就不用靠猜路径。不需要时置 UI_PORT_FS_DUMP_TREE 为 0。 */
    ui_fs_dump_tree();

    return 0;
}

void ui_fs_dump_tree(void)
{
#if (defined(UI_PORT_FS_DUMP_TREE) && (UI_PORT_FS_DUMP_TREE))
    ui_res_be_dump_tree();
#endif
}


/* ==================================================================== *
 *  文件操作
 * ==================================================================== */

RESFILE *resfile_open(const char *path)
{
    struct __resfile *f;

    if (path == NULL) {
        return NULL;
    }

    /* 没挂上就先挂 —— 框架有些探测流程会在 lcd_ui_init 之前就来开文件 */
    if (!s_is_mounted) {
        if (ui_fs_mount() != 0) {
            return NULL;
        }
    }

    f = Ui_Res_Alloc();
    if (f == NULL) {
        return NULL;
    }

    if (ui_res_be_open(f->ctx, path) < 0) {
        /* 打不开是【正常情况】: 框架会逐个试探 UI_STY_CHECK_PATH 等多条
         * 路径, 找到第一个能开的为止。所以这里用 debug 级别, 不是 error */
        log_debug("ui_res: 打不开 %s\r\n", path);
        f->is_used = 0;
        return NULL;
    }

    return f;
}

int resfile_read(RESFILE *fp, void *buf, u32 len)
{
    struct __resfile *f = Ui_Res_Check(fp);
    int32_t ret;

    if ((f == NULL) || (buf == NULL)) {
        return RESFILE_ERR_HANDLE;
    }
    if (len == 0) {
        return 0;
    }

    ret = ui_res_be_read(f->ctx, buf, len);
    if (ret < 0) {
        return RESFILE_ERR_HANDLE;
    }

    /* 返回实际读到的字节数。读到文件尾时 ret < len 是合法的,
     * 框架的调用点都会拿返回值和期望长度比对 */
    return (int)ret;
}

int resfile_seek(RESFILE *fp, u32 offset, u32 fromwhere)
{
    struct __resfile *f = Ui_Res_Check(fp);
    int32_t size;
    int32_t cur;
    int32_t target;

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }

    size = ui_res_be_size(f->ctx);
    if (size < 0) {
        return RESFILE_ERR_HANDLE;
    }

    switch (fromwhere) {
    case RESFILE_SEEK_SET:
        target = (int32_t)offset;
        break;
    case RESFILE_SEEK_CUR:
        cur = ui_res_be_tell(f->ctx);
        if (cur < 0) {
            return RESFILE_ERR_HANDLE;
        }
        /* offset 形参是 u32, 但框架有按有符号语义传负偏移的地方,
         * 所以按 s32 解释再累加 —— 直接当无符号加会跳到天上去 */
        target = cur + (int32_t)offset;
        break;
    case RESFILE_SEEK_END:
        target = size + (int32_t)offset;
        break;
    default:
        return RESFILE_ERR_OPS_NO_SUPPORT;
    }

    /* 越界定位当错误报上去, 而不是让后端把文件撑大 */
    if ((target < 0) || (target > size)) {
        return RESFILE_ERR_HANDLE;
    }

    if (ui_res_be_seek(f->ctx, (u32)target) < 0) {
        return RESFILE_ERR_HANDLE;
    }

    return (int)target;
}

int resfile_get_len(RESFILE *fp)
{
    struct __resfile *f = Ui_Res_Check(fp);
    int32_t ret;

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }

    ret = ui_res_be_size(f->ctx);
    return (ret < 0) ? RESFILE_ERR_HANDLE : (int)ret;
}

int resfile_get_attrs(RESFILE *fp, struct resfile_attrs *attrs)
{
    struct __resfile *f = Ui_Res_Check(fp);
    int32_t size;

    if ((f == NULL) || (attrs == NULL)) {
        return RESFILE_ERR_HANDLE;
    }

    size = ui_res_be_size(f->ctx);
    if (size < 0) {
        return RESFILE_ERR_HANDLE;
    }

    attrs->attr  = 0;               /* 只读资源, 无特殊属性位 */
    attrs->fsize = (u32)size;
    /* sclust 是杰理 VFS 的起始簇号, 框架只在 flash 直存路径用它 ——
     * 那条路已由 UI_PORT_LYRICS_FLASH_SAVE_ENABLE 关闭。填 0 表示"拿不到",
     * 调用方(liba/ui_dot/lyrics.c:564)靠判空放弃。 */
    attrs->sclust = 0;

    return RESFILE_ERR_NONE;
}

int resfile_close(RESFILE *fp)
{
    struct __resfile *f = Ui_Res_Check(fp);

    if (f == NULL) {
        return RESFILE_ERR_HANDLE;
    }

    (void)ui_res_be_close(f->ctx);
    f->is_used = 0;     /* 最后一步才释放槽位, 避免被别处抢先复用 */

    return RESFILE_ERR_NONE;
}
