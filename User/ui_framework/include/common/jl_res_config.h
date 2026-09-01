/**
 * @file    jl_res_config.h
 * @brief   资源文件路径配置
 *
 * 路径根从 config/ui_port_config.h 的 UI_PORT_RES_ROOT 派生, 改存放位置
 * 只需要改那一处。
 */
#ifndef __JL_RES_CONFIG_H__
#define __JL_RES_CONFIG_H__

#include "ui_port_config.h"

/** 资源根目录。FATFS 路径, 末尾必须带 '/' —— 框架靠字符串拼接组路径 */
#define RES_PATH            UI_PORT_RES_ROOT "/"
#define FONT_PATH           UI_PORT_FONT_ROOT "/"

/* 原厂里内置 flash 与外置 flash 分两个根, 本移植统一走 FATFS 一个根 */
#define FLASH_ROOT          UI_PORT_RES_ROOT
#define FLASH_RES_PATH      RES_PATH
#define FLASH_APP_PATH      RES_PATH
#define EXTERN_PATH         RES_PATH
#define INTERN_PATH         RES_PATH
#define UPGRADE_PATH        RES_PATH "upgrade/"

/* 框架启动时逐个试探这些路径, 找到第一个能打开的就用。
 * 点阵屏只有一套 JL 资源(表盘 watch1~5 是彩屏手表专用) */
#define UI_STY_CHECK_PATH   RES_PATH "JL/JL.sty",
#define UI_RES_CHECK_PATH   RES_PATH "JL/JL.res",
#define UI_STR_CHECK_PATH   RES_PATH "JL/JL.str",
#define UI_STY_WATCH_PATH

/** 表盘功能: 多套 watch 表盘 + 表盘升级, 只适用于彩屏手表。
 * 点阵屏开启会去加载不存在的 watch 资源并访问已废弃接口 */
#define UI_WATCH_RES_ENABLE     0
#define UI_UPGRADE_RES_ENABLE   1

/* UI_USED_DOUBLE_BUFFER 有意【不】在这里定义 —— 它是移植配置项,
 * 统一放在 ui_port_config.h, 避免两处定义打架 */

#endif /* __JL_RES_CONFIG_H__ */
