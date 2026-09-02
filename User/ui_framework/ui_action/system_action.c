/**
 * @file    system_action.c
 * @brief   SYSTEM 页面(PAGE_2)的事件响应
 *
 * 对应 703 SDK 的 apps/soundbox/ui/lcd/STYLE_SOUNDBOX/system_action.c。
 * 本文件【只做事件骨架】—— 按键分发、弹层显示/隐藏、控件生命周期钩子;
 * 背光/语言/自动关机/恢复出厂/升级这些业务动作一律留 TODO。
 *
 * ┌─ 与 703 原版的两处差异(与 music/bt_action.c 相同) ─────────────────┐
 * │ 1. 注册方式: 文末直接定义 ui_handlers_system, 再到                 │
 * │    config/ui_port_registry.c 的 g_ui_handler_table 里登记一行。    │
 * │ 2. 键值: 只有 KEY_PAGE_ENTER / BACK / PREV / NEXT 四个。           │
 * │    原版的 ID_WINDOW_SYS 在本工程叫 ID_WINDOW_MAIN(都是 PAGE_2)。   │
 * └────────────────────────────────────────────────────────────────────┘
 *
 * 页面结构(由 tools/JL/JL.sty 解析得到)。这页是"一个主列表 + 一堆二级弹层",
 * 每个弹层里一个 grid:
 *
 *   SYSTEM_LAYER
 *    ├─ SYSTEM_LAYOUT          设置主界面(常显)
 *    │    SYSTEM_BAT / SYSTEM_SET_TEXT
 *    │    SYSTEM_SET_LIST      (5 项) 系统 / 语言 / 自动关机 / 背光 / 返回
 *    ├─ SYSTEM_M_LAYOUT        系统菜单   -> SYSTEM_M_LIST           (4 项)
 *    ├─ SYS_LANGUAGE           语言       -> SYS_LANGUAGE_LIST       (4 项)
 *    ├─ SYS_POWEROFF           自动关机   -> SYS_POWEROFF_LIST       (6 项)
 *    ├─ SYS_BACKLIGHT          背光       -> SYS_BACKLIGHT_LIST      (3 项)
 *    ├─ SYS_BACKLIGHT_TIME     背光时间   -> SYS_BACKLIGHT_TIME_LIST (6 项)
 *    ├─ SYS_BACKLIGHT_VALUE    背光亮度   -> SYS_BACKLIGHT_VALUE_LIST(4 项)
 *    ├─ SYS_MSG_LAYOUT         本机信息   -> SYS_MSG_INFO_LIST       (2 项)
 *    └─ SYSTEM_UPDATE          升级提示层 -> UPDATE_FAIL_TXT / UPDATE_PAUSE
 *
 * @note 各项的中文含义靠子布局名推断(如 SYS_BACKLIGHT_VALUE_0..3 = 亮度档位),
 *       文字是图片资源看不出来, 具体次序标了 TODO(核对), 在 UI 工具里确认。
 */
#include "ui_action.h"


/* ====================================================================== *
 *  一、窗口: ID_WINDOW_MAIN (PAGE_2, 703 里叫 ID_WINDOW_SYS)
 * ====================================================================== */

static int sys_win_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct window *window = (struct window *)ctrl;

    (void)window;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        printf("sys: window init\n");
        /* TODO: 读一次系统设置(背光时间/亮度/语言/自动关机), 缓存到页面状态 */
        break;

    case ON_CHANGE_RELEASE:
        printf("sys: window release\n");
        /* TODO: 需要的话把改动落盘 */
        break;

    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  二、二级弹层的公共 onchange
 *
 *  703 原版所有二级弹层(SYSTEM_M_LAYOUT / SYS_LANGUAGE / SYS_POWEROFF /
 *  SYS_BACKLIGHT*)都挂同一个 common_layout_onchange, 只做两件事:
 *      ON_CHANGE_INIT    -> layout_on_focus(layout)
 *      ON_CHANGE_RELEASE -> layout_lose_focus(layout)
 *
 *  这是必须的: 拿到焦点后按键才会优先送到这一层, 关层时再交还,
 *  否则弹层与底下的 SYSTEM_LAYOUT 会抢按键。
 * ====================================================================== */

