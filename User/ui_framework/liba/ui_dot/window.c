/*
 * window.c —— 窗口(界面根容器)
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的:
 *   struct window: elm=0 busy=72 hide=73 ctrl_num=74 entry=76 layer=84
 *                  info=88 handler=92 private_data=96, sizeof=100
 *   struct window_info: type=0 ctrl_num=1 css_num=2 len=3 rev[4]=4
 *                       rect=8(left8 top12 width16 height20) layer=24
 *
 * 【窗口栈】模块内有一个静态链表头 head(放在 .window.data 段), window_show
 *   时把新窗口挂进去, __window_hide 时摘掉。
 *
 * 【busy/hide 的配合】ui_core_ontouch / ui_core_element_onkey 期间置 busy,
 *   这段时间里若有人调 window_hide, __window_hide 只把 hide 置 1 就返回 ——
 *   不能在事件分发过程中把自己 free 掉。等事件处理返回后再补做真正的销毁。
 *   这也是 window_ontouch/window_onkey 末尾那段"清 hide 再 __window_hide(w, 0)"
 *   的用途; 传 0 是因为 layer_delete_probe 已经在第一次调用里做过了。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".window.data.bss")
#pragma data_seg(".window.data")
#pragma const_seg(".window.text.const")
#pragma code_seg(".window.text")
#endif

#include "ui/window.h"
#include "ui/control.h"

static LIST_HEAD(head);

static int __window_onkey(void *_window, struct element_key_event *e)
{
    struct window *window = (struct window *)_window;

    if (window->handler && window->handler->onkey) {
        if (window->handler->onkey(window, e)) {
            return true;
        }
    }

    return false;
}

static int __window_ontouch(void *_window, struct element_touch_event *e)
{
    struct window *window = (struct window *)_window;

    if (window->handler && window->handler->ontouch) {
        return window->handler->ontouch(window, e);
    }

    return false;
}

/*
 * @note 与 __window_onkey 不同, 这里【无条件返回 true】—— 应用 handler 的
 *       返回值不参与判断, onchange 不做"吃掉事件"这件事。
 */
static int __window_onchange(void *_window, enum element_change_event event,
                             void *arg)
{
    struct window *window = (struct window *)_window;

    if (window->handler && window->handler->onchange) {
        window->handler->onchange(window, event, arg);
    }

    return true;
}

static const struct element_event_handler window_event_handler = {
    .id       = 0,
    .ontouch  = __window_ontouch,
    .onkey    = __window_onkey,
    .onchange = __window_onchange,
};

/*
 * @param id 窗口 id; 低 8 位同时用作资源的 page 号
 * @return 0 成功; -ENOMEM 失败
 *
 * @note 失败路径统一走 __err1: 卸载已加载的窗口资源 + 打印。从 malloc 失败
 *       那条路径跳进来时 info 还是 NULL, 所以 __err1 里要先判空。
 */
int window_show(int id)
{
    struct window *window;
    const struct window_info *info = NULL;
    struct element_css1 css;

    printf("window_show %d\n", (u8)id);

    window = ui_core_malloc(sizeof(struct window));
    if (!window) {
        goto __err1;
    }

    window->info = NULL;

    info = platform_api->load_widget_info(NULL, (u8)id);
    if (!info) {
        ui_core_free(window);
        goto __err1;
    }

    /*
     * 【这一句不能少】window->info 要指向刚加载到的资源信息: __window_hide 里
     * 靠 `if (window->info)` 决定是否调 unload_window, 少了这句就等于每开关
     * 一次窗口漏一个资源句柄。
     *
     * @note 窗口资源如果在别处是共享的, 关窗时的提前释放会波及那边 ——
     *       改动这一带时要上板验证一遍。
     */
    window->info = info;

    memset(&css, 0, sizeof(css));
    css.left             = info->rect.left;
    css.top              = info->rect.top;
    css.width            = info->rect.width;
    css.height           = info->rect.height;
    css.background_color = 0xffffff;

    window->ctrl_num = info->ctrl_num;
    window->busy     = 0;
    /* busy 与 hide 都要在这里清: window 来自 ui_core_malloc, 内容未初始化。
     * hide 若恰好非 0, 第一次按键/触摸结束后就会立刻把这个窗口关掉
     * (见 window_onkey / window_ontouch 末尾那段)。 */
    window->hide     = 0;

    window->handler = element_event_handler_for_id(id);

    ui_core_element_init(&window->elm, id, 0, 0, &css, &window_event_handler, NULL);
    ui_core_append_child(window);

    if (window->handler && window->handler->onchange) {
        window->handler->onchange(window, ON_CHANGE_INIT, NULL);
    }

    window->layer = layer_new(info->layer, info->ctrl_num, &window->elm);
    if (!window->layer) {
        goto __err;
    }

    ui_core_show(window, 1);

    list_add(&window->entry, &head);

    return 0;

__err:
    ui_core_remove_element(window);
    ui_core_free(window);
__err1:
    /* 从 "ui_core_malloc 失败" 这条路径跳进来时 info 仍是 NULL, 所以必须先
     * 判空 —— 否则 unload_window(&info->type) 拿到的是 NULL 加偏移得来的
     * 野指针, 崩不崩全看平台实现判不判空。 */
    if (info) {
        platform_api->unload_window((void *)&info->type);
    }
    puts("window show err!");

    return -ENOMEM;
}

