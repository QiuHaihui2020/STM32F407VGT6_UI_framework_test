/**
 * @file    ui_port_misc.c
 * @brief   UI 框架杂项 —— 需要由平台侧提供的散装符号
 *
 * @note CRC16 系列已移到 liba/common/jl_crc.c, ASCII_* 与 JBHash 已移到
 *       liba/common/jl_ascii.c —— 那两个是与平台无关的通用库代码,
 *       不属于平台适配胶水, 所以不放在 port/ 下。
 */
#include "jl_typedef.h"

//================================================//
//              UI版本和CPU类型  				  //
// 使用一个8bit值表示
// 	高四位表示UI类型，0表示点阵屏，1表示彩屏
// 	低四位表示CPU，0表示BR23和BR27，1表示BR28
//================================================//
const int JLUI_TYPE_AND_VERSION = 0x00;
