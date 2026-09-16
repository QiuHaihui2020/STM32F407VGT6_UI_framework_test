/**
 * @file    ui_action.h
 * @brief   ui_action/ 各页面共用的东西
 *
 * 只放"每个页面都要用、又不值得各写一份"的小工具。页面之间的业务不要
 * 通过这个头互相调用 —— 页面是平级的, 跨页动作走 ui_show/ui_hide 或
 * app 消息。
 */
#ifndef __UI_ACTION_H__
#define __UI_ACTION_H__

#include "ui/ui.h"
#include "ui_style.h"
#include "jl_ui_api.h"   /* UI_SHOW_WINDOW / UI_HIDE_CURR_WINDOW 等对外 API */
#include "apps.h"       /* KEY_PAGE_*: 与 app_core.c 投递的键值同一份定义 */


/**
 * @brief 列表导航键转译: KEY_PAGE_PREV/NEXT -> UI_KEY_UP/DOWN
 *
 * grid 自带上下键滚动(liba/ui_dot/ui_grid.c 的 grid_onkey), 但它只认
 * UI_KEY_UP/DOWN/LEFT/RIGHT。而 grid_onkey 的写法是【先调本页注册的 onkey,
 * 返回 0 才走自己那段 switch】—— 所以把键值就地改写成 UI_KEY_UP/DOWN 再
 * 返回 FALSE, 滚动/高亮/翻页就全部交给框架, 不用自己算 index。
 *
 * 用法(每个 grid 的 onkey 开头):
 *     if (ui_action_list_nav_key(e)) {
 *         return FALSE;   // 交给 grid 内置滚动
 *     }
 *
 * @return 1 = 已转译(调用方应 return FALSE 让 grid 接手); 0 = 不是导航键
 */
static inline int ui_action_list_nav_key(struct element_key_event *e)
{
    switch (e->value) {
    case KEY_PAGE_PREV:
        e->value = UI_KEY_UP;
        return 1;
    case KEY_PAGE_NEXT:
        e->value = UI_KEY_DOWN;
        return 1;
    default:
        return 0;
    }
}

#endif /* __UI_ACTION_H__ */
