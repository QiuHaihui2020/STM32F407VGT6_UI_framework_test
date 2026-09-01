/*
 * ui_p.c —— 通用 text element 基础控件
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_p.c.o 还原。
 *   该库交付的是 LLVM bitcode(非机器码)且保留完整调试信息，
 *   故本文件是按 IR + DWARF 还原，而非从反汇编推测。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_p.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_p.c
 *
 * 【还原依据】
 *   函数原始行号(DISubprogram): text_onchange@10 text_onkey@33 text_ontouch@47
 *                              text_element_init@68 text_element_set_event_handler@78
 *                              text_element_show@85 text_element_set_text@93
 *   本文件按此顺序排列，便于与参考 IR 逐函数对照。
 *   局部变量名取自 DILocalVariable，结构体字段偏移与 ui/p.h 逐字段吻合
 *   (elm=0 str=72 format=76 priv=80 color=84 handler=88, sizeof=92)。
 *
 * 【导出符号】其它模块(ui_number/ui_time)实际依赖前三个:
 *   text_element_init / text_element_set_event_handler / text_element_set_text
 *   text_element_show 库里有导出但无调用者, 为 1:1 保真一并实现。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_p.data.bss")
#pragma data_seg(".ui_p.data")
#pragma const_seg(".ui_p.text.const")
#pragma code_seg(".ui_p.text")
#endif

#include "ui/p.h"

/*
 * @note 此处先判 handler 再判 handler->onchange; 而下面 text_onkey/text_ontouch
 *       【未】判 handler 本身是否为 NULL —— 这是原库行为, 不是还原疏漏
 *       (参考 IR 中 onkey/ontouch 直接 load handler->onkey 无 null 检查)。
 *       等价还原优先, 加固已另行提交, 见文末"加固记录"。
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

    /* 加固: 原库只判了 handler->onkey 而【未判 handler 本身】(同文件的
     * text_onchange 是判了的)。控件若没调过 text_element_set_event_handler,
     * handler 为 NULL, 一收到按键就是空指针解引用。 */
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

    /* 加固: 同 text_onkey, 原库漏判 handler 本身。 */
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
 * 加固记录(原库缺陷已修, 见 README 第 8 节):
 *
 * [已修] text_onkey / text_ontouch 未判 text->handler == NULL —— 原库只判了
 *        handler->onkey / handler->ontouch, 而同文件的 text_onchange 是判了
 *        handler 本身的。控件若未调用 text_element_set_event_handler 就收到
 *        按键/触摸, 即空指针解引用。两处均已补上 handler 判空。
 *
 * 差异已登记在 cpu/br27/tools/ui_reimpl/accept/ui_p.txt 并锁定指纹:
 * 本模块 9 项里除这 2 个函数外, 其余仍与原库逐字节一致。
 */