static int sys_popup_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct layout *layout = (struct layout *)ctrl;

    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        layout_on_focus(layout);
        break;
    case ON_CHANGE_RELEASE:
        layout_lose_focus(layout);
        break;
    default:
        break;
    }

    return FALSE;
}


/* ====================================================================== *
 *  三、设置主列表: SYSTEM_SET_LIST (5 项)
 *
 *  子布局顺序即项序, 名字是工具里起的:
 *  SYSTEM_MENU_LIST / LANGUAGE_MENU_LIST / POWER_MENU_LIST /
 *  BACKLIGHT_MENU_LIST / BACK_MENU_LIST
 * ====================================================================== */

enum {
    SYS_SET_ITEM_SYSTEM = 0,    /* 系统菜单 */
    SYS_SET_ITEM_LANGUAGE,      /* 语言 */
    SYS_SET_ITEM_POWEROFF,      /* 自动关机 */
    SYS_SET_ITEM_BACKLIGHT,     /* 背光 */
    SYS_SET_ITEM_BACK,          /* 返回上一个页面 */
};

static int sys_set_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;   /* 交给 grid 内置滚动 */
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        switch (ui_grid_cur_item(grid)) {
        case SYS_SET_ITEM_SYSTEM:
            ui_show(SYSTEM_M_LAYOUT);
            break;
        case SYS_SET_ITEM_LANGUAGE:
            ui_show(SYS_LANGUAGE);
            break;
        case SYS_SET_ITEM_POWEROFF:
            ui_show(SYS_POWEROFF);
            break;
        case SYS_SET_ITEM_BACKLIGHT:
            ui_show(SYS_BACKLIGHT);
            break;
        case SYS_SET_ITEM_BACK:
            /* 703 原版: UI_HIDE_CURR_WINDOW() + ui_show_main(-1) 回上一页。
             * 本工程目前只在开机时 UI_SHOW_WINDOW(ID_WINDOW_MUSIC), 没有
             * "上一页"的概念, 先直接切回音乐页。 */
            UI_HIDE_CURR_WINDOW();
            UI_SHOW_WINDOW(ID_WINDOW_MUSIC);
            break;
        default:
            break;
        }
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  四、系统菜单: SYSTEM_M_LIST (4 项)
 *
 *  SYSTEM_INFO_LIST / SYSTEM_RESET_LIST / SYSTEM_UPDATE_LIST /
 *  SYSTEM_M_BACK_LIST
 * ====================================================================== */

enum {
    SYS_MENU_ITEM_INFO = 0,     /* 本机信息 */
    SYS_MENU_ITEM_RESET,        /* 恢复出厂设置 */
    SYS_MENU_ITEM_UPDATE,       /* 固件升级 */
    SYS_MENU_ITEM_BACK,         /* 返回 */
};

static int sys_menu_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        switch (ui_grid_cur_item(grid)) {
        case SYS_MENU_ITEM_INFO:
            ui_show(SYS_MSG_LAYOUT);
            break;

        case SYS_MENU_ITEM_RESET:
            /* TODO: 恢复出厂 —— 703 只重置背光时间/亮度/自动关机时间,
             *       并且重置后要把背光时间再设一次, 否则按键处理里会
             *       立刻按"背光时间 0"去关背光。 */
            ui_hide(SYSTEM_M_LAYOUT);
            break;

        case SYS_MENU_ITEM_UPDATE:
            /* TODO: 依次在各存储设备上找升级文件; 没找到就弹 SYSTEM_UPDATE
             *       提示层并挂一个 2s 定时器自动收起。 */
            ui_show(SYSTEM_UPDATE);
            break;

        case SYS_MENU_ITEM_BACK:
        default:
            ui_hide(SYSTEM_M_LAYOUT);
            break;
        }
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYSTEM_M_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  五、语言: SYS_LANGUAGE_LIST (4 项)
 *
 *  子布局 SYSTEM_55 / SYS_8 / SYS_9 / SYS_10 —— 名字看不出语种,
 *  703 那边是 简体/繁体/英文/返回。
 * ====================================================================== */

