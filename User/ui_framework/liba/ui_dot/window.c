/*
 * window.c —— 窗口(界面根容器)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 window.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/window.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/window.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   __window_onkey@14  __window_ontouch@27  __window_onchange@39
 *   window_show@60  __window_hide@135  window_hide@160  window_toggle@171
 *   window_ontouch@184  window_onkey@207
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct window: elm=0 busy=72 hide=73 ctrl_num=74 entry=76 layer=84
 *                  info=88 handler=92 private_data=96, sizeof=100
 *                  (ui_core_malloc(100) 直接印证)
 *   struct window_info: type=0 ctrl_num=1 css_num=2 len=3 rev[4]=4
 *                       rect=8(left8 top12 width16 height20) layer=24
 *
 * 【窗口栈】模块内有一个静态链表头 s_head, window_show 时把新窗口挂进去,
 *   __window_hide 时摘掉。原库这个变量就叫 head, 放在 .window.data 段。
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
 * @note 与 __window_onkey 不同, 这里【无条件返回 true】, 应用 handler 的返回值
 *       被丢弃(IR 里 %call 的结果没有任何使用者, 两个 ret 都是常量 1)。
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
 * @note 失败路径统一走 __err1: 卸载已加载的窗口资源 + 打印。注意 info 为
 *       NULL 时(malloc 就失败的那条路径)仍然会调 unload_window(NULL) ——
 *       原库如此, 见文末 TODO。
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
     * 加固【资源句柄泄漏】: 原库 window->info 只在上面被写过一次 NULL, 【此后
     * 再没赋过值】—— 加载到的 info 只活在这个局部变量里。于是 __window_hide
     * 里那句 `if (window->info)` 恒假, 正常关窗时 unload_window 永远不会被
     * 调用, 每开关一次窗口就漏一个资源句柄。
     * (IR 里对 window+88 只有 window_show 这一次 store null。)
     *
     * @note 这是【行为变化】: 补上之后关窗会真的走 unload_window。原库的
     *       意图显然如此(否则 __window_hide 里那个判断毫无意义), 但要真机
     *       验证一遍 —— 万一某处窗口资源是共享的, 提前释放会波及别处。
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
    /* 加固: 原库【只清了 busy 没清 hide】, 而 window 来自 ui_core_malloc,
     * 内容是未初始化的。hide 若恰好非 0, 第一次按键/触摸结束后就会立刻
     * 把这个窗口关掉(见 window_onkey / window_ontouch 末尾那段)。 */
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
    /* 加固: 从 "ui_core_malloc 失败" 这条路径跳进来时 info 仍是 NULL,
     * 原库照样调 unload_window(&info->type) —— 即 unload_window 拿到一个
     * 由 NULL 加偏移得来的野指针, 崩不崩全看平台实现判不判空。 */
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
 * 原库缺陷清单 + 加固状态(描述的是【原库】行为; 方括号是当前处理结果,
 * 差异已登记在 accept/ 并锁定指纹)。
 *
 *  [已修] 1. window->info 【从未被赋值】—— window_show 里只写了一次 NULL,
 *            加载到的 info 只活在局部变量里, 于是 __window_hide 中
 *            `if (window->info)` 恒假, 正常关窗时 unload_window 永远不会被
 *            调用, 每开关一次窗口就漏一个资源句柄。已补 window->info = info。
 *            @note 这是【行为变化】: 关窗会真的走 unload_window 了。原库意图
 *                  显然如此(否则那个判断毫无意义), 但要真机验证 —— 万一某处
 *                  窗口资源是共享的, 提前释放会波及别处。
 *  [已修] 2. __err1 在 "ui_core_malloc 失败" 这条路径上 info 仍是 NULL, 却照样
 *            调 unload_window(&info->type) —— 拿到的是 NULL 加偏移得来的野指针,
 *            崩不崩全看平台实现判不判空。已补 if (info)。
 *  [已修] 3. window->hide 在 window_show 里没有初始化(只清了 busy), 用的是
 *            ui_core_malloc 返回的未初始化内存。若恰好非 0, 第一次按键/触摸
 *            结束后就会立刻把窗口关掉。已补 window->hide = 0。
 *
 *  1. window->info 【从未被赋值】—— window_show 里只写了一次 NULL, 加载到的
 *     info 只存在局部变量里。于是 __window_hide 中 `if (window->info)` 恒假,
 *     正常关窗时 unload_window 永远不会被调用, 窗口资源句柄泄漏。
 *     (IR 里对 window+88 只有 window_show 那一次 store null, 见 window.ll:65。)
 *     修法: window_show 里补 `window->info = info;`。
 *
 *  2. __err1 在 "malloc 失败" 这条路径上 info 仍是 NULL, 却照样调
 *     unload_window(&info->type) —— 即 unload_window(NULL)。是否崩取决于
 *     平台实现是否判空。
 *     修法: __err1 里先判 `if (info)`。
 *
 *  3. window->hide 在 window_show 里没有初始化(只清了 busy), 用的是
 *     ui_core_malloc 返回的未初始化内存。若恰好非 0, 第一次按键/触摸结束后
 *     就会立刻把窗口关掉。
 *     修法: window_show 里补 `window->hide = 0;`。
 */
