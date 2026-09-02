/**
 * @file    music_action.c
 * @brief   MUSIC 页面(PAGE_1)的事件响应
 *
 * 对应 703 SDK 的 apps/soundbox/ui/lcd/STYLE_SOUNDBOX/music_action.c。
 * 本文件【只做事件骨架】—— 按键分发、弹层显示/隐藏、控件生命周期钩子;
 * 播放器 / EQ / 音量 / 电量这些业务动作一律留 TODO, 由上层接。
 *
 * ┌─ 与 703 原版的两处差异 ────────────────────────────────────────────┐
 * │ 1. 注册方式: 原版用 REGISTER_UI_EVENT_HANDLER(id), 靠链接脚本收集   │
 * │    .elm_event_handler_<style> 段。本移植 sec() 是空宏, 留着就是      │
 * │    "编译过但注册不上"的静默故障, 改成一页一张表:                 │
 * │    文末直接定义 ui_handlers_music, 再到                            │
 * │    config/ui_port_registry.c 的 g_ui_handler_table 里登记一行。      │
 * │                                                                    │
 * │ 2. 键值: 原版用 UI_KEY_OK/MENU/UP/DOWN/VOLUME_*, 那是 703 按键驱动 │
 * │    的语义键。本工程 User/apps/app_core.c 只投递四个键:             │
 * │    KEY_PAGE_ENTER / KEY_PAGE_BACK / KEY_PAGE_PREV / KEY_PAGE_NEXT, │
 * │    所以这里按这四个键分发。框架本身不解释键值, 只原样透传。        │
 * └────────────────────────────────────────────────────────────────────┘
 *
 * 页面结构(由 tools/JL/JL.sty 解析得到):
 *
 *   MUSIC_LAYER
 *    ├─ MUSIC_LAYOUT        主播放界面(常显)
 *    │    MUSIC_BAT / MUSIC_CUR_TIME / MUSIC_TOTAL_TIME / MUSIC_CUR_NUM
 *    │    MUSIC_TOTAL_NUM / MUSIC_START / MUSIC_MLOOP / MUSIC_DEV
 *    │    MUSIC_EQ / MUSIC_FILE / MUSIC_LYRICS / MUSIC_LYRICS1
 *    ├─ MUSIC_MENU_LAYOUT   主菜单弹层    -> MUSIC_MENU_LIST  (4 项)
 *    ├─ MUSIC_EQ_LAYOUT     EQ 弹层       -> MUSIC_EQ_LIST    (7 项)
 *    ├─ MUSIC_VOL_LAYOUT    音量弹层      -> MUSIC_VOL_NUM / MUSIC_VOL_PIC
 *    ├─ MUSIC_FILE_LAYOUT   文件浏览弹层  -> MUSIC_FILE_BROWSE(4 项)
 *    └─ MUSIC_CYCLE_LAYOUT  循环模式弹层  -> MUSIC_CYCLE_LIST (6 项)
 *
 * @note 按键分发顺序是"子元素从尾往前, 都不消费才轮到自己", 且 invisible
 *       的整棵子树直接跳过(ui_core_dot.c 的 __ui_core_onkey)。弹层在树里
 *       排在 MUSIC_LAYOUT 之后, 所以弹层一旦显示就先拿到按键, 主界面收不到
 *       —— 不需要自己做状态判断。
 */
#include "ui_action.h"


/* ====================================================================== *
 *  一、窗口: ID_WINDOW_MUSIC (PAGE_1)
 * ====================================================================== */

static int music_win_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct window *window = (struct window *)ctrl;

    (void)window;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        printf("music: window init\n");
        /* TODO: 申请页面私有状态、注册 app 消息处理、起刷新定时器 */
        break;

    case ON_CHANGE_FIRST_SHOW:
        /* TODO: 首次显示, 拉一次播放器状态刷到界面上 */
        break;

    case ON_CHANGE_RELEASE:
        printf("music: window release\n");
        /* TODO: 删定时器、注销消息处理、释放页面私有状态 */
        break;

    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  二、主播放布局: MUSIC_LAYOUT
 * ====================================================================== */

static int music_layout_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        break;

    case ON_CHANGE_FIRST_SHOW:
        /* @note 布局第一次显示时才能安全地改子控件 —— 此时子控件已经建好。
         *       要在这里改界面用 ui_set_call() 推迟到本轮事件分发结束, 别
         *       在回调里直接 ui_show/ui_hide(会走 wait_call 队列, 语义绕)。 */
        /* TODO: ui_set_call(music_layout_init, 0); 刷文件名/曲目号/时间 */
        break;

    default:
        return FALSE;
    }

    return FALSE;
}

