/**
 * @file    ui_res_backend.h
 * @brief   资源读取的【后端接口】—— 介质相关的最小面
 *
 * 存储层分两半:
 *
 *   ui_res_core.c   介质无关。句柄池、句柄校验、参数检查、seek 的 whence
 *                   换算与越界判断、挂载幂等、懒挂载 —— 换介质时【完全复用】。
 *                   它实现 include/common/jl_fs.h 那 7 个 resfile_* 接口。
 *
 *   ui_res_<介质>.c 介质相关。只需实现本文件这 8 个函数, 每个都是几行。
 *
 * 换文件系统 = 写一个新的 ui_res_<介质>.c + 在下面的 CTX_SIZE 表里加一行,
 * 别的什么都不动。原先这些逻辑和 FatFs 调用混在一个 371 行的文件里,
 * 换介质得把池管理连带重写一遍。
 *
 * @note 【为什么不用函数指针表】后端在编译期就定了(一个工程一种介质),
 *       直接函数调用可让编译器内联, 也避开了强 LTO 下函数指针同一性的坑
 *       (见 CLAUDE.md 里那条规矩)。选后端靠 config/ui_port_config.h 的
 *       UI_RES_BACKEND_* 宏 + Keil 工程里只编译对应的那个 .c。
 */
#ifndef __UI_RES_BACKEND_H__
#define __UI_RES_BACKEND_H__

#include <stdint.h>
#include "ui_port_config.h"     /* UI_RES_BACKEND_* 选哪个后端 */

/* ==================================================================== *
 *  一、每个文件句柄需要的后端上下文大小
 *
 *  core 按这个尺寸【静态分配】每个池槽的后端上下文, 于是 core 不必
 *  include 后端的头(ff.h / lfs.h ...), 保持介质无关。
 *
 *  后端 .c 里有编译期断言校验这个值够大 —— 写小了会在编译期报错,
 *  不会变成运行期越界。
 * ==================================================================== */

#if defined(UI_RES_BACKEND_FATFS) && (UI_RES_BACKEND_FATFS)
/* FatFs 的 FIL: _FS_TINY=0 且 _MAX_SS=512 时约 550 字节。取 576 留余量。
 * 打开 _USE_LFN 或加大 _MAX_SS 会变大, 后端里的断言会提醒你改这里。 */
#define UI_RES_BE_CTX_SIZE      576
#define UI_RES_BE_NAME          "fatfs"

#elif defined(UI_RES_BACKEND_RAWFLASH) && (UI_RES_BACKEND_RAWFLASH)
/* 裸 flash + 打包索引: 只需记 {起始偏移, 长度, 当前读指针} */
#define UI_RES_BE_CTX_SIZE      16
#define UI_RES_BE_NAME          "rawflash"

#else
#error "没有选择资源后端。在 config/ui_port_config.h 里定义 UI_RES_BACKEND_FATFS 之类的宏。"
#endif


/* ==================================================================== *
 *  二、后端必须实现的 8 个函数
 *
 *  ctx 是 core 给的一块 UI_RES_BE_CTX_SIZE 字节的内存, 4 字节对齐,
 *  后端把它强转成自己的类型(FIL / 自定义结构)使用。core 不看它的内容。
 *
 *  返回值约定统一为: >=0 成功(含具体数值), <0 失败。
 *  core 负责把负值翻译成 jl_fs.h 的 RESFILE_ERR_*, 后端不用管那套错误码。
 * ==================================================================== */

/**
 * @brief 挂载存储介质
 * @return 0 成功, 负值失败
 * @note core 保证只在未挂载时调用, 后端不必自己做幂等。
 */
int32_t ui_res_be_mount(void);

/**
 * @brief 打开文件(只读)
 * @param ctx 后端上下文, core 已清零
 * @param path 完整路径
 * @return 0 成功, 负值失败
 * @note 【打不开是正常情况】: 框架会逐条试探 UI_STY_CHECK_PATH 等多个路径,
 *       找到第一个能开的为止。所以后端在这里不要打 error 级日志。
 */
int32_t ui_res_be_open(void *ctx, const char *path);

/**
 * @brief 读取
 * @return >=0 实际读到的字节数(可以小于 len, 表示到了文件尾), 负值失败
 */
int32_t ui_res_be_read(void *ctx, void *buf, uint32_t len);

/**
 * @brief 绝对定位
 * @param pos 从文件头算起的字节偏移
 * @return 0 成功, 负值失败
 * @note whence(SET/CUR/END)的换算和越界检查都在 core 里做完了,
 *       后端只需要处理绝对位置 —— 这部分逻辑各介质完全一样, 不该重复实现。
 */
int32_t ui_res_be_seek(void *ctx, uint32_t pos);

/** @brief 取当前读指针 @return >=0 位置, 负值失败 */
int32_t ui_res_be_tell(void *ctx);

/** @brief 取文件总长 @return >=0 字节数, 负值失败 */
int32_t ui_res_be_size(void *ctx);

/** @brief 关闭 @return 0 成功, 负值失败 */
int32_t ui_res_be_close(void *ctx);

/**
 * @brief 打印介质上的实际目录树(上板排查用)
 *
 * "资源找不到 / 界面全黑" 是上板阶段最常见的问题, 原因基本都是路径对不上。
 * 把介质上实际有什么打出来, 和 config/ui_port_config.h 里的
 * UI_PORT_RES_ROOT / UI_PORT_FONT_ROOT 一对照就清楚了。
 *
 * @note 没有目录概念的后端(如裸 flash 打包索引)可以打印索引表, 或留空实现。
 *       core 在挂载成功后会自动调一次(受 UI_PORT_FS_DUMP_TREE 控制)。
 */
void ui_res_be_dump_tree(void);

#endif /* __UI_RES_BACKEND_H__ */
