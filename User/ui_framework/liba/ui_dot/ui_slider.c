/*
 * ui_slider.c —— 滑动条控件
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
 *   struct ui_slider: elm=0 child_elm[4]=72 step=360 persent=361
 *                     left=362 width=364 min_value=366 max_value=368
 *                     text_color=370 info=372 text_info=376 handler=380
 *                     sizeof=384
 *   struct ui_slider_info: head=0 step=16 ctrl=20
 *   struct ui_ctrl_info_head: type=0 ctrl_num=1 css_num=2 len=3 page=4 id=8 css=12
 *   struct element_css1: align=0 invisible=1 z_order=2 left=4 top=8 width=12 height=16
 *   struct element_css: 位域=0(u8) left=4 top=8 width=12 height=16
 *                       background_color:24+alpha:8=20 background_image:24+image_quadrant:8=24
 *                       border(css_border=u32)=28
 *   struct element: 位域=0(u32) id=4 parent=8 sibling=12 child=20 focus=28
 *                   css=32 dc=64 handler=68, sizeof=72
 *   struct draw_context: rect=20 draw=36 need_draw=68 disp=84
 *   struct rect: left=0 top=4 width=8 height=12, sizeof=16
 *   struct slider_text_info: move=0(u8) min_value=4 max_value=8 text_color=12
 *   struct ui_text_attrs: str=0 format=4 color=8 strlen=12 offset=14
 *                         encode:2+endian:1+flags:5=16 displen=18, sizeof=20
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_slider.data.bss")
#pragma data_seg(".ui_slider.data")
#pragma const_seg(".ui_slider.text.const")
#pragma code_seg(".ui_slider.text")
#endif

#include "ui/ui_slider.h"
#include "ui/control.h"
#include "jl_ascii.h"

int slider_get_percent(struct ui_slider *slider)
{
    return slider->persent;
}

/*
 * @note 滑块位置用 "加 99 再除 100" 向上取整: (persent * range + 99) / 100。
 *       child[2](滑块图)用这个公式, child[3](百分比文本)用普通除法 ——
 *       滑块要顶到最右端, 文本不需要, 所以两者取整方式不同。
 *       循环写成 switch 分派而不是 for + if 链, 是为了让"哪一个子控件做什么"
 *       一眼可见。
 */
int slider_touch_slider_move(struct ui_slider *slider, struct element_touch_event *e)
{
    struct rect r;
    int sub;
    int div;

    ui_core_get_element_abs_rect(&slider->elm, &r);

    sub = e->pos.x - r.left;
    div = sub * 100 / r.width;
    div = (div > 0) ? div : 0;
    div = (div < 100) ? div : 100;

    if (slider->persent == div) {
        return 0;
    }

    slider->persent = div;

    int i;
    for (i = 0; i <= 4; i++) {
        switch (i) {
        case 4:
            ui_core_redraw(slider);
            return 1;
        case 2:
            slider->child_elm[2].css.left =
                slider->left +
                (div * (slider->width - slider->child_elm[2].css.width) + 99) / 100;
            break;
        case 3:
            if (slider->text_info->move) {
                slider->child_elm[3].css.left =
                    slider->left +
                    div * (slider->width - slider->child_elm[3].css.width) / 100;
            }
            break;
        default:
            break;
        }
    }

    return 1;
}

static int slider_ontouch(void *_slider, struct element_touch_event *e)
{
    struct ui_slider *slider = (struct ui_slider *)_slider;

    if (slider->handler->ontouch) {
        if (slider->handler->ontouch(slider, e)) {
            return 1;
        }
    }

    return 0;
}

/*
 * @note 按键码 37/38(左/上) 减 step, 39/40(右/下) 加 step。
 *       加减都在 u8 / s8 上做, 然后 clamp 到 [0, 100] —— persent 本身就是
 *       单字节, 用 int 运算再存回去反而要多一次截断。
 */
