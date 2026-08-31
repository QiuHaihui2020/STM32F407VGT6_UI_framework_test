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
 * │   ename.h     -> port/style_jl02.h     ★ 全部控件/窗口 ID          │
 * │                                                                    │
 * │ 所以【style_jl02.h 是自动生成的, 不要手改】——                      │
 * │ 每次用工具改界面都会被覆盖。要改的是本文件的映射。                  │
 * └────────────────────────────────────────────────────────────────────┘
 *
 * ID 是工具算出来的哈希(如 PAGE_1 = 0x420001), 不是顺序编号。
 * 其中高 3 位是"资源工程号 pj_id"((id >> 29) & 0x7), 本工程只有一套
 * JL 资源, 所有 ID 的高 3 位都是 0 —— 详见 port/ui_port_registry.c 第三节。
 */
#ifndef __UI_STYLE_H__
#define __UI_STYLE_H__

#include "jl_typedef.h"

/* 资源工具生成的 ID 表(513 个 #define)。自动生成, 勿手改 */
#include "style_jl02.h"

/** 风格名。
 *
 * ⚠ 必须是 JL, 不能改成 jl02 之类 —— 它要和 platform/ui_resources_manager.c
 *   传给 ui_core_set_style() 的字符串【逐字相同】, 而那个字符串是从资源
 *   文件名推导的:
 *
 *       strcpy(style_name, "JL.sty");
 *       style_name[6 - 4] = 0;          // 去掉 ".sty" -> "JL"
 *       ui_core_set_style(style_name);  // 传 "JL"
 *
 *   对不上的后果: ui_core_set_style 返回 -EINVAL,
 *   elm_event_handler_begin/end 保持为 NULL,
 *   所有控件都查不到事件回调 —— 界面画得出来但完全不响应。
 *
 *   703 的 apps/soundbox/ui/lcd/STYLE_SOUNDBOX 下那几个 action 源文件里,
 *   也都是 #define STYLE_NAME JL。
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

#define ID_WINDOW_MAIN          PAGE_0      /* 主界面 */
#define ID_WINDOW_BT            PAGE_1      /* 蓝牙 */
#define ID_WINDOW_FM            PAGE_2      /* 收音 */

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

/* platform/lcd_ui_api.c 直接引用了 ID_WINDOW_VMENU(判断"当前是否在竖向菜单")。
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