static int music_layout_onkey(void *ctrl, struct element_key_event *e)
{
    (void)ctrl;

    printf("music: layout key %d\n", e->value);

    switch (e->value) {
    case KEY_PAGE_ENTER:
        /* TODO: 播放/暂停, 并翻转 MUSIC_START 图标 */
        break;

    case KEY_PAGE_PREV:
        /* TODO: 上一曲 */
        break;

    case KEY_PAGE_NEXT:
        /* TODO: 下一曲 */
        break;

    case KEY_PAGE_BACK:
        /* 主界面按返回 = 打开主菜单。
         * 703 原版这里是独立的 UI_KEY_MENU, 本工程按键不够, 复用 BACK。 */
        ui_show(MUSIC_MENU_LAYOUT);
        break;

    default:
        return FALSE;
    }

    /*
     * @note 与 703 原版一致: 处理完仍返回 FALSE。
     *       返回 TRUE 会让框架把焦点(root.focus)设到这个 layout 上, 之后
     *       按键改走"焦点优先 + 向父节点冒泡"那条路(ui_core_element_onkey),
     *       弹层就再也拿不到按键了。
     */
    return FALSE;
}


/* ====================================================================== *
 *  三、主菜单列表: MUSIC_MENU_LIST (4 项)
 *
 *  项含义要在 UI 工具里核对。参考 703 的排布是 EQ / 循环模式 / 文件浏览 /
 *  返回, 本工程的 .sty 里子布局顺序是
 *  MUSIC_MENU_0 / MUSIC_22 / MUSIC_MENU_1 / MUSIC_MENU_2。
 * ====================================================================== */

enum {
    MUSIC_MENU_ITEM_EQ = 0,     /* TODO(核对): EQ */
    MUSIC_MENU_ITEM_CYCLE,      /* TODO(核对): 循环模式 */
    MUSIC_MENU_ITEM_FILE,       /* TODO(核对): 文件浏览 */
    MUSIC_MENU_ITEM_BACK,       /* TODO(核对): 返回 */
};

static int music_menu_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;   /* 交给 grid 内置滚动 */
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        switch (ui_grid_cur_item(grid)) {
        case MUSIC_MENU_ITEM_EQ:
            ui_show(MUSIC_EQ_LAYOUT);
            break;
        case MUSIC_MENU_ITEM_CYCLE:
            ui_show(MUSIC_CYCLE_LAYOUT);
            break;
        case MUSIC_MENU_ITEM_FILE:
            ui_show(MUSIC_FILE_LAYOUT);
            break;
        case MUSIC_MENU_ITEM_BACK:
            ui_hide(MUSIC_MENU_LAYOUT);
            break;
        default:
            break;
        }
        break;

    case KEY_PAGE_BACK:
        ui_hide(MUSIC_MENU_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  四、EQ 列表: MUSIC_EQ_LIST (7 项, 末项为返回)
 * ====================================================================== */

#define MUSIC_EQ_ITEM_NUM       7
#define MUSIC_EQ_ITEM_BACK      (MUSIC_EQ_ITEM_NUM - 1)

static int music_eq_list_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: 读当前 EQ 模式, ui_pic_show_image_by_id() 点亮对应项的图标 */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}

static int music_eq_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != MUSIC_EQ_ITEM_BACK) {
            printf("music: set eq %d\n", item);
            /* TODO: eq_mode_set(item) */
        }
        ui_hide(MUSIC_EQ_LAYOUT);
        break;

    case KEY_PAGE_BACK:
        ui_hide(MUSIC_EQ_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  五、循环模式列表: MUSIC_CYCLE_LIST (6 项, 末项为返回)
 * ====================================================================== */

#define MUSIC_CYCLE_ITEM_NUM    6
#define MUSIC_CYCLE_ITEM_BACK   (MUSIC_CYCLE_ITEM_NUM - 1)

static int music_cycle_list_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: 读当前循环模式, 点亮对应项图标 */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}

static int music_cycle_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;
    int item;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        item = ui_grid_cur_item(grid);
        if (item != MUSIC_CYCLE_ITEM_BACK) {
            printf("music: set repeat mode %d\n", item);
            /* TODO: music_player_set_repeat_mode(item) + 刷 MUSIC_MLOOP 图标 */
        }
        ui_hide(MUSIC_CYCLE_LAYOUT);
        break;

    case KEY_PAGE_BACK:
        ui_hide(MUSIC_CYCLE_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  六、文件浏览列表: MUSIC_FILE_BROWSE (4 个可见项, 内容动态)
 * ====================================================================== */

static int music_file_list_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: ui_grid_dynamic_create(grid, 方向, 文件总数, 取项回调)
         *       —— 文件数不定, 要走动态列表, 见 ui_grid.h */
        break;
    case ON_CHANGE_RELEASE:
        /* TODO: ui_grid_dynamic_release(grid) */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}

static int music_file_list_onkey(void *ctrl, struct element_key_event *e)
{
    struct ui_grid *grid = (struct ui_grid *)ctrl;

    if (ui_action_list_nav_key(e)) {
        return FALSE;
    }

    switch (e->value) {
    case KEY_PAGE_ENTER:
        printf("music: play file %d\n", ui_grid_cur_item(grid));
        /* TODO: 切到选中曲目并播放 */
        ui_hide(MUSIC_FILE_LAYOUT);
        break;

    case KEY_PAGE_BACK:
        ui_hide(MUSIC_FILE_LAYOUT);
        break;

    default:
        return FALSE;
    }

    return TRUE;
}


