/*
 * ui_slider_vert.c —— 垂直滑动条控件
 *
 * 【与水平版 ui_slider.c 的关系】两者结构对称, 差异集中在: 方向(top/height
 *   而不是 left/width)、进度反向(用 100 - persent)、以及本文件多两条越界告警。
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
 *   struct ui_vslider: elm=0 child_elm[4]=72 step=360 persent=361
 *                     top=362 height=364 min_value=366 max_value=368
 *                     text_color=370 info=372 text_info=376 handler=380
 *                     sizeof=384
 *   struct ui_slider_info: head=0 step=16 ctrl=20 (与水平版共用)
 *   struct ui_ctrl_info_head: type=0 ctrl_num=1 css_num=2 len=3 page=4 id=8 css=12
 *   struct element_css1: align=0 invisible=1 z_order=2 left=4 top=8 width=12 height=16
 *   struct element_css: 位域=0(u8) left=4 top=8 width=12 height=16
 *                       background_color:24+alpha:8=20 background_image:24+image_quadrant:8=24
 *                       border(css_border=u32)=28
 *   struct element: 位域=0(u32) id=4 parent=8 sibling=12 child=20 focus=28
 *                   css=32 dc=64 handler=68, sizeof=72
 *   struct draw_context: rect=20 draw=36 need_draw=68 disp=84
 *   struct rect: left=0 top=4 width=8 height=12, sizeof=16
 *   struct vslider_text_info: move=0(u8) min_value=4 max_value=8 text_color=12
 *   struct ui_text_attrs: str=0 format=4 color=8 strlen=12 offset=14
 *                         encode:2+endian:1+flags:5=16 displen=18, sizeof=20
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_slider_vert.data.bss")
#pragma data_seg(".ui_slider_vert.data")
#pragma const_seg(".ui_slider_vert.text.const")
#pragma code_seg(".ui_slider_vert.text")
#endif

#include "ui/ui_slider_vert.h"
#include "ui/control.h"
#include "jl_ascii.h"
#include "jl_debug.h"    /* ASSERT / log_*: 显式包含, 保证本文件自包含 */

int vslider_get_percent(struct ui_vslider *vslider)
{
    return vslider->persent;
}

/*
 * @note 滑块位置用 "加 99 再除 100" 向上取整: (persent * range + 99) / 100。
 *       child[2](滑块图)用这个公式, child[3](百分比文本)用普通除法 ——
 *       滑块要顶到端点, 文本不需要。
 *       循环写成 switch 分派而不是 for + if 链, 是为了让"哪一个子控件做什么"
 *       一眼可见。
 */
int vslider_touch_slider_move(struct ui_vslider *vslider, struct element_touch_event *e)
{
    struct rect r;
    int sub;
    int div;

    ui_core_get_element_abs_rect(&vslider->elm, &r);

    sub = e->pos.y - r.top;
    div = sub * 100 / r.height;
    /*
     * @note 垂直滑块要反向 —— 屏幕 Y 轴向下增长, 触点越靠上百分比越大。
     *       水平版(ui_slider.c)没有这一步。
     */
    div = 100 - div;
    div = (div > 0) ? div : 0;
    div = (div < 100) ? div : 100;

    if (vslider->persent == div) {
        return 0;
    }

    vslider->persent = div;

    int i;
    for (i = 0; i <= 4; i++) {
        switch (i) {
        case 4:
            ui_core_redraw(vslider);
            return 1;
        case 2:
            vslider->child_elm[2].css.top =
                vslider->top +
                (div * (vslider->height - vslider->child_elm[2].css.height) + 99) / 100;
            break;
        case 3:
            if (vslider->text_info->move) {
                vslider->child_elm[3].css.top =
                    vslider->top +
                    div * (vslider->height - vslider->child_elm[3].css.height) / 100;
            }
            break;
        default:
            break;
        }
    }

    return 1;
}

static int vslider_ontouch(void *_vslider, struct element_touch_event *e)
{
    struct ui_vslider *vslider = (struct ui_vslider *)_vslider;

    if (vslider->handler->ontouch) {
        if (vslider->handler->ontouch(vslider, e)) {
            return 1;
        }
    }

    return 0;
}

/*
 * @note 按键码 37/38(左/上) 减 step, 39/40(右/下) 加 step。
 *       减法用 sub + clamp(>0), 加法用 add + clamp(<100), 两侧不对称:
 *       加减都在 u8 / s8 上做, 然后 clamp 到 [0, 100] —— persent 本身就是
 *       单字节, 用 int 运算再存回去反而要多一次截断。
 */
