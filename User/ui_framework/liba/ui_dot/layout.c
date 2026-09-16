/*
 * layout.c —— 布局容器
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
 *   struct layout: elm=0 位域(hide:1,inited:1,release:6)=72 layout=76
 *                  info=80 handler=84, sizeof=88
 *   struct layout_info: head=0 action=16 ctrl=20
 *   位域测试速记: & 1 -> hide, & 2 -> inited, 整字节 > 3 -> release != 0
 *
 * 【一个容易看错的点】布局的"是否已展开"用的是 inited(bit1), 不是 hide(bit0)。
 *   layout_toggle / layout_ontouch / layout_onkey 判的都是 inited。
 *   hide 位在本模块里其实没被读过(只在 layout_init 里被清零)。
 *
 * 【handler 可能为 NULL】与各控件模块不同, 本模块【没有】dumy_handler 兜底 ——
 *   layout_init 里 element_event_handler_for_id 查不到就是 NULL, 所以每次调用
 *   前都要判两层(handler 与 handler->onchange)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".layout.data.bss")
#pragma data_seg(".layout.data")
#pragma const_seg(".layout.text.const")
#pragma code_seg(".layout.text")
#endif

#include "ui/layout.h"

void layout_lose_focus(struct layout *layout)
{
    ui_core_element_on_focus(&layout->elm, 0);
}

void layout_on_focus(struct layout *layout)
{
    ui_core_element_on_focus(&layout->elm, 1);
}

/*
 * 高亮切换那几行(查 widget_info -> 判 css_num -> set_element_css)在
 * layout_onchange 里【两处调用点直接展开】, 没有单独包一个 helper ——
 * 它只有两个调用点, 包起来编译器多半也会内联掉, 反而多一层跳转。
 */

static int layout_ontouch(void *_elm, struct element_touch_event *e)
{
    struct layout *layout = (struct layout *)_elm;

    if (layout->inited) {
        if (layout->handler && layout->handler->ontouch) {
            if (layout->handler->ontouch(layout, e)) {
                return true;
            }
        }
    }

    return false;
}

static int layout_onkey(void *_elm, struct element_key_event *e)
{
    struct layout *layout = (struct layout *)_elm;

    if (layout->inited) {
        if (layout->handler && layout->handler->onkey) {
            if (layout->handler->onkey(layout, e)) {
                return true;
            }
        }
    }

    return false;
}

/*
 * @note release 位非 0 表示这个 layout 是被工厂(__layout_new)动态创建的,
 *       此时 RELEASE_PROBE/RELEASE 要走"自己释放自己"的路径; 否则(静态数组
 *       创建的)由 layout_delete 统一释放。
 */
static int layout_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct layout *layout = (struct layout *)_elm;

    if (layout->release) {
        switch (event) {
        case ON_CHANGE_RELEASE_PROBE:
            ui_core_release_child_probe(&layout->elm);
            return false;
        case ON_CHANGE_RELEASE:
            if (layout->handler && layout->handler->onchange) {
                layout->handler->onchange(layout, ON_CHANGE_RELEASE, arg);
            }
            ui_core_remove_element(layout);
            ui_core_free(layout);
            return true;
        default:
            break;
        }
    }

    if (!layout->inited) {
        return false;
    }

    if (layout->handler && layout->handler->onchange) {
        if (layout->handler->onchange(layout, event, arg) == 0 &&
            event == ON_CHANGE_HIGHLIGHT) {
                struct layout_info *hi;

                hi = platform_api->load_widget_info((void *)layout->info, 0xff);
                if (hi->head.css_num > 1) {
                    ui_core_set_element_css(layout,
                        platform_api->load_css(layout->elm.page,
                                               &hi->head.css[arg ? 1 : 0]));
                }
            }
    } else if (event == ON_CHANGE_HIGHLIGHT) {
                struct layout_info *hi;

                hi = platform_api->load_widget_info((void *)layout->info, 0xff);
                if (hi->head.css_num > 1) {
                    ui_core_set_element_css(layout,
                        platform_api->load_css(layout->elm.page,
                                               &hi->head.css[arg ? 1 : 0]));
                }
            }

    return true;
}

static const struct element_event_handler event_handler = {
    .id       = 0,
    .ontouch  = layout_ontouch,
    .onkey    = layout_onkey,
    .onchange = layout_onchange,
};

/*
 * create != 0: 完整初始化(建 element、查 handler、挂到 parent 下);
 * create == 0: 只做"展开子控件"这一段, 用于 __layout_show 里的延迟初始化。
 *
 * @note css->invisible 为真时【提前返回】, 子控件不创建 —— 资源里标了默认隐藏
 *       的布局要等到 layout_show 时才真正展开, 这就是"弹出菜单"省 RAM 的机制。
 */
int layout_init(struct layout *layout, struct layout_info *info,
                struct element *parent, u8 create)
{
    struct layout_info *_info;
    struct element_css1 *css;
    struct ui_ctrl_info_head *head;
    int ctrl_num;
    int i;

    _info = platform_api->load_widget_info(&info->head, 0xff);
    layout->info = info;

    if (create) {
        layout->hide = 0;
        layout->inited = 0;

        layout->handler = element_event_handler_for_id(_info->head.id);

        css = platform_api->load_css(_info->head.page, _info->head.css);

        /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
        ui_core_element_init(&layout->elm, _info->head.id, _info->head.page,
                             (u8)((u32)_info->head.css >> 29),
                             css, &event_handler, _info->action);
        ui_core_element_append_child(parent, &layout->elm);

        if (css->invisible) {
            return 0;
        }
    }

    if (layout->handler && layout->handler->onchange) {
        layout->handler->onchange(layout, ON_CHANGE_INIT_PROBE, NULL);
    }