#define SYS_LANGUAGE_ITEM_NUM   4
#define SYS_LANGUAGE_ITEM_BACK  (SYS_LANGUAGE_ITEM_NUM - 1)  /* TODO(核对) */

static int sys_language_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != SYS_LANGUAGE_ITEM_BACK) {
            printf("sys: set language %d\n", item);
            /* TODO: 切语言 + 重新加载字库 */
        }
        ui_hide(SYS_LANGUAGE);
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYS_LANGUAGE);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  六、自动关机时间: SYS_POWEROFF_LIST (6 项)
 * ====================================================================== */

#define SYS_POWEROFF_ITEM_NUM   6
#define SYS_POWEROFF_ITEM_BACK  (SYS_POWEROFF_ITEM_NUM - 1)  /* TODO(核对) */

static int sys_poweroff_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != SYS_POWEROFF_ITEM_BACK) {
            printf("sys: set auto poweroff %d\n", item);
            /* TODO: 写自动关机时间 */
        }
        ui_hide(SYS_POWEROFF);
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYS_POWEROFF);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  七、背光: SYS_BACKLIGHT_LIST (3 项)
 *
 *  703 那边这一层是入口菜单: 亮度 / 时间 / 返回, 选中后再进二级层。
 * ====================================================================== */

enum {
    SYS_BL_ITEM_VALUE = 0,      /* TODO(核对): 亮度 */
    SYS_BL_ITEM_TIME,           /* TODO(核对): 时间 */
    SYS_BL_ITEM_BACK,           /* TODO(核对): 返回 */
};

static int sys_backlight_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        switch (ui_grid_cur_item(grid)) {
        case SYS_BL_ITEM_VALUE:
            ui_show(SYS_BACKLIGHT_VALUE);
            break;
        case SYS_BL_ITEM_TIME:
            ui_show(SYS_BACKLIGHT_TIME);
            break;
        case SYS_BL_ITEM_BACK:
        default:
            ui_hide(SYS_BACKLIGHT);
            break;
        }
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYS_BACKLIGHT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  八、背光时间: SYS_BACKLIGHT_TIME_LIST (6 项)
 * ====================================================================== */

#define SYS_BL_TIME_ITEM_NUM    6
#define SYS_BL_TIME_ITEM_BACK   (SYS_BL_TIME_ITEM_NUM - 1)   /* TODO(核对) */

static int sys_backlight_time_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != SYS_BL_TIME_ITEM_BACK) {
            printf("sys: set backlight time %d\n", item);
            /* TODO: set_backlight_time(档位对应的秒数)
             * @note 本工程是自发光 OLED, 没有背光脚(ui_lcd_has_backlight()
             *       返回 0), 这一层只改设置值, 实际不会有可见效果。 */
        }
        ui_hide(SYS_BACKLIGHT_TIME);
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYS_BACKLIGHT_TIME);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  九、背光亮度: SYS_BACKLIGHT_VALUE_LIST (4 项)
 * ====================================================================== */

#define SYS_BL_VALUE_ITEM_NUM   4
#define SYS_BL_VALUE_ITEM_BACK  (SYS_BL_VALUE_ITEM_NUM - 1)  /* TODO(核对) */

static int sys_backlight_value_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != SYS_BL_VALUE_ITEM_BACK) {
            printf("sys: set backlight value %d\n", item);
            /* TODO: 写亮度档位。同样受"本屏无背光"限制, 见上。 */
        }
        ui_hide(SYS_BACKLIGHT_VALUE);
        break;

    case KEY_PAGE_BACK:
        ui_hide(SYS_BACKLIGHT_VALUE);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  十、本机信息: SYS_MSG_INFO_LIST (2 项)
 *
 *  703 那边这一层展示版本号/剩余空间, 由 SYSTEM_31(版本) 与 SYSTEM_35(空间)
 *  两个 text 的 onchange 去填内容。
 * ====================================================================== */