/* ====================================================================== *
 *  七、音量弹层: MUSIC_VOL_LAYOUT
 *
 *  703 原版在 ON_CHANGE_INIT 里挂一个 3 秒定时器自动收起, 按键时 modify
 *  续期。本工程没接 sys_timeout_*, 先留钩子。
 * ====================================================================== */

static int music_vol_layout_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    (void)ctrl;
    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        /* TODO: 起 3s 自动收起定时器 */
        break;

    case ON_CHANGE_FIRST_SHOW:
        /* TODO: ui_set_call(vol_init, 0); 把当前音量刷到 MUSIC_VOL_NUM */
        break;

    case ON_CHANGE_RELEASE:
        /* TODO: 删定时器 */
        break;

    default:
        return FALSE;
    }

    return FALSE;
}

static int music_vol_layout_onkey(void *ctrl, struct element_key_event *e)
{
    (void)ctrl;

    /* TODO: 有按键就把自动收起定时器续期 */

    switch (e->value) {
    case KEY_PAGE_PREV:
        /* TODO: 音量 +, 再 ui_number_update_by_id(MUSIC_VOL_NUM, &num) */
        break;

    case KEY_PAGE_NEXT:
        /* TODO: 音量 - */
        break;

    case KEY_PAGE_ENTER:
    case KEY_PAGE_BACK:
        ui_hide(MUSIC_VOL_LAYOUT);
        break;

    default:
        return FALSE;
    }

    /* 音量层显示期间要独占按键, 不能漏给下面的主界面 -> 返回 TRUE */
    return TRUE;
}


/* ====================================================================== *
 *  八、只有 onchange 的显示类控件
 * ====================================================================== */

static int music_bat_onchange(void *ctrl, enum element_change_event event, void *arg)
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

/* MUSIC_CUR_TIME 与 MUSIC_TOTAL_TIME 共用, 靠 elm.id 区分 */
static int music_time_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    struct ui_time *time = (struct ui_time *)ctrl;

    (void)arg;

    switch (event) {
    case ON_CHANGE_INIT:
        if (time->text.elm.id == MUSIC_CUR_TIME) {
            /* TODO: 取当前播放秒数 */
        } else if (time->text.elm.id == MUSIC_TOTAL_TIME) {
            /* TODO: 取总时长秒数 */
        }
        /* TODO: time->hour/min/sec = ...; 控件自己会画 */
        break;
    default:
        return FALSE;
    }

    return FALSE;
}


/* ====================================================================== *
 *  九、本页回调表
 *
 *  定义完要去 config/ui_port_registry.c 的 g_ui_handler_table 里加一行
 *  &ui_handlers_music —— 漏了是链接期未定义符号, 编不过, 不会变成
 *  "界面能显示但不响应"。
 * ====================================================================== */

static const struct element_event_handler music_handlers[] = {

    /* 窗口 */
    {
        .id       = ID_WINDOW_MUSIC,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = music_win_onchange,
    },

    /* 布局 */
    {
        .id       = MUSIC_LAYOUT,
        .ontouch  = NULL,
        .onkey    = music_layout_onkey,
        .onchange = music_layout_onchange,
    },
    {
        .id       = MUSIC_VOL_LAYOUT,
        .ontouch  = NULL,
        .onkey    = music_vol_layout_onkey,
        .onchange = music_vol_layout_onchange,
    },

    /* 列表 */
    {
        .id       = MUSIC_MENU_LIST,
        .ontouch  = NULL,
        .onkey    = music_menu_list_onkey,
        .onchange = NULL,
    },
    {
        .id       = MUSIC_EQ_LIST,
        .ontouch  = NULL,
        .onkey    = music_eq_list_onkey,
        .onchange = music_eq_list_onchange,
    },
    {
        .id       = MUSIC_CYCLE_LIST,
        .ontouch  = NULL,
        .onkey    = music_cycle_list_onkey,
        .onchange = music_cycle_list_onchange,
    },
    {
        .id       = MUSIC_FILE_BROWSE,
        .ontouch  = NULL,
        .onkey    = music_file_list_onkey,
        .onchange = music_file_list_onchange,
    },

    /* 显示类控件 */
    {
        .id       = MUSIC_BAT,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = music_bat_onchange,
    },
    {
        .id       = MUSIC_CUR_TIME,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = music_time_onchange,
    },
    {
        .id       = MUSIC_TOTAL_TIME,
        .ontouch  = NULL,
        .onkey    = NULL,
        .onchange = music_time_onchange,
    },
};

const struct ui_handler_group ui_handlers_music = {
    .begin = music_handlers,
    .end   = music_handlers + ARRAY_SIZE(music_handlers),
};
