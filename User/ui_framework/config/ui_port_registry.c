/**
 * @file    ui_port_registry.c
 * @brief   显式注册表 —— 控件 / UI 风格 / 推屏接口
 *
 * 原厂靠链接脚本收集三个段(.control_ops / .ui_style / .lcd_if_info)得到
 * begin/end 边界符号。移植到 Keil 后改成本文件里的三张显式表, 原因:
 *
 *   1. armlink 没有 GNU ld 的 PROVIDE, 造不出那类段边界符号;
 *   2. 段收集漏一项是【静默故障】—— 编译链接全过, 只是界面上某类元素
 *      整块不显示, 极难定位。显式表漏了则是编译期未定义符号;
 *   3. 换编译器 / 链接器不用再改一次。
 *
 * ⚠ 新增控件或新增风格时, 必须往对应的表里加一行。
 */
/* 打开本文件的分级日志。jl_debug.h 的 log_* 是靠这几个宏开关的,
 * 不定义就是空实现 —— port 层是上板排查的关键路径, 必须留着。 */
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE

#include "ui/includes.h"
#include "ui/control.h"
#include "ui/ui_core.h"
#include "jl_lcd_drive.h"
#include "jl_ui_api.h"
#include "ui_port_config.h"

/* ==================================================================== *
 *  一、控件类型注册表
 *
 *  每一项由控件自己的 .c 用 REGISTER_CONTROL_OPS(类型) 定义,
 *  符号名固定为 control_ops_<类型宏名>。
 * ==================================================================== */

extern const struct control_ops control_ops_CTRL_TYPE_LAYOUT;
extern const struct control_ops control_ops_CTRL_TYPE_WINDOW;
extern const struct control_ops control_ops_CTRL_TYPE_TEXT;
extern const struct control_ops control_ops_CTRL_TYPE_PIC;
extern const struct control_ops control_ops_CTRL_TYPE_NUMBER;
extern const struct control_ops control_ops_CTRL_TYPE_BATTERY;
extern const struct control_ops control_ops_CTRL_TYPE_TIME;
extern const struct control_ops control_ops_CTRL_TYPE_SLIDER;
extern const struct control_ops control_ops_CTRL_TYPE_VSLIDER;
extern const struct control_ops control_ops_CTRL_TYPE_GRID;

/**
 * 全部参与编译的控件。顺序不重要(按 type 查表), 但保持与
 * include/ui/control.h 里 CTRL_TYPE_* 的定义顺序一致便于核对。
 *
 * @note 这 9 项对应 liba/ui_dot/ 下 9 处 REGISTER_CONTROL_OPS。
 *       CTRL_TYPE_WINDOW 由 liba/ui_dot/window.c 提供但【没有】用
 *       REGISTER_CONTROL_OPS 注册 —— 窗口是控件树的根, 由
 *       window_show() 直接创建, 不走 get_control_ops_by_type,
 *       所以不列进表里。
 */
const struct control_ops *const g_control_ops_table[] = {
    &control_ops_CTRL_TYPE_LAYOUT,
    &control_ops_CTRL_TYPE_TEXT,
    &control_ops_CTRL_TYPE_PIC,
    &control_ops_CTRL_TYPE_NUMBER,
    &control_ops_CTRL_TYPE_BATTERY,
    &control_ops_CTRL_TYPE_TIME,
    &control_ops_CTRL_TYPE_SLIDER,
    &control_ops_CTRL_TYPE_VSLIDER,
    &control_ops_CTRL_TYPE_GRID,
    NULL,   /* 结束标记 */
};

const struct control_ops *get_control_ops_by_type(int type)
{
    const struct control_ops *const *pp;

    for (pp = g_control_ops_table; *pp != NULL; pp++) {
        if ((*pp)->type == type) {
            return *pp;
        }
    }
    return NULL;
}


/* ==================================================================== *
 *  二、页面事件回调注册表
 *
 *  每个页面在 ui_action/<页面>_action.c 里定义自己那张
 *  (窗口/控件 id -> ontouch/onkey/onchange) 表, 并包成一个
 *  const struct ui_handler_group ui_handlers_<页面>。
 *
 *  ⚠ 新增页面要动两处: 写 ui_action/<页面>_action.c, 然后在这里加
 *    一条 extern + 表里加一行。漏了是编译期未定义符号, 不是运行期静默失效。
 *
 *  原厂靠链接脚本收集 .elm_event_handler_JL 段 + .ui_style 段做同一件事,
 *  为什么改成显式表见 include/ui/ui_core.h 的说明。
 * ==================================================================== */

extern const struct ui_handler_group ui_handlers_music;
extern const struct ui_handler_group ui_handlers_bt;
extern const struct ui_handler_group ui_handlers_system;

const struct ui_handler_group *const g_ui_handler_table[] = {
    &ui_handlers_music,
    &ui_handlers_bt,
    &ui_handlers_system,
    NULL,   /* 结束标记 */
};


/* ==================================================================== *
 *  三、资源工程(project)表
 *
 *  liba/res/resfile.c 用它把"工程号 pj_id"映射到一套资源文件路径, 供
 *  ui_load_res_by_pj_id / ui_load_sty_by_pj_id 查找。
 *
 *  原厂定义在杰理 SDK 的 platform/watch_bgp.c 里(彩屏手表的多套表盘各占一个 pj_id)。
 *  该文件是彩屏表盘管理, 点阵屏用不到, 已从工程移除, 表挪到这里 ——
 *  它本质就是一张注册表, 和上面两张放一起。
 *
 *  点阵屏只有一套 JL 资源, 不需要按 pj_id 分流, 所以表里只有哨兵项。
 *  @note 哨兵靠 pj_id 越界生效: 字段是 u8, -1 存进去是 255,
 *        resfile.c 的循环判 `pj_id > PJ_ID_MAX(7)` 就跳出。
 *        要加第二套资源时, 在哨兵【之前】插入 {pj_id, "路径", NULL}。
 * ==================================================================== */

struct ui_load_info ui_load_info_table[] = {
    { -1, NULL, NULL },     /* 表末哨兵, 必须保留 */
};


/* ==================================================================== *
 *  四、推屏接口注册表
 *
 *  由 lcd_drive/middle/ui_pushScreen_manager.c 用 REGISTER_LCD_INTERFACE(lcd)
 *  定义, 一个工程只会有一个生效(其余被 #if 排除)。
 * ==================================================================== */

extern const struct lcd_interface lcd;

/**
 * @brief 取推屏接口句柄
 * @note 原实现遍历 .lcd_if_info 段并返回第一项; 这里直接返回唯一那一项,
 *       行为等价。
 */
struct lcd_interface *lcd_get_hdl(void)
{
    /* 去掉 const: 框架的 struct lcd_interface * 返回类型没带 const,
     * 但只读不写(ui_platform.c 里只取函数指针调用) */
    return (struct lcd_interface *)&lcd;
}
