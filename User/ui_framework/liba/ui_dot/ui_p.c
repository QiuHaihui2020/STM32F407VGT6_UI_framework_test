/*
 * ui_p.c —— 通用 text element 基础控件
 *
 * 【结构体布局】struct element_text 的字段偏移见 ui/p.h
 *   (elm=0 str=72 format=76 priv=80 color=84 handler=88, sizeof=92)——
 *   它是控件基类, 前面必须是 struct element, 不要往前面插字段。
 *
 * 【导出符号】其它模块(ui_number / ui_time)实际用到的是前三个:
 *   text_element_init / text_element_set_event_handler / text_element_set_text。
 *   text_element_show 目前没有调用者, 一并提供以保持接口完整。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_p.data.bss")
#pragma data_seg(".ui_p.data")
#pragma const_seg(".ui_p.text.const")
#pragma code_seg(".ui_p.text")
#endif

#include "ui/p.h"

/*
 * @note 三个回调入口都要【先判 handler 本身、再判具体的回调指针】: 控件若
 *       没调过 text_element_set_event_handler, handler 就是 NULL。
 */
static int text_onchange(void *_elm, enum element_change_event e, void *arg)
{
    struct element_text *text = (struct element_text *)_elm;
    struct ui_text_attrs text_attrs = {0};

    if (text->handler && text->handler->onchange) {
        text->handler->onchange(text->priv, e, arg);
    }

    if (e == ON_CHANGE_SHOW_POST) {
        text_attrs.str    = text->str;
        text_attrs.format = text->format;
        text_attrs.color  = text->color;
        platform_api->show_text((struct draw_context *)arg, &text_attrs);
    }

    return true;
}

static int text_onkey(void *_elm, struct element_key_event *e)
{
    struct element_text *text = (struct element_text *)_elm;

    /* handler 本身也要判: 控件若没调过 text_element_set_event_handler,
     * 它就是 NULL, 一收到按键就是空指针解引用。 */
    if (text->handler && text->handler->onkey) {
        if (text->handler->onkey(text->priv, e)) {
            return true;
        }
    }

    return false;
}

static int text_ontouch(void *_elm, struct element_touch_event *e)
{
    struct element_text *text = (struct element_text *)_elm;

    /* 同 text_onkey: handler 本身也要判。 */
    if (text->handler && text->handler->ontouch) {
        if (text->handler->ontouch(text->priv, e)) {
            return true;
        }
    }

    return false;
}

static const struct element_event_handler text_event_handler = {
    .id       = 0,
    .ontouch  = text_ontouch,
    .onkey    = text_onkey,
    .onchange = text_onchange,
};

void text_element_init(struct element_text *text, int id, u8 page, u8 prj,
                       struct element_css1 *css,
                       struct element_event_action *action)
{
    ui_core_element_init(&text->elm, id, page, prj, css,
                         &text_event_handler, action);
}

void text_element_set_event_handler(struct element_text *text, void *priv,
                                    const struct element_event_handler *handler)
{
    text->priv    = priv;
    text->handler = handler;
}

void text_element_show(struct element_text *text, char *str, const char *format)
{
    text->str    = str;
    text->format = format;
    ui_core_redraw(text);
}

void text_element_set_text(struct element_text *text, char *str,
                           const char *format, int color)
{
    text->str    = str;
    text->format = format;
    text->color  = color;
}

/*
 * 实现注意事项
 *
 *  1) 【三个回调入口都判两层】text_onchange / text_onkey / text_ontouch 都是
 *     先判 text->handler、再判具体的回调指针。控件未调用
 *     text_element_set_event_handler 时 handler 为 NULL, 少判一层就是空指针
 *     解引用。
 *
 *  2) 【text_onchange 里的 ON_CHANGE_SHOW_POST】文本是在这一步才真正画出去的
 *     (交给 platform_api->show_text), 所以 str / format / color 要在此之前
 *     设好。
 */
