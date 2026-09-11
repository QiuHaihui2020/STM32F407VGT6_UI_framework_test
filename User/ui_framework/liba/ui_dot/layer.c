/*
 * layer.c —— 图层(window 与 layout 之间的一层, 每个 layer 独占一个 draw_context)
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
 *   struct layer: elm=0 hide=72 inited=73 highlight=74 ctrl_num=75 css_num=76
 *                 css[2]=80 dc=88 layout=248 info=252 handler=256
 *                 sizeof=260 (由 layer_new 的 mul num, 260 印证)
 *   struct layer_info: head=0 format=16 action=20 layout=24
 *   struct ui_ctrl_info_head: type=0 ctrl_num=1 css_num=2 len=3 page=4 id=8 css=12
 *   struct draw_context 字段序: 0 ref, 1 alpha, 2 align, 3 data_format,
 *                              4 prj, 5 page, 6 buf_num
 *
 * 【"是否已展开"用 inited(+73)】ontouch/onkey/onchange/toggle/highlight 判的都是
 *   inited。hide 位(+72)在本模块里只在 layer_init 里被清零, 从未被读过。
 *
 * 【layer_highlight 没有出现在头文件里】它是非 static 的全局符号, 但
 *   include/ui/layer.h 里没有声明 —— 保持非 static, 让需要的地方能 extern
 *   到它; 真要用就把声明补进那个头。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".layer.data.bss")
#pragma data_seg(".layer.data")
#pragma const_seg(".layer.text.const")
#pragma code_seg(".layer.text")
#endif

#include "ui/layer.h"

static int layer_ontouch(void *_layer, struct element_touch_event *e)
{
    struct layer *layer = (struct layer *)_layer;

    if (layer->inited) {
        if (layer->handler && layer->handler->ontouch) {
            return layer->handler->ontouch(layer, e);
        }
    }

    return false;
}

static int layer_onkey(void *_layer, struct element_key_event *e)
{
    struct layer *layer = (struct layer *)_layer;

    if (layer->inited) {
        if (layer->handler && layer->handler->onkey) {
            return layer->handler->onkey(layer, e);
        }
    }

    return false;
}

/*
 * @note css_num > 1 时才有高亮态; 高亮取 css[1], 常态取 css[0]。
 *       两个 css 指针是在 layer_init 里一次性算好存下来的(css[1] = &css[0][1]),
 *       所以这里不需要再解析资源。
 *
 *       标 always_inline + 在 layer_onchange 里用 goto 汇到一处, 是为了让
 *       "有 handler"和"没 handler"两条路共用同一段高亮代码, 不重复展开。
 */
static inline __attribute__((always_inline))
void layer_do_highlight(struct layer *layer, void *arg)
{
    if (layer->css_num > 1) {
        ui_core_set_element_css(layer,
                                platform_api->load_css(layer->elm.page,
                                        (void *)layer->css[arg ? 1 : 0]));
    }
}

static int layer_onchange(void *_layer, enum element_change_event e, void *arg)
{
    struct layer *layer = (struct layer *)_layer;

    if (!layer->inited) {
        return false;
    }

    if (layer->handler && layer->handler->onchange) {
        if (layer->handler->onchange(layer, e, arg) == 0 &&
            e == ON_CHANGE_HIGHLIGHT) {
            goto do_highlight;
        }
    } else if (e == ON_CHANGE_HIGHLIGHT) {
        goto do_highlight;
    }

    return true;

do_highlight:
    layer_do_highlight(layer, arg);
    return true;
}

static const struct element_event_handler event_handler = {
    .id       = 0,
    .ontouch  = layer_ontouch,
    .onkey    = layer_onkey,
    .onchange = layer_onchange,
};

/*
 * init != 0: 完整初始化(建 element、查 handler、挂到 parent 下);
 * init == 0: 只做"展开 layout + 开 draw_context"这一段, 用于 __layer_show
 *            里的延迟初始化。
 *
 * @return 0 成功(或资源标了默认隐藏而提前返回); -ENOMEM layout_new 失败;
 *         其余为 ui_core_open_draw_context 的返回值。
 */
static int layer_init(struct layer *layer, struct layer_info *_info,
                      struct element *parent, u8 init)
{
    struct layer_info *info;
    struct element_css1 *css;
    u8 ctrl_num;
    u8 page;
    u8 format;

    info = platform_api->load_widget_info(&_info->head, 0xff);

    layer->info     = _info;
    layer->ctrl_num = info->head.ctrl_num;
    layer->css_num  = info->head.css_num;
    /* 常态/高亮两份 css 一次算好: 高亮态就是紧跟其后的那一个 */
    layer->css[0]   = (u32)info->head.css;
    layer->css[1]   = (u32)&((struct element_css1 *)info->head.css)[1];