/*
 * @param del_probe 非 0 时先做一遍 layer_delete_probe(给子控件"即将释放"的通知)
 *
 * @note busy 期间只置 hide 标记就返回, 真正的销毁推迟到事件分发结束
 *       (见文件头注释)。
 */
static void __window_hide(struct window *window, int del_probe)
{
    if (del_probe) {
        layer_delete_probe(window->layer, window->ctrl_num);
    }

    if (window->busy) {
        window->hide = 1;
        return;
    }

    if (window->handler && window->handler->onchange) {
        window->handler->onchange(window, ON_CHANGE_RELEASE, NULL);
    }

    list_del(&window->entry);

    layer_delete(window->layer, window->ctrl_num);
    ui_core_element_on_focus(&window->elm, 0);
    ui_core_remove_element(window);

    if (window->info) {
        platform_api->unload_window((void *)&window->info->type);
    }

    ui_core_free(window);
}

int window_hide(int id)
{
    struct window *window = (struct window *)ui_core_get_element_by_id(id);

    if (window) {
        __window_hide(window, 1);
    }

    return 0;
}

int window_toggle(int id)
{
    struct window *window = (struct window *)ui_core_get_element_by_id(id);

    if (window) {
        __window_hide(window, 1);
        return 0;
    }

    return window_show(id);
}

int window_ontouch(struct element_touch_event *e)
{
    struct window *window = (struct window *)ui_core_get_first_child();
    int ret;

    if (!window) {
        return 0;
    }

    window->busy = 1;
    ret = ui_core_ontouch(e);
    window->busy = 0;

    if (window->hide) {
        window->hide = 0;
        __window_hide(window, 0);
    }

    return ret;
}

int window_onkey(struct element_key_event *e)
{
    struct window *window = (struct window *)ui_core_get_first_child();
    int ret;

    if (!window) {
        puts("------no_window");
        return 0;
    }

    window->busy = 1;
    ret = ui_core_element_onkey(&window->elm, e);
    window->busy = 0;

    if (window->hide) {
        window->hide = 0;
        __window_hide(window, 0);
    }

    return ret;
}

/*
 * 实现注意事项
 *
 *  1) 【window->info 必须指向加载到的资源信息】__window_hide 靠它决定是否
 *     调 unload_window。漏了这一句, 每开关一次窗口就漏一个资源句柄。
 *     反过来, 窗口资源若在别处共享, 关窗时的释放会波及那边 —— 改这一带
 *     要上板验证。
 *
 *  2) 【__err1 要先判 info】从 malloc 失败那条路径跳进来时 info 还是 NULL,
 *     不判就会把 NULL 加偏移得来的野指针交给 unload_window。
 *
 *  3) 【busy / hide 都要显式清零】window 来自 ui_core_malloc, 内容未初始化。
 *     hide 若恰好非 0, 第一次按键/触摸结束后窗口就被关掉了。
 *
 *  4) 【busy 期间不能销毁自己】事件分发时置 busy, 这段时间里的 window_hide
 *     只置 hide 标记; 等 window_ontouch / window_onkey 返回后再补做真正的
 *     销毁(那时传 del_probe = 0, 因为 layer_delete_probe 已经做过一次)。
 */
