/**
 * @file    jl_fs.h
 * @brief   UI 框架文件系统抽象接口 —— 唯一的存储边界
 *
 * 框架读 .res / .str / .sty 资源和 .PIX / .TAB 字库, 全部只经过本文件
 * 声明的这套接口。没有第二层封装。
 *
 * 存储层已拆成两半, 本文件声明的接口由前者实现:
 *   liba/res/ui_res_core.c      介质无关 —— 句柄池 / 句柄校验 / 参数检查 /
 *                               seek 的 whence 换算与越界判断 / 挂载幂等
 *   port/res/ui_res_fatfs.c     介质相关 —— 只实现 8 个后端函数
 *
 * 所以换文件系统【不是重写本接口】: 照 ui_res_fatfs.c 写一个新后端
 * (实代码 ~100 行), 在 port/res/ui_res_backend.h 的 CTX_SIZE 表里加一行,
 * 改 config/ui_port_config.h 的 UI_RES_BACKEND_* 宏。core 与框架都不动。
 *
 * 接口面按【实际调用】来定: 只声明框架真正用得到的这几个函数。写入、
 * 取当前位置、取文件名一类的能力先不放进来 —— 每多一个函数, 每个新后端
 * 都得白实现一遍(取文件名还要在句柄里存一份路径, 8 个句柄就是 128 字节
 * RAM)。真需要写入能力时再加, 到时只有一个调用方, 接口形状反而能按真实
 * 需求定。
 *
 * @note 函数名用 resfile_* / mount 这套通用只读文件 API 的叫法, 让 39 个
 *       .c 的调用点读起来一致, 也不带任何平台语义。
 */
#ifndef __JL_FS_H__
#define __JL_FS_H__

#include "jl_typedef.h"

/* ---- 定位方式 ------------------------------------------------------- */
#define RESFILE_SEEK_SET    0   /**< 从文件开头 */
#define RESFILE_SEEK_CUR    1   /**< 从当前读写指针 */
#define RESFILE_SEEK_END    2   /**< 从文件结尾 */

/* ---- 错误码 --------------------------------------------------------- */
enum resfile_err_code {
    RESFILE_ERR_NONE            =  0,
    RESFILE_ERR_PATH            = -1,   /**< 路径不存在 / 打不开 */
    RESFILE_ERR_NO_MEM          = -2,   /**< 句柄池或堆耗尽 */
    RESFILE_ERR_OPS_NO_SUPPORT  = -3,   /**< 本实现不支持该操作 */
    RESFILE_ERR_HANDLE          = -4,   /**< 句柄非法 */
};

/** 文件句柄。对框架是不透明类型, 内部结构由 port 层的实现自己定义 */
typedef struct __resfile RESFILE;

/** 文件属性 */
struct resfile_attrs {
    u8  attr;       /**< 文件属性位 */
    u32 fsize;      /**< 文件长度(字节) */
    u32 sclust;     /**< 起始簇号。无此概念的实现填 0 */
};


/* ==================================================================== *
 *  以下为 port 层必须实现的全部函数
 * ==================================================================== */

/**
 * @brief 挂载资源分区。UI 任务启动时调一次
 * @return 0 成功, 负值失败
 */
int ui_fs_mount(void);

/**
 * @brief 打印文件系统上的实际目录树(排查用)
 *
 * "资源找不到 / 界面全黑" 是上板阶段最常见的问题, 原因基本都是路径对不上。
 * 把盘上实际内容打出来, 和 config/ui_port_config.h 里的 UI_PORT_RES_ROOT /
 * UI_PORT_FONT_ROOT 一对照就清楚了。ui_fs_mount() 成功后会自动调一次。
 *
 * @note 关掉它: 在 ui_port_config.h 里 #define UI_PORT_FS_DUMP_TREE 0
 */
void ui_fs_dump_tree(void);

/**
 * @brief 打开资源文件(只读)
 * @param path 文件路径, 由 jl_res_config.h 里的 RES_PATH 宏拼出
 * @return 非 NULL 为文件句柄; NULL 表示打开失败
 */
RESFILE *resfile_open(const char *path);

/**
 * @brief 读取
 * @return >=0 为实际读到的字节数; <0 见 enum resfile_err_code
 * @note 返回值小于 len 是合法的(读到文件尾), 调用方会检查。
 */
int resfile_read(RESFILE *fp, void *buf, u32 len);

/**
 * @brief 移动读写指针
 * @param fromwhere RESFILE_SEEK_SET / _CUR / _END
 * @return >=0 为定位后的绝对位置; <0 见 enum resfile_err_code
 */
int resfile_seek(RESFILE *fp, u32 offset, u32 fromwhere);

/**
 * @brief 取文件长度
 * @return >=0 为字节数; <0 见 enum resfile_err_code
 */
int resfile_get_len(RESFILE *fp);

/**
 * @brief 取文件属性
 * @return 0 成功; <0 见 enum resfile_err_code
 */
int resfile_get_attrs(RESFILE *fp, struct resfile_attrs *attrs);

/**
 * @brief 关闭
 * @return 0 成功; <0 见 enum resfile_err_code
 */
int resfile_close(RESFILE *fp);

/* ==================================================================== *
 *  兼容用的零碎项
 * ==================================================================== */

/** 文件时间戳。框架里只作为 vfs_attr 的成员出现, 不参与逻辑 */
struct sys_time {
    u16 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  min;
    u8  sec;
};

/** ui_core.h 里按值内嵌为控件成员, 必须是完整类型 */
struct vfs_attr {
    u8  attr;
    u32 fsize;
    u32 sclust;
    struct sys_time crt_time;
    struct sys_time wrt_time;
    struct sys_time acc_time;
};


/* ---- 歌词索引回写用的 flash 直存 ------------------------------------
 * 【本工程不支持】—— 开关在 config/ui_port_config.h 的
 * UI_PORT_LYRICS_FLASH_SAVE_ENABLE, 默认 0, 存根实现在
 * liba/common/ui_port_stubs.c。
 *
 * 全工程只有 liba/ui_dot/lyrics.c 用它们, 而歌词模块目前零调用方,
 * 链接器会把 lyrics.o 连这两个存根整个丢弃(实测 42 个 section 全被移除),
 * 所以声明留在这里不占任何空间。
 *
 * ☠ 声明不能删: lyrics.c 经本文件拿到它们 —— 删了那边就是隐式声明错误。 */
u32 sdfile_cpu_addr2flash_addr(u32 addr);
u32 sdfile_flash_addr2cpu_addr(u32 addr);
/* sfc_erase / sfc_write 有意不在这里声明: liba/ui_dot/lyrics.c 自己 extern 了
 * 它们, 且签名与本文件其它接口风格不同(返回 u8/u32)。在两处声明会冲突,
 * 以调用方的声明为准, 实现见 liba/common/ui_port_stubs.c */

#endif /* __JL_FS_H__ */
