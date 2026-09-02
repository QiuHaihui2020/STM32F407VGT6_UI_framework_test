/**
 * @file    ui_style.h
 * @brief   应用侧 UI 风格 —— 把资源工具生成的 ID 映射成业务语义
 *
 * 对应 703 的 apps/soundbox/include/ui/ui_style.h。
 *
 * ┌─ 机制(与 703 完全一致) ────────────────────────────────────────────┐
 * │ UI 资源工具(ResBuilder)出图后, 由                                  │
 * │   tools/LCD_UI工程/ui_128_64_JL02/模式界面/project/copy_file.bat   │
 * │ 自动把生成物拷进工程:                                              │
 * │                                                                    │
 * │   project.bin -> tools/JL/JL.sty       窗口/控件布局               │
 * │   result.bin  -> tools/JL/JL.res       图片资源                    │
 * │   result.str  -> tools/JL/JL.str       字符串图片                  │
 * │   ename.h     -> include/common/style_jl02.h     ★ 全部控件/窗口 ID          │
 * │                                                                    │
 * │ 所以【style_jl02.h 是自动生成的, 不要手改】——                      │
 * │ 每次用工具改界面都会被覆盖。要改的是本文件的映射。                  │
 * └────────────────────────────────────────────────────────────────────┘
 *
 * ID 是工具算出来的哈希(如 PAGE_1 = 0x420001), 不是顺序编号。
 * 其中高 3 位是"资源工程号 pj_id"((id >> 29) & 0x7), 本工程只有一套
 * JL 资源, 所有 ID 的高 3 位都是 0 —— 详见 config/ui_port_registry.c 第三节。
 */
#ifndef __UI_STYLE_H__
#define __UI_STYLE_H__

#include "jl_typedef.h"

/* 资源工具生成的 ID 表(513 个 #define)。自动生成, 勿手改 */
#include "style_jl02.h"

/** 风格名。
 *
 * 703 靠它在多套 handler 表里选一套: 宏展开后会拼出段名
 * .elm_event_handler_JL, 再由 REGISTER_UI_STYLE(STYLE_NAME) 把段边界
 * 包成 ui_style_info; ui_core_set_style() 拿资源文件名推出的 "JL"
 * 去匹配。对不上就整屏不响应, 且编译链接全过 —— 典型静默故障。
 *
 * 本移植已去掉"风格"这一层: 事件回调改成一页一张表, 全部登记在
 * config/ui_port_registry.c 的 g_ui_handler_table 里并同时生效
 * (见 include/ui/ui_core.h 的说明), ui_core_set_style() 退化成一句日志。
 * 所以这个宏现在只是个注释性的存在, 改了也不会有任何后果。
 */
#define STYLE_NAME      JL


/* ======================================================================
 * 窗口 ID 映射
 *
 * 取值与 703 的 ui_style.h 在 CONFIG_UI_STYLE == STYLE_JL_SOUNDBOX 分支下
 * 【逐字一致】—— 因为用的就是同一个 UI 工程(ui_128_64_JL02)。
 *
 * 本工程只画到 PAGE_10, 没有 SPDIF / SINK 页面, 那两个用 (-1) 表示"无窗口"
 * (框架对 -1 的处理是不显示, 不会当成合法 id 去查资源)。
 * ====================================================================== */

#define ID_WINDOW_MAIN          PAGE_2      /* 系统页面 */
#define ID_WINDOW_BT            PAGE_0      /* 蓝牙 */
#define ID_WINDOW_MUSIC         PAGE_1      /* 音乐 */

/* ui_128_64_JL02 工程没画这两页; 保留 703 的写法, 将来工具里加了页面
 * 就会自动生效(PAGE_11/PAGE_12 一旦被生成出来, 这里就用真值) */
#ifdef PAGE_11
#define ID_WINDOW_SPDIF         PAGE_11
#else
#define ID_WINDOW_SPDIF         (-1)
#endif
#ifdef PAGE_12
#define ID_WINDOW_SINK          PAGE_12
#else
#define ID_WINDOW_SINK          (-1)
#endif

/* lcd_drive/middle/lcd_ui_api.c 直接引用了 ID_WINDOW_VMENU(判断"当前是否在竖向菜单")。
 * ui_128_64_JL02 里没有独立的竖向菜单页, 用 (-1) 表示不存在 —— 那处判断
 * 恒不成立, 与 703 点阵屏配置的行为一致。 */
#ifndef ID_WINDOW_VMENU
#define ID_WINDOW_VMENU         (-1)
#endif


/* ======================================================================
 * 应用菜单枚举
 *
 * 仅 LED7 数码管风格用得到(jl_ui_api.h 里 ui_set_main_menu 的形参类型)。
 * 点阵屏走框架通路, 这里只需要让声明能编过。
 * ====================================================================== */
enum ui_menu_main {
    MENU_MAIN_NONE = 0,
    MENU_MAIN_MAX,
};

#endif /* __UI_STYLE_H__ */