static int slider_onkey(void *_slider, struct element_key_event *e)
{
    struct ui_slider *slider = (struct ui_slider *)_slider;

    if (slider->handler->onkey) {
        if (slider->handler->onkey(slider, e)) {
            return 1;
        }
    }

    if (e->event < 2) {
        switch (e->value) {
        case 37:
        case 38: {
            /*
             * @note step 在 case 内部读: 两个分支各取一次, 互不影响 ——
             *       提到 switch 之前会让"加"和"减"共享一次读取, 将来若有
             *       分支要改 step 就容易出错。
             */
            u8 step = slider->step;
            s8 sub = (s8)(slider->persent - step);
            s8 clamped = (sub > 0) ? sub : 0;
            slider->persent = clamped;
            break;
        }
        case 39:
        case 40: {
            u8 step = slider->step;
            s8 add = (s8)(slider->persent + step);
            s8 clamped = (add < 100) ? add : 100;
            slider->persent = clamped;
            break;
        }
        default:
            return 0;
        }
    } else {
        return 0;
    }

    int i;
    for (i = 0; i < 4; i++) {
        if (slider->child_elm[i].handler &&
            slider->child_elm[i].handler->onchange) {
            slider->child_elm[i].handler->onchange(&slider->child_elm[i],
                                                   ON_CHANGE_SHOW_PROBE, NULL);
        }
    }

    ui_core_redraw(slider);
    return 1;
}

/*
 * @note 应用层 onchange 返回 true 时通常吃掉事件, 但 RELEASE 例外 ——
 *       必须继续往下走释放内存。本控件只在 RELEASE 时释放(不像 ui_pic 那样
 *       还要处理 RELEASE_PROBE), 因为子控件都是内嵌数组, 不需要单独摘链。
 */