static int sys_msg_list_onkey(void *ctrl, struct element_key_event *e)
{
    (void)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
    case KEY_PAGE_BACK:
        ui_hide(SYS_MSG_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}

/* 版本号文本 SYSTEM_31 / 剩余空间文本 SYSTEM_35 共用, 靠 elm.id 区分 */
static int sys_info_text_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct ui_text *text = (struct ui_text *)ctrl;

    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        if (text->elm.id == SYSTEM_31) {
            /* TODO: ui_text_set_str(text, NULL, 版本号字符串, len, 0) */
        } else if (text->elm.id == SYSTEM_35) {
            /* TODO: 填剩余空间 */
        }
        break;
    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  十一、升级提示层: SYSTEM_UPDATE
 * ====================================================================== */

static int sys_update_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: 按检查结果切换 UPDATE_FAIL_TXT / NO_UPDATEFAIL_TXT 的文字,
         *       并挂 2s 定时器自动收起本层 */
        break;
    case ON_CHANGE_RELEASE:
        /* TODO: 删定时器 */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  十二、电池
 * ====================================================================== */

static int sys_bat_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct ui_battery *battery = (struct ui_battery *)ctrl;

    (void)battery;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: ui_battery_set_level(battery, 电量百分比, 是否在充电)
         *       + 起 1s 定时器周期刷新 */
        break;
    case ON_CHANGE_RELEASE:
        /* TODO: 删定时器 */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  十三、本页回调表
 *
 *  定义完要去 config/ui_port_registry.c 的 g_ui_handler_table 里加一行
 *  &ui_handlers_system —— 漏了是链接期未定义符号, 编不过。
 * ====================================================================== */

static const struct element_event_handler system_handlers[] = {

    /* 窗口 */
    {
        .id       = ID_WINDOW_MAIN,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_win_onchange,
    },

    /* 二级弹层: 统一做 layout_on_focus / lose_focus */
    {
        .id       = SYSTEM_M_LAYOUT,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_LANGUAGE,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_POWEROFF,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_BACKLIGHT,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_BACKLIGHT_TIME,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_BACKLIGHT_VALUE,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYS_MSG_LAYOUT,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_popup_onchange,
    },
    {
        .id       = SYSTEM_UPDATE,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_update_onchange,
    },

    /* 列表 */
    {
        .id       = SYSTEM_SET_LIST,
        .ontouch  = NULL,
        .onkey    = sys_set_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYSTEM_M_LIST,
        .ontouch  = NULL,
        .onkey    = sys_menu_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_LANGUAGE_LIST,
        .ontouch  = NULL,
        .onkey    = sys_language_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_POWEROFF_LIST,
        .ontouch  = NULL,
        .onkey    = sys_poweroff_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_BACKLIGHT_LIST,
        .ontouch  = NULL,
        .onkey    = sys_backlight_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_BACKLIGHT_TIME_LIST,
        .ontouch  = NULL,
        .onkey    = sys_backlight_time_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_BACKLIGHT_VALUE_LIST,
        .ontouch  = NULL,
        .onkey    = sys_backlight_value_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = SYS_MSG_INFO_LIST,
        .ontouch  = NULL,
        .onkey    = sys_msg_list_onkey,
        .onchange = NULL,
    },

    /* 显示类控件 */
    {
        .id       = SYSTEM_BAT,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_bat_onchange,
    },
    {
        .id       = SYSTEM_31,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_info_text_onchange,
    },
    {
        .id       = SYSTEM_35,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = sys_info_text_onchange,
    },
};

const struct ui_handler_group ui_handlers_system = {
    .begin = system_handlers,
    .end   = system_handlers + ARRAY_SIZE(system_handlers),
};
