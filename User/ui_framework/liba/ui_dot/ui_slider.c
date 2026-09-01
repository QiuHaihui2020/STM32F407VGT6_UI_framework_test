/*
 * ui_slider.c —— 滑动条控件
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_slider.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_slider.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_slider.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   slider_get_percent@28  slider_touch_slider_move@34  slider_ontouch@78
 *   slider_onkey@97  slider_child_onchange@146  new_ui_slider@300
 *   ui_slider_set_persent@376  ui_slider_set_persent_by_id@396
 *   ui_slider_enable@418
 *
 *   slider_onchange 在 DWARF 里未单独列出(与 slider_child_onchange 同段),
 *   其形态见下方函数注释。
 *
 *   element_event_handler_for_id(@466, 头文件 static inline) 在原库中未被内联
 *   (IR 中是 internal fastcc 独立函数, 属性含 inlinehint), 而本工程 clang 把它
 *   内联进了 new_ui_slider —— 这是与其它模块相同的已登记偏差。
 *   get_rect_cover(rect.h 的 static inline)两侧【都】保持为独立 fastcc 函数,
 *   不构成偏差。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
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
 * @note 百分比计算用 "加 99 再除 100" 实现四舍五入(向上取整):
 *       (persent * range + 99) / 100。child[2] 用这个公式, child[3] 不加 99
 *       (普通除法), 是原库的不对称写法, 照抄。
 *       for 循环写成 switch(0/2/3/4/default) 而非 for(i=0;i<4;i++) + if,
 *       因为原厂 IR 是一条 switch 指令, 写成 if 链对不上。
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
 *       减法用 sub + clamp(>0), 加法用 add + clamp(<100), 两侧不对称:
 *       减法判 sgt(有符号大于 0), 加法判 slt(有符号小于 100), 照抄。
 *       step 与 persent 都在 u8/i8 上做运算(IR 为 add/sub i8),
 *       若用 int 做加法会多出 trunc, 所以用 u8 局部变量。
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
             * @note step 要在 case 内部读, 不能提到 switch 之前 —— 原厂 IR 里
             *       两个 case 各自 load 一次 slider->step(offset 360), 提到外面
             *       只 load 一次, 与原厂对不上。且必须先读 step 再读 persent,
             *       顺序反了 load 次序也不一致。
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
 *       必须继续往下走释放内存。原库只判 ON_CHANGE_RELEASE(10), 不判
 *       RELEASE_PROBE(9), 与 ui_pic 不同。
 */
static int slider_onchange(void *_slider, enum element_change_event event, void *arg)
{
    struct ui_slider *slider = (struct ui_slider *)_slider;

    /* @note 与 ui_pic 等不同, 这里【不判】slider->handler 本身是否为 NULL */
    if (slider->handler->onchange) {
        if (slider->handler->onchange(slider, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    /* 只处理 RELEASE, 没有 switch —— 原库如此 */
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
 *       矩形裁剪: 先取 slider 绝对矩形, 再用 dc->draw 覆盖该局部变量
 *       (原库如此, IR 为 memcpy 覆盖), 然后调 get_rect_cover 算 dc->disp
 *       与该矩形的交集, 有交集则把交集写回 dc->draw。
 */
static int slider_child_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct element *elm = (struct element *)_elm;
    struct draw_context *dc = (struct draw_context *)arg;
    struct ui_slider *slider = (struct ui_slider *)elm->parent;

    int byte_offset = (int)((u8 *)elm - (u8 *)slider->child_elm);
    int index = elm - slider->child_elm;

    /*
     * @note text_attrs 的清零要放在算完 index 之后 —— 原厂 IR 里
     *       lifetime.start + memset 出现在下标计算之后, 放到函数开头会提前。
     */
    struct ui_text_attrs text_attrs = {0};

    switch (event) {
    case ON_CHANGE_SHOW_PROBE:
        switch (index) {
        /*
         * @note 乘法要写成 (width - css.width) * persent, 不能写成
         *       persent * (width - css.width) —— 原厂 IR 的 load 顺序是
         *       left, width, css.width, sub, persent, mul; 把 persent 写在
         *       前面会先 load persent, 与原厂对不上。
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
             * @note r = dc->draw 必须放在乘法之前 —— 原厂 IR 里 memcpy 出现在
             *       load width/persent 之前。放到后面顺序就对不上。
             */
            r = dc->draw;
            int width = dc->rect.width;
            /*
             * @note 判定要写成 "div == 0", 不能写成 (mul + 99) <= 198。
             *       原厂 IR 是 icmp ugt (mul+99), 198 —— 那正是 clang 把
             *       "mul / 100 == 0" 折叠成的无符号范围检查(等价于
             *       -99 <= mul <= 99), 并另外保留一条 sdiv 供后面取值。
             *       手写成 +99/<=198 的比较会得到 icmp slt(有符号), 且因为
             *       编译器由此推出 mul > 0, 后面的除法会变成 udiv 而非 sdiv。
             */
            int div43 = slider->persent * width / 100;
            /*
             * @note 要写成 if (div43) { ... } 把主体包起来, 不能写
             *       if (div43 == 0) break; —— 原厂 IR 的 true 分支是主体
             *       (icmp ugt ... 198 直接跳主体), 写成提前 break 会得到
             *       反向的 icmp ult ... 199, 分支极性对不上。
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
         * @note 原库这里判的是 byte_offset == 216(= 3 * 72), 而非
         *       index == 3。照抄以匹配 IR 的 icmp eq i32 sub.ptr.sub, 216。
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
     * id 必须【在循环之前】取出来 —— 循环体里的 load_widget_info 会把平台层那个
     * 唯一的 static ui_control_info 缓存整块覆盖, 循环结束后 info->head.id 读到的
     * 是最后一个子控件的 id。参考 IR 里这个 load 位于循环前导块(%v39), 供循环后的
     * element_event_handler_for_id 使用。详见 README 5.3.2。
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
         * ui_control_info 缓存整块覆盖。原来在循环末尾才读 child_head->len,
         * 通用子控件那条路径上读到的就是被覆盖后的值, head 会走错位置。
         * 参考 IR 里 len 的 load 紧跟在 load_widget_info 之后。详见 README 5.3.2。
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
             * case 顺序照抄原厂: SELECTED_PIC -> PERSENT_TEXT -> SLIDER_PIC。
             * UNSELECT_PIC(29) 在原厂【不做任何事】, 走 default。
             *
             * @note PERSENT_TEXT 那支的三个值取自【child_info 强转 slider_text_info】
             *       的 +4/+8/+12(min_value/max_value/text_color), 不是取自 css。
             *       与竖版(ui_slider_vert.c)一致, 只是水平版没有 ASSERT。
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
     * @note 必须同时判负 —— 原厂 IR 是 icmp ugt i32 %persent, 100(无符号),
     *       这正是 clang 把 "persent > 100 || persent < 0" 折叠成一条无符号
     *       比较的结果。只写 persent > 100 会生成 icmp sgt, 且传负值时会被
     *       当成合法值存进 char persent(如 -5 变成 251)。
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