static int slider_onchange(void *_slider, enum element_change_event event, void *arg)
{
    struct ui_slider *slider = (struct ui_slider *)_slider;

    /* @note 这里只判 onchange, 没再判 slider->handler 本身 ——
     *       new_ui_slider 已把它兜底成 dumy_handler。 */
    if (slider->handler->onchange) {
        if (slider->handler->onchange(slider, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    /* 只有 RELEASE 要处理, 所以不用 switch */
    if (event == ON_CHANGE_RELEASE) {
        ui_core_remove_element(slider);
        ui_core_free(slider);
    }

    return true;
}

/*
 * @note 子控件 onchange: 按 child index(0~3) 和 event 分派。
 *       child index 由 (_elm - &slider->child_elm[0]) / sizeof(element) 算出。
 *       event 4=SHOW_PROBE, 5=SHOW, 6=SHOW_POST。
 *
 *       child[0] (UNSELECT_PIC): SHOW 时若 persent==100 则跳过。
 *       child[1] (SELECTED_PIC): SHOW 时若 persent==0 则跳过。
 *       child[2] (SLIDER_PIC):   SHOW_PROBE 时按 persent 定位 left。
 *       child[3] (PERSENT_TEXT): SHOW_PROBE 时按 persent 定位 left(若 move),
 *                                SHOW_POST 时用 ASCII_IntToStr 输出数值文本。
 *
 *       矩形裁剪: 先取 slider 的绝对矩形(为了拿到 width), 再整体换成
 *       dc->draw, 然后求它与 dc->disp 的交集写回 dc->draw。
 */
static int slider_child_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct element *elm = (struct element *)_elm;
    struct draw_context *dc = (struct draw_context *)arg;
    struct ui_slider *slider = (struct ui_slider *)elm->parent;

    int byte_offset = (int)((u8 *)elm - (u8 *)slider->child_elm);
    int index = elm - slider->child_elm;

    /*
     * @note text_attrs 只有 SHOW_POST 那一支用得到, 所以放在下标算完之后
     *       再清零, 别的分支不必付这份开销。
     */
    struct ui_text_attrs text_attrs = {0};

    switch (event) {
    case ON_CHANGE_SHOW_PROBE:
        switch (index) {
        /*
         * @note 先算 (width - css.width) 再乘 persent —— 滑块能走的范围是
         *       "轨道宽减去滑块自身宽", 这样写与这个含义一致。
         */
        case 2:
            elm->css.left = slider->left +
                ((slider->width - elm->css.width) * slider->persent + 99) / 100;
            break;
        case 3:
            if (slider->text_info->move) {
                elm->css.left = slider->left +
                    (slider->width - elm->css.width) * slider->persent / 100;
            }
            break;
        default:
            break;
        }
        break;

    case ON_CHANGE_SHOW:
        switch (index) {
        case 1: {
            if (slider->persent == 0) {
                break;
            }
            struct rect r;
            struct rect c;
            ui_core_get_element_abs_rect(&slider->elm, &r);
            /*
             * @note 先把 r 换成 dc->draw, 再往下算 —— 上面那次
             *       ui_core_get_element_abs_rect 只是为了确保 r 有值。
             */
            r = dc->draw;
            int width = dc->rect.width;
            /*
             * @note 判定就写成 div43 是否为 0, 不要手工展开成
             *       "(persent * width + 99) <= 198" 之类的等价式 ——
             *       那种写法既难读, 又会让编译器对符号性做出不同推断。
             */
            int div43 = slider->persent * width / 100;
            /*
             * @note 用 if (div43) 把主体包起来, 而不是 "div43 == 0 就 break"
             *       —— 后面还要在这个 case 里做别的收尾时不至于漏掉。
             */
            if (div43) {
                int add50 = dc->rect.left + div43;
                if (add50 < r.left) {
                    break;
                }
                if (r.width + r.left > add50) {
                    r.width = add50 - r.left;
                }
                if (get_rect_cover(&dc->disp, &r, &c)) {
                    dc->draw = c;
                }
            }
            break;
        }
        case 0: {
            if (slider->persent == 100) {
                break;
            }
            struct rect r84;
            struct rect c85;
            ui_core_get_element_abs_rect(&slider->elm, &r84);
            int width = dc->rect.width;
            int div100 = slider->persent * width / 100;
            int add104 = dc->rect.left + div100;
            if (dc->draw.left + dc->draw.width < add104) {
                break;
            }
            r84 = dc->draw;
            if (r84.left < add104) {
                r84.left = add104;
            }
            int sub124 = width - div100;
            if (r84.width > sub124) {
                r84.width = sub124;
            }
            if (get_rect_cover(&dc->disp, &r84, &c85)) {
                dc->draw = c85;
            }
            break;
        }
        default:
            break;
        }
        break;

    case ON_CHANGE_SHOW_POST:
        /*
         * @note 这里判的是字节偏移 216(= 3 * sizeof(struct element)),
         *       等价于 index == 3(百分比文本)。
         */
        if (byte_offset != 216) {
            break;
        }
        {
            char text[16];
            int value = slider->min_value +
                (slider->max_value - slider->min_value) * slider->persent / 100;
            ASCII_IntToStr(text, value, 0, 16);
            text_attrs.str = text;
            text_attrs.format = "ascii";
            text_attrs.color = slider->text_color;
            platform_api->show_text(dc, &text_attrs);
        }
        break;

    default:
        break;
    }

    return 1;
}

static const struct element_event_handler slider_event_handler = {
    .id       = 0,
    .ontouch  = slider_ontouch,
    .onkey    = slider_onkey,
    .onchange = slider_onchange,
};

static const struct element_event_handler slider_child_event_handler = {
    .id       = 0,
    .ontouch  = NULL,
    .onkey    = NULL,
    .onchange = slider_child_onchange,
};

/*
 * @note 子控件遍历: 循环变量是 head(ui_ctrl_info_head*), 每次
 *       加 child_head->len 步进。child index 由 head->type - 29 算出
 *       (29=UNSELECT, 30=SELECTED, 31=SLIDER, 32=PERSENT_TEXT)。
 *       type 不在 [29,32] 范围内的子控件走 control_ops 工厂创建(通用控件)。
 *       type 在 [29,32) 时用 child_head->type 作为 switch 分支(不是 type-29)。
 */
static void *new_ui_slider(const void *_info, struct element *parent)
{
    struct ui_slider_info *info;
    struct ui_slider *slider;
    struct element_css1 *css;
    struct ui_ctrl_info_head *head;
    int i;
    int ctrl_num;
    int id;

    info = platform_api->load_widget_info((void *)_info, 0xff);

    slider = ui_core_malloc(sizeof(struct ui_slider));
    if (!slider) {
        return NULL;
    }

    slider->info = _info;
    slider->step = info->step;
    slider->persent = 0;

    css = platform_api->load_css(info->head.page, info->head.css);

    ui_core_element_init(&slider->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &slider_event_handler, NULL);
    ui_core_element_append_child(parent, &slider->elm);

    head = info->ctrl;
    ctrl_num = info->head.ctrl_num;
    /*
     * id 必须【在循环之前】取出来 —— 循环体里的 load_widget_info 会把平台层
     * 那个唯一的 static ui_control_info 缓存整块覆盖, 循环结束后再读
     * info->head.id 拿到的是最后一个子控件的 id。
     */
    id = info->head.id;

    for (i = 0; i < ctrl_num; i++) {
        struct ui_slider_info *child_info;
        struct ui_ctrl_info_head *child_head;
        u8 type;
        u8 len;

        child_info = platform_api->load_widget_info(head, 0xff);
        child_head = &child_info->head;
        /*
         * len 与 type 必须在这里(紧跟 load_widget_info)就取出来 —— 本轮后面的
         * ops->new() 会递归调 load_widget_info, 把平台层那个唯一的 static
         * ui_control_info 缓存整块覆盖。若等到循环末尾才读 child_head->len,
         * 通用子控件那条路径上读到的就是被覆盖后的值, head 会走错位置。
         */
        len  = child_head->len;
        type = child_head->type;

        if (type >= SLIDER_CHILD_BEGIN && type < SLIDER_CHILD_END) {
            int sub = type - SLIDER_CHILD_BEGIN;
            struct element_css1 *child_css;

            child_css = platform_api->load_css(info->head.page, child_head->css);

            ui_core_element_init(&slider->child_elm[sub], child_head->id,
                                 child_head->page,
                                 (u8)((u32)child_head->css >> 29),
                                 child_css, &slider_child_event_handler, NULL);
            ui_core_element_append_child(&slider->elm, &slider->child_elm[sub]);

            /*
             * 这里只有三种子控件要额外记参数; UNSELECT_PIC 不需要, 走 default。
             *
             * @note PERSENT_TEXT 那支的三个值取自【child_info 强转
             *       slider_text_info】后的 min_value / max_value / text_color,
             *       不是取自 css —— 资源里这三个是控件私有参数。
             */
            switch (child_head->type) {
            case SLIDER_CHILD_SELECTED_PIC:
                slider->left = child_css->left;
                slider->width = child_css->width;
                break;
            case SLIDER_CHILD_PERSENT_TEXT:
                slider->min_value =
                    ((struct slider_text_info *)child_info)->min_value;
                slider->max_value =
                    ((struct slider_text_info *)child_info)->max_value;
                slider->text_color =
                    ((struct slider_text_info *)child_info)->text_color;
                break;
            case SLIDER_CHILD_SLIDER_PIC:
                slider->child_elm[sub].css.left = slider->left;
                break;
            default:
                break;
            }
        } else {
            const struct control_ops *ops = get_control_ops_by_type(type);
            if (ops) {
                ops->new(head, &slider->elm);
            }
        }

        head = (struct ui_ctrl_info_head *)((u8 *)head + len);
    }

    slider->handler = element_event_handler_for_id(id);
    if (!slider->handler) {
        slider->handler = &dumy_handler;
    }
    if (slider->handler->onchange) {
        slider->handler->onchange(slider, ON_CHANGE_INIT, NULL);
    }

    return slider;
}

int ui_slider_set_persent(struct ui_slider *slider, int persent)
{
    int i;

    /*
     * @note 【必须同时判负】只写 persent > 100 的话, 传负值会被当成合法值
     *       存进 char persent(如 -5 变成 251), 滑块直接跑到最右端。
     */
    if (persent > 100 || persent < 0) {
        return -EINVAL;
    }

    slider->persent = persent;

    for (i = 0; i < 4; i++) {
        if (slider->child_elm[i].handler &&
            slider->child_elm[i].handler->onchange) {
            slider->child_elm[i].handler->onchange(&slider->child_elm[i],
                                                   ON_CHANGE_SHOW_PROBE, NULL);
        }
    }

    return 0;
}

int ui_slider_set_persent_by_id(int id, int persent)
{
    struct ui_slider *slider = (struct ui_slider *)ui_core_get_element_by_id(id);

    if (!slider) {
        return -EINVAL;
    }

    if (ui_slider_set_persent(slider, persent)) {
        return -EINVAL;
    }

    if (slider->elm.css.invisible) {
        ui_core_show(slider, 0);
    } else {
        ui_core_redraw(slider);
    }

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_slider_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_SLIDER)
.new = new_ui_slider,
};
