#ifndef UI_WINDOW_H
#define UI_WINDOW_H


#include "ui/layer.h"
#include "ui/ui_core.h"
#include "ui/control.h"
#include "jl_list.h"





struct window {
    struct element elm; 	//must be first
    u8 busy;
    u8 hide;
    u8 ctrl_num;
    struct list_head entry;
    struct layer *layer;
    const struct window_info *info;
    const struct element_event_handler *handler;
    void *private_data;
};


extern const struct window_info *window_table;



/* REGISTER_WINDOW_EVENT_HANDLER 已随 REGISTER_UI_EVENT_HANDLER 一起删除,
 * 原因见 ui_core.h 里的说明。窗口的事件回调现在写进
 * UI_STYLE_HANDLERS_BEGIN/END 的数组里, 与其它控件一视同仁。 */


int window_show(int);

int window_hide(int id);

int window_toggle(int id);

int window_ontouch(struct element_touch_event *e);

int window_onkey(struct element_key_event *e);


#endif