    head = (struct ui_ctrl_info_head *)_info->ctrl;

    /*
     * ctrl_num 必须【先取出来】, 绝对不能写成 i < _info->head.ctrl_num。
     *
     * platform_api->load_widget_info() 返回的是 ui_resources_manager.c 里
     * 那个【唯一的 static union ui_control_info】的地址, 每次调用都会把它整块
     * 覆盖。而下面循环体里既调了 load_widget_info(child), 又通过 ops->new()
     * 间接再调 —— 所以第 2 轮开始 _info 指向的内容已经不是本 layout 的了。
     *
     * 写成 i < _info->head.ctrl_num 的后果: 第 1 轮用的是真 ctrl_num, 建完
     * 第一个控件后缓存被覆盖, 第 2 轮读到的是那个子控件的 ctrl_num(通常 0),
     * 循环立刻结束 —— 界面上只剩第一个控件, 而且【不会有任何报错】。
     */
    ctrl_num = _info->head.ctrl_num;
    for (i = 0; i < ctrl_num; i++) {
        struct ui_ctrl_info_head *child;
        const struct control_ops *ops;
        int len;

        child = platform_api->load_widget_info(&head->type, 0xff);
        len = child->len;

        /* 控件类型 -> ops 走显式注册表(见 control.h), 不依赖链接器的段收集 */
        ops = get_control_ops_by_type(child->type);
        if (!ops) {
            puts("!!!!!unknow:ctrl_type");
            break;
        }

        ops->new(&head->type, &layout->elm);
        head = (struct ui_ctrl_info_head *)((u8 *)&head->type + len);
    }

    layout->inited = 1;

    if (layout->handler && layout->handler->onchange) {
        layout->handler->onchange(layout, ON_CHANGE_INIT, NULL);
    }

    return 0;
}

static void layout_release_probe(struct layout *layout)
{
    if (layout->inited) {
        ui_core_release_child_probe(&layout->elm);
    }
}

static void layout_release(struct layout *layout)
{
    if (layout->inited) {
        if (layout->handler && layout->handler->onchange) {
            layout->handler->onchange(layout, ON_CHANGE_RELEASE, NULL);
        }
        layout->inited = 0;
        ui_core_ontouch_lose_focus(&layout->elm);
        ui_core_element_on_focus(&layout->elm, 0);
        ui_core_release_child(&layout->elm);
    }
}

struct layout *layout_new(struct layout_info *info, int num,
                          struct element *parent)
{
    struct layout *layout;
    int i;

    layout = ui_core_malloc(num * sizeof(struct layout));
    if (!layout) {
        return NULL;
    }

    for (i = 0; i < num; i++) {
        layout[i].release = 0;
        layout_init(&layout[i], &info[i], parent, 1);
    }

    return layout;
}

/*
 * 控件工厂入口 —— 由 ui_core 在建控件树时调用。
 * @note layout_new 的返回值【必须判】, 见函数内说明。
 */
static void *__layout_new(const void *_info, struct element *parent)
{
    struct layout *layout;

    layout = layout_new((struct layout_info *)_info, 1, parent);
    /* 内存不足时 layout_new 返回 NULL, 不判的话紧接着写 layout->release
     * 就是空指针解引用。 */
    if (layout == NULL) {
        return NULL;
    }
    layout->release = 1;

    return layout;
}

void layout_delete_probe(struct layout *layout, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        layout_release_probe(&layout[i]);
    }
}

void layout_delete(struct layout *layout, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        layout_release(&layout[i]);
    }

    if (num) {
        ui_core_free(layout);
    }
}

static int __layout_show(struct layout *layout)
{
    if (!layout->inited) {
        layout_init(layout, layout->info, NULL, 0);
    }

    return ui_core_show(layout, 0);
}

static void __layout_hide(struct layout *layout)
{
    ui_core_hide(layout);
    layout_release_probe(layout);
    layout_release(layout);
}

int layout_show(int id)
{
    struct layout *layout = (struct layout *)ui_core_get_element_by_id(id);

    if (!layout) {
        return -EINVAL;
    }

    return __layout_show(layout);
}

int layout_hide(int id)
{
    struct layout *layout = (struct layout *)ui_core_get_element_by_id(id);

    if (!layout) {
        return -EINVAL;
    }

    __layout_hide(layout);

    return 0;
}

int layout_toggle(int id)
{
    struct layout *layout = (struct layout *)ui_core_get_element_by_id(id);

    if (!layout) {
        return -EINVAL;
    }

    if (!layout->inited) {
        __layout_show(layout);
    } else {
        __layout_hide(layout);
    }

    return layout->inited;
}

REGISTER_CONTROL_OPS(CTRL_TYPE_LAYOUT)
.new = __layout_new,
};

/*
 * 实现注意事项
 *
 *  1) 【__layout_new 要判 layout_new 的返回值】内存不足时它返回 NULL, 紧接着
 *     写 layout->release 就是空指针解引用。
 *
 *  2) 【ctrl_num 先取到局部变量】load_widget_info 返回的是一块公用缓存,
 *     循环里会被覆盖 —— 详见 layout_init 里的说明。这是本文件最容易踩的坑。
 *
 *  3) 【release 位的含义】非 0 表示这个 layout 是控件工厂(__layout_new)动态
 *     创建的, RELEASE_PROBE / RELEASE 要走"自己释放自己"; 静态数组创建的那些
 *     由 layout_delete 统一释放。
 *
 *  4) 【handler 可能为 NULL】本模块没有 dumy_handler 兜底, 所以每次调用前都
 *     要判两层(handler 与 handler->onchange)。
 */