    if (init) {
        layer->hide      = 0;
        layer->highlight = 0;
        layer->inited    = 0;

        layer->handler = element_event_handler_for_id(info->head.id);

        css = platform_api->load_css(info->head.page, (void *)layer->css[0]);

        /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
        ui_core_element_init(&layer->elm, info->head.id, info->head.page,
                             (u8)((u32)info->head.css >> 29),
                             css, &event_handler, info->action);
        ui_core_element_append_child(parent, &layer->elm);

        if (css->invisible) {
            return 0;
        }
    }

    /*
     * 这三个必须【在 layout_new 之前】就从 info 里取出来。
     *
     * platform_api->load_widget_info() 返回的是 ui_resources_manager.c 里那个
     * 唯一的 static union ui_control_info 的地址, 每次调用整块覆盖; 而
     * layout_new -> layout_init 里会反复调它。所以 layout_new 返回之后, info
     * 指向的内容已经不是本 layer 的了 —— 那时再读就是读到别人的控件信息。
     * layout_init 里有同一个坑, 见那边的说明。
     */
    ctrl_num = info->head.ctrl_num;
    page     = info->head.page;
    format   = info->format;

    if (ctrl_num) {
        layer->layout = layout_new(info->layout, ctrl_num, &layer->elm);
        if (!layer->layout) {
            return -ENOMEM;
        }
    }

    layer->inited         = 1;
    layer->dc.page        = page;
    layer->dc.data_format = format;
    layer->dc.buf_num     = 2;

    if (layer->highlight) {
        ui_core_highlight_element(&layer->elm, 1);
    }

    return ui_core_open_draw_context(&layer->dc, &layer->elm);
}

static void layer_release_probe(struct layer *layer)
{
    if (layer->inited) {
        layout_delete_probe(layer->layout, layer->ctrl_num);
    }
}

/*
 * @note ui_core_element_append_child(elm, NULL) 表示"清空子链表"——
 *       ui_core 对 NULL 子节点就是这个语义。layer_new 的失败路径也这么用。
 */
static void layer_release(struct layer *layer)
{
    if (layer->inited) {
        layer->inited = 0;
        layout_delete(layer->layout, layer->ctrl_num);
        ui_core_element_append_child(&layer->elm, NULL);
        ui_core_element_on_focus(&layer->elm, 0);
        ui_core_close_draw_context(&layer->dc);
    }
}

struct layer *layer_new(struct layer_info *info, int num, struct element *parent)
{
    struct layer *layer;
    int i;

    layer = ui_core_malloc(num * sizeof(struct layer));
    if (!layer) {
        return NULL;
    }

    for (i = 0; i < num; i++) {
        if (layer_init(&layer[i], &info[i], parent, 1)) {
            goto __err;
        }
    }

    return layer;

__err:
    ui_core_element_append_child(parent, NULL);
    ui_core_free(layer);

    return NULL;
}

void layer_delete_probe(struct layer *layer, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        layer_release_probe(&layer[i]);
    }
}

/*
 * @note 与 layout_delete 不同, 这里【没有】if (num) 的保护: num 为 0 时也会
 *       走到 ui_core_free —— layer 数组本身总是分配过的, 所以这样是对的。
 */
void layer_delete(struct layer *layer, int num)
{
    int i;

    for (i = 0; i < num; i++) {
        layer_release(&layer[i]);
    }

    ui_core_free(layer);
}

static int __layer_show(struct layer *layer)
{
    int ret;

    ret = layer_init(layer, layer->info, NULL, 0);
    if (ret) {
        return ret;
    }

    return ui_core_show(layer, 0);
}

static void __layer_hide(struct layer *layer)
{
    ui_core_hide(layer);
    layer_release_probe(layer);
    layer_release(layer);
}

int layer_show(int id)
{
    struct layer *layer = layer_for_id(id);

    if (!layer) {
        return -EINVAL;
    }

    return __layer_show(layer);
}

int layer_hide(int id)
{
    struct layer *layer = layer_for_id(id);

    if (!layer) {
        return -EINVAL;
    }

    __layer_hide(layer);

    return 0;
}

int layer_toggle(int id)
{
    struct layer *layer = layer_for_id(id);

    if (!layer) {
        return -EINVAL;
    }

    if (layer->inited) {
        __layer_hide(layer);
    } else {
        __layer_show(layer);
    }

    return layer->inited;
}

/*
 * @note 已展开时是"关 dc -> 切高亮 -> 重开 dc -> 重画"; 未展开时只把 highlight
 *       标记记下来, 由 layer_init 在展开时补做 ui_core_highlight_element。
 */
int layer_highlight(int id, int yes)
{
    struct layer *layer = layer_for_id(id);

    if (!layer) {
        return -EINVAL;
    }

    if (layer->inited) {
        ui_core_close_draw_context(&layer->dc);
        ui_core_highlight_element(&layer->elm, yes);
        ui_core_open_draw_context(&layer->dc, &layer->elm);
        ui_core_show(layer, 0);
    } else {
        layer->highlight = yes;
        __layer_show(layer);
    }

    return 0;
}