static int vslider_onkey(void *_vslider, struct element_key_event *e)
{
    struct ui_vslider *vslider = (struct ui_vslider *)_vslider;

    if (vslider->handler->onkey) {
        if (vslider->handler->onkey(vslider, e)) {
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
            u8 step = vslider->step;
            s8 sub = (s8)(vslider->persent - step);
            s8 clamped = (sub > 0) ? sub : 0;
            vslider->persent = clamped;
            break;
        }
        case 39:
        case 40: {
            u8 step = vslider->step;
            s8 add = (s8)(vslider->persent + step);
            s8 clamped = (add < 100) ? add : 100;
            vslider->persent = clamped;
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
        if (vslider->child_elm[i].handler &&
            vslider->child_elm[i].handler->onchange) {
            vslider->child_elm[i].handler->onchange(&vslider->child_elm[i],
                                                   ON_CHANGE_SHOW_PROBE, NULL);
        }
    }

    ui_core_redraw(vslider);
    return 1;
}

/*
 * @note 应用层 onchange 返回 true 时通常吃掉事件, 但 RELEASE 例外 ——
 *       必须继续往下走释放内存。本控件只在 RELEASE 时释放(不像 ui_pic 那样
 *       还要处理 RELEASE_PROBE), 因为子控件都是内嵌数组, 不需要单独摘链。
 */
static int vslider_onchange(void *_vslider, enum element_change_event event, void *arg)
{
    struct ui_vslider *vslider = (struct ui_vslider *)_vslider;

    /* @note 与 ui_pic 等不同, 这里【不判】vslider->handler 本身是否为 NULL */
    if (vslider->handler->onchange) {
        if (vslider->handler->onchange(vslider, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    /* 只有 RELEASE 要处理, 所以不用 switch */
    if (event == ON_CHANGE_RELEASE) {
        ui_core_remove_element(vslider);
        ui_core_free(vslider);
    }

    return true;
}

/*
 * @note 子控件 onchange: 按 child index(0~3) 和 event 分派。
 *       child index 由 (_elm - &vslider->child_elm[0]) / sizeof(element) 算出。
 *       event 4=SHOW_PROBE, 5=SHOW, 6=SHOW_POST。
 *
 *       child[0] (UNSELECT_PIC): SHOW 时若 persent==0   则跳过(水平版是 ==100)。
 *       child[1] (SELECTED_PIC): SHOW 时若 persent==100 则跳过(水平版是 ==0)。
 *       child[2] (SLIDER_PIC):   SHOW_PROBE 时按 (100-persent) 定位 top。
 *       child[3] (PERSENT_TEXT): SHOW_PROBE 时按 (100-persent) 定位 top(若 move),
 *                                SHOW_POST 时用 ASCII_IntToStr 输出数值文本。
 *
 *       垂直方向一律用 (100 - persent) —— 见各处 @note。
 *
 *       矩形裁剪: 先取 vslider 绝对矩形, 再用 dc->draw 覆盖该局部变量
 *       然后求它与 dc->disp 的交集, 有交集则把交集写回 dc->draw。
 */
static int vslider_child_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct element *elm = (struct element *)_elm;
    struct draw_context *dc = (struct draw_context *)arg;
    struct ui_vslider *vslider = (struct ui_vslider *)elm->parent;

    int byte_offset = (int)((u8 *)elm - (u8 *)vslider->child_elm);
    int index = elm - vslider->child_elm;

    /*
     * @note text_attrs 只有 SHOW_POST 那一支用得到, 所以放在下标算完之后
     *       再清零, 别的分支不必付这份开销。
     */
    struct ui_text_attrs text_attrs = {0};

    switch (event) {
    case ON_CHANGE_SHOW_PROBE:
        switch (index) {
        /*
         * @note 先算 (height - css.height) 再乘 (100 - persent) —— 滑块能走的
         *       范围是"轨道高减去滑块自身高", 按这个含义来写更直观;
         *       垂直方向进度反向, 所以乘的是 100 - persent。
         */
        case 2:
            elm->css.top = vslider->top +
                ((vslider->height - elm->css.height) * (100 - vslider->persent) + 99) / 100;
            break;
        case 3:
            if (vslider->text_info->move) {
                elm->css.top = vslider->top +
                    (vslider->height - elm->css.height) * (100 - vslider->persent) / 100;
            }
            break;
        default:
            break;
        }
        break;

    case ON_CHANGE_SHOW:
        switch (index) {
        case 1: {
            /* 与水平版相反: 垂直版 case1 判 ==100 */
            if (vslider->persent == 100) {
                break;
            }
            struct rect r;
            struct rect c;
            ui_core_get_element_abs_rect(&vslider->elm, &r);
            /*
             * @note 这段是垂直版独有的健壮性检查, 水平版没有:
             *       子控件比滑条本体还高时打一条告警(只打印, 不修正)。
             */
            if (dc->rect.height > r.height) {
                puts("VSLIDER_CHILD_SELECTED_PIC is large than VSLIDER,Please check it!");
            }
            r = dc->draw;
            /* 垂直方向反向: 用 (100 - persent) */
            int div43 = (100 - vslider->persent) * dc->rect.height / 100;
            /*
             * @note r.top 先取到局部变量里复用 —— 下面比较、加、减都用它,
             *       中间 r.height 会被改写, 分散读容易读到改过的值。
             */
            int top = r.top;
            int add50 = div43 + dc->rect.top;
            if (add50 < top) {
                break;
            }
            if (r.height + top > add50) {
                r.height = add50 - top;
            }
            if (get_rect_cover(&dc->disp, &r, &c)) {
                dc->draw = c;
                /* @note 垂直版独有: 交集的高度要回写 disp.height, 否则下一条
                 *       带的起点会算错(水平方向不需要, 条带本来就是按行切的) */
                dc->disp.height = c.height;
            }
            break;
        }
        case 0: {
            /* 与水平版相反: 垂直版 case0 判 ==0 */
            if (vslider->persent == 0) {
                break;
            }
            struct rect r84;
            struct rect c85;
            ui_core_get_element_abs_rect(&vslider->elm, &r84);
            /*
             * @note 这条告警比的是 top(case 1 那条比的是 height), 对应的
             *       子控件也不同(UNSELECTED_PIC / SELECTED_PIC), 两条都要留。
             */
            if (dc->rect.top > r84.top) {
                puts("VSLIDER_CHILD_UNSELECTED_PIC is large than VSLIDER,Please check it!");
            }
            /*
             * @note 垂直方向是【反向】的: 用 (100 - persent) 而不是 persent。
             *       0% 在底部, 所以进度越大, 未选中区的起点越往上。
             */
            int height = dc->rect.height;
            int div100 = (100 - vslider->persent) * height / 100;
            int add104 = div100 + dc->rect.top;
            if (dc->draw.height + dc->draw.top < add104) {
                break;
            }
            r84 = dc->draw;
            if (r84.top < add104) {
                r84.top = add104;
            }
            int sub124 = height - div100;
            if (r84.height > sub124) {
                r84.height = sub124;
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
            int value = vslider->min_value +
                (vslider->max_value - vslider->min_value) * vslider->persent / 100;
            ASCII_IntToStr(text, value, 0, 16);
            text_attrs.str = text;
            text_attrs.format = "ascii";
            text_attrs.color = vslider->text_color;
            platform_api->show_text(dc, &text_attrs);
        }
        break;

    default:
        break;
    }

    return 1;
}

static const struct element_event_handler vslider_event_handler = {
    .id       = 0,
    .ontouch  = vslider_ontouch,
    .onkey    = vslider_onkey,
    .onchange = vslider_onchange,
};

static const struct element_event_handler vslider_child_event_handler = {
    .id       = 0,
    .ontouch  = NULL,
    .onkey    = NULL,
    .onchange = vslider_child_onchange,
};

/*
 * @note 子控件遍历: 循环变量是 head(ui_ctrl_info_head*), 每次
 *       加 child_head->len 步进。child index 由 head->type - 29 算出
 *       (29=UNSELECT, 30=SELECTED, 31=SLIDER, 32=PERSENT_TEXT)。
 *       type 不在 [29,32] 范围内的子控件走 control_ops 工厂创建(通用控件)。
 *       type 在 [29,32) 时用 child_head->type 作为 switch 分支(不是 type-29)。
 */
static void *new_ui_vslider(const void *_info, struct element *parent)
{
    struct ui_vslider_info *info;
    struct ui_vslider *vslider;
    struct element_css1 *css;
    struct ui_ctrl_info_head *head;
    int i;
    int ctrl_num;
    int id;

    info = platform_api->load_widget_info((void *)_info, 0xff);

    vslider = ui_core_malloc(sizeof(struct ui_vslider));
    if (!vslider) {
        return NULL;
    }

    vslider->info = _info;
    vslider->step = info->step;
    vslider->persent = 0;

    css = platform_api->load_css(info->head.page, info->head.css);

    ui_core_element_init(&vslider->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &vslider_event_handler, NULL);
    ui_core_element_append_child(parent, &vslider->elm);

    head = info->ctrl;
    ctrl_num = info->head.ctrl_num;
    /*
     * id 必须【在循环之前】取出来 —— 循环体里的 load_widget_info 会把平台层
     * 那个唯一的 static ui_control_info 缓存整块覆盖, 循环结束后再读
     * info->head.id 拿到的是最后一个子控件的 id。
     */
    id = info->head.id;

    for (i = 0; i < ctrl_num; i++) {
        /*
         * @note 变量名叫 _head 是因为下面的 ASSERT 会把条件表达式字符串化,
         *       打印出来就是 "((struct vslider_text_info *)_head)->min_value" ——
         *       改名会让断言输出跟着变。
         */
        struct ui_slider_info *_head;
        struct ui_ctrl_info_head *child_head;
        u8 type;
        u8 len;

        _head = platform_api->load_widget_info(head, 0xff);
        child_head = &_head->head;
        /*
         * len 与 type 必须在这里(紧跟 load_widget_info)就取出来 —— 本轮后面的
         * ops->new() 会递归调 load_widget_info, 把平台层那个唯一的 static
         * ui_control_info 缓存整块覆盖。若等到循环末尾才读 child_head->len,
         * 通用子控件那条路径上读到的就是被覆盖后的值, head 会走错位置。
         */
        len  = child_head->len;
        type = child_head->type;

        if (type >= VSLIDER_CHILD_BEGIN && type < VSLIDER_CHILD_END) {
            int sub = type - VSLIDER_CHILD_BEGIN;
            struct element_css1 *child_css;

            child_css = platform_api->load_css(info->head.page, child_head->css);

            ui_core_element_init(&vslider->child_elm[sub], child_head->id,
                                 child_head->page,
                                 (u8)((u32)child_head->css >> 29),
                                 child_css, &vslider_child_event_handler, NULL);
            ui_core_element_append_child(&vslider->elm, &vslider->child_elm[sub]);

            /*
             * @note 与水平版(ui_slider.c)不同的两处:
             *   1. top/height 取自 SELECTED_PIC 的 css(水平版取自 UNSELECT_PIC);
             *   2. min_value/max_value/text_color 取自把 _head 转成
             *      vslider_text_info* 后的字段(水平版取自 css 的 left/width/height),
             *      并且 min/max 各带一个 ASSERT(资源里必须填, 填 0 说明漏配)。
             */
            switch (child_head->type) {
            case VSLIDER_CHILD_SELECTED_PIC:
                vslider->top = child_css->top;
                vslider->height = child_css->height;
                break;
            case VSLIDER_CHILD_PERSENT_TEXT:
                ASSERT(((struct vslider_text_info *)_head)->min_value);
                vslider->min_value = ((struct vslider_text_info *)_head)->min_value;
                ASSERT(((struct vslider_text_info *)_head)->max_value);
                vslider->max_value = ((struct vslider_text_info *)_head)->max_value;
                vslider->text_color = ((struct vslider_text_info *)_head)->text_color;
                break;
            case VSLIDER_CHILD_SLIDER_PIC:
                vslider->child_elm[sub].css.top = vslider->top;
                break;
            default:
                break;
            }
        } else {
            const struct control_ops *ops = get_control_ops_by_type(type);
            if (ops) {
                ops->new(head, &vslider->elm);
            }
        }

        head = (struct ui_ctrl_info_head *)((u8 *)head + len);
    }

    vslider->handler = element_event_handler_for_id(id);
    if (!vslider->handler) {
        vslider->handler = &dumy_handler;
    }
    if (vslider->handler->onchange) {
        vslider->handler->onchange(vslider, ON_CHANGE_INIT, NULL);
    }

    return vslider;
}

int ui_vslider_set_persent(struct ui_vslider *vslider, int persent)
{
    int i;

    /*
     * @note 【必须同时判负】只写 persent > 100 的话, 传负值会被当成合法值
     *       存进 char persent(如 -5 变成 251), 滑块直接跑到另一端。
     */
    if (persent > 100 || persent < 0) {
        return -EINVAL;
    }

    vslider->persent = persent;

    for (i = 0; i < 4; i++) {
        if (vslider->child_elm[i].handler &&
            vslider->child_elm[i].handler->onchange) {
            vslider->child_elm[i].handler->onchange(&vslider->child_elm[i],
                                                   ON_CHANGE_SHOW_PROBE, NULL);
        }
    }

    return 0;
}

int ui_vslider_set_persent_by_id(int id, int persent)
{
    struct ui_vslider *vslider = (struct ui_vslider *)ui_core_get_element_by_id(id);

    if (!vslider) {
        return -EINVAL;
    }

    if (ui_vslider_set_persent(vslider, persent)) {
        return -EINVAL;
    }

    if (vslider->elm.css.invisible) {
        ui_core_show(vslider, 0);
    } else {
        ui_core_redraw(vslider);
    }

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_vslider_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_VSLIDER)
.new = new_ui_vslider,
};
