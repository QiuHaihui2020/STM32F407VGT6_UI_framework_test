#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_core_api.data.bss")
#pragma data_seg(".ui_core_api.data")
#pragma const_seg(".ui_core_api.text.const")
#pragma code_seg(".ui_core_api.text")
#endif

#include "ui/ui_core.h"
#include "jl_debug.h"    /* cpu_assert / config_asser: 本文件直接调用了它们 */

extern int window_show(int id);
extern int layer_show(int id);
extern int layout_show(int id);
extern int window_hide(int id);
extern int layer_hide(int id);
extern int layout_hide(int id);
extern int window_onkey(struct element_key_event *e);
extern int window_ontouch(struct element_touch_event *e);
extern void mem_var_free(void);
extern int ui_platform_init(u8 *lcd);
extern struct element *ui_core_get_first_child(void);
extern int ui_core_set_default_handler(int (*)(u8 *, struct element_touch_event *),
                                       int (*)(u8 *, struct element_key_event *),
                                       int (*)(u8 *, int, u8 *));

struct ui_handl {
    int count;
    int window;
    struct list_head entry;
};

struct ui_wait_call {
    struct list_head entry;
    int id;
    int (*func)(int);
};

struct uimsg_handl {
    u8 *name;
    int (*handler)(u8 *, int);
};

extern const int config_asser;
extern struct ui_platform_api *platform_api;

static struct ui_handl handl;

static struct element *__get_highlight_child(struct element *elm);
static struct ui_wait_call *__get_call_entry(void);
static void __do_wait_call(void);

struct element *ui_get_highlight_child(struct element *elm)
{
    return __get_highlight_child(elm);
}

static struct element *__get_highlight_child(struct element *elm)
{
    struct list_head *p;
    list_for_each(p, &elm->child) {
        struct element *e = (struct element *)((char *)p - offsetof(struct element, child));
        if (e->highlight) {
            return e;
        }
    }
    return NULL;
}

struct element *ui_get_highlight_child_by_id(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);

    if (config_asser) {
        if (elm) {
            return __get_highlight_child(elm);
        }
        int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
        printf("cpu %d file:%s, line:%d", cnum, "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 56);
        printf("ASSERT-FAILD: elm != NULL ");
        cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 56, 0, "elm != NULL");
        return NULL;
    }
    if (!elm) {
        cpu_assert(NULL, 56, 0, "elm != NULL");
        return NULL;
    }
    return __get_highlight_child(elm);
}

int ui_get_child_by_id(int id, int (*event_handler_cb)(u8 *, int, int))
{
    struct element *elm = ui_core_get_element_by_id(id);

    if (config_asser) {
        if (!elm) {
            int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
            printf("cpu %d file:%s, line:%d", cnum, "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 70);
            printf("ASSERT-FAILD: elm != NULL ");
            cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 70, 0, "elm != NULL");
            return -14;
        }
    } else {
        if (!elm) {
            cpu_assert(NULL, 70, 0, "elm != NULL");
            return -14;
        }
    }

    if (event_handler_cb) {
        u8 *p = (u8 *)elm;
        int elm_id = elm->id;
        int page = (elm_id >> 16) & 63;
        if (event_handler_cb(p, elm_id, page)) {
            return 0;
        }
    }

    struct list_head *node;
    for (node = elm->child.next; node != &elm->child; node = node->next) {
        struct element *e = (struct element *)((char *)node - offsetof(struct element, child));
        if (event_handler_cb) {
            int eid = e->id;
            int epage = (eid >> 16) & 63;
            if (event_handler_cb((u8 *)e, eid, epage)) {
                return 0;
            }
        }
    }
    return 0;
}

int ui_set_default_handler(int (*ontouch)(u8 *, struct element_touch_event *),
                           int (*onkey)(u8 *, struct element_key_event *),
                           int (*onchange)(u8 *, int, u8 *))
{
    return ui_core_set_default_handler(ontouch, onkey, onchange);
}

int ui_invert_element_by_id(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);

    if (config_asser) {
        if (!elm) {
            int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
            printf("cpu %d file:%s, line:%d", cnum, "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 103);
            printf("ASSERT-FAILD: elm != NULL ");
            cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 103, 0, "elm != NULL");
            return -22;
        }
    } else {
        if (!elm) {
            cpu_assert(NULL, 103, 0, "elm != NULL");
            return -22;
        }
    }

    struct draw_context dc;
    memset(&dc, 0, sizeof(dc));
    ui_core_get_draw_context(&dc, elm, NULL);
    int ret = ui_core_invert_rect(&dc);
    return ret;
}

int ui_no_highlight_element(struct element *elm)
{
    if (!elm) {
        return -22;
    }
    ui_core_highlight_element(elm, 0);
    ui_core_redraw((u8 *)elm);
    return 0;
}

int ui_no_highlight_element_by_id(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    return ui_no_highlight_element(elm);
}

int ui_highlight_element(struct element *elm)
{
    if (!elm) {
        return -22;
    }
    ui_core_highlight_element(elm, 1);
    ui_core_redraw((u8 *)elm);
    return 0;
}

int ui_highlight_element_by_id(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    return ui_highlight_element(elm);
}

int ui_highlight_sibling(struct element *elm, int direction)
{
    struct element *sibling = NULL;

    if (config_asser) {
        if (!elm) {
            int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
            printf("cpu %d file:%s, line:%d", cnum, "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 157);
            printf("ASSERT-FAILD: elm != NULL ");
            cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_api.c", 157, 0, "elm != NULL");
        }
    } else {
        if (!elm) {
            cpu_assert(NULL, 157, 0, "elm != NULL");
        }
    }

    switch (direction) {
    case 0:
        sibling = ui_core_get_up_element(elm);
        break;
    case 1:
        sibling = ui_core_get_down_element(elm);
        break;
    case 2:
        sibling = ui_core_get_left_element(elm);
        break;
    case 3:
        sibling = ui_core_get_right_element(elm);
        break;
    default:
        return -22;
    }

    if (!sibling) {
        return -22;
    }
    ui_highlight_element(sibling);
    return 0;
}

void control_hide(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    if (!elm) {
        return;
    }
    ui_core_hide((u8 *)elm);
}

int ui_redraw(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    if (!elm) {
        return -22;
    }
    struct element *parent = elm->parent;
    if (!parent) {
        return -22;
    }
    return ui_core_redraw((u8 *)parent);
}

int ui_show(int id)
{
    int err = 0;

    if (handl.count) {
        struct ui_wait_call *p = __get_call_entry();
        if (!p) {
            return -12;
        }
        p->id = id;
        p->func = ui_show;
        list_add_tail(&p->entry, &handl.entry);
        return 0;
    }

    handl.count = 1;
    /*
     * @note 必须【只取 6 位】。原厂 IR 是 lshr i32 id, 16 之后 trunc to i6,
     *       即只看 bits[21:16]; 同文件 ui_get_child_by_id 里原厂写的也是
     *       lshr 16 + and 63。
     *
     *       写成 switch ((u32)id >> 16) 是错的: 实测 id = 0x1420005 时
     *       >> 16 得 0x142(322), 原厂 322 & 63 = 2 走 case 2(window_show),
     *       而 switch(322) 会掉进 default 去查 element —— 窗口根本不会创建,
     *       界面全黑。
     */
    switch (((u32)id >> 16) & 0x3f) {
    case 2:
        if (handl.window != 0) {
            puts("Attention: ui_show(PAGE_xx) and ui_hide(PAGE_xx) must be called in pairs!!!");
        } else {
            err = window_show(id);
            if (!err) {
                handl.window++;
            } else {
                printf("ui show err!!  func= %s,line= %d\n", "ui_show", 315);
            }
        }
        break;
    case 4:
        err = layer_show(id);
        break;
    case 3:
        err = layout_show(id);
        break;
    default:
    {
        struct element *elm = ui_core_get_element_by_id(id);
        if (elm) {
            err = ui_core_redraw((u8 *)elm);
        } else {
            err = -22;
        }
    }
        break;
    }

    handl.count--;
    if (handl.count == 0) {
        __do_wait_call();
    }
    return err;
}

int ui_hide(int id)
{
    if (handl.count) {
        struct ui_wait_call *p = __get_call_entry();
        if (!p) {
            return -12;
        }
        p->id = id;
        p->func = ui_hide;
        list_add_tail(&p->entry, &handl.entry);
        return 0;
    }

    handl.count = 1;
    /*
     * @note 必须【只取 6 位】。原厂 IR 是 lshr i32 id, 16 之后 trunc to i6,
     *       即只看 bits[21:16]; 同文件 ui_get_child_by_id 里原厂写的也是
     *       lshr 16 + and 63。
     *
     *       写成 switch ((u32)id >> 16) 是错的: 实测 id = 0x1420005 时
     *       >> 16 得 0x142(322), 原厂 322 & 63 = 2 走 case 2(window_show),
     *       而 switch(322) 会掉进 default 去查 element —— 窗口根本不会创建,
     *       界面全黑。
     */
    switch (((u32)id >> 16) & 0x3f) {
    case 2:
        if (handl.window != 1) {
            puts("Attention: ui_show(PAGE_xx) and ui_hide(PAGE_xx) must be called in pairs!!!");
        } else {
            handl.window = 0;
            window_hide(id);
            mem_var_free();
        }
        break;
    case 4:
        layer_hide(id);
        break;
    case 3:
        layout_hide(id);
        break;
    default:
        control_hide(id);
        break;
    }

    handl.count--;
    if (handl.count == 0) {
        __do_wait_call();
    }
    return 0;
}

int ui_set_call(int (*func)(int), int param)
{
    if (!handl.count) {
        return 0;
    }
    struct ui_wait_call *p = __get_call_entry();
    if (!p) {
        return -12;
    }
    p->id = param;
    p->func = func;
    list_add_tail(&p->entry, &handl.entry);
    return 0;
}

int ui_get_current_window_id(void)
{
    struct element *elm = ui_core_get_first_child();
    if (!elm) {
        return -1;
    }
    return elm->id;
}

int ui_event_onkey(struct element_key_event *e)
{
    handl.count++;
    int ret = window_onkey(e);
    handl.count--;
    __do_wait_call();
    return ret;
}

int ui_event_ontouch(struct element_touch_event *e)
{
    handl.count++;
    int ret = window_ontouch(e);
    handl.count--;
    __do_wait_call();
    return ret;
}

void ui_ontouch_lock(u8 *_elm)
{
    struct element *elm = (struct element *)_elm;
    ui_core_ontouch_lock(elm);
}

void ui_ontouch_unlock(u8 *_elm)
{
    struct element *elm = (struct element *)_elm;
    ui_core_ontouch_unlock(elm);
}

int ui_register_msg_handler(int id, struct uimsg_handl *handler)
{
    struct element *elm = ui_core_get_element_by_id(id);
    if (!elm) {
        return -22;
    }
    *(struct uimsg_handl **)&elm[1].child.prev = handler;
    return 0;
}

int ui_get_disp_status_by_id(int id)
{
    return ui_core_get_disp_status_by_id(id);
}

void ui_remove_backcolor(struct element *elm)
{
    u64 *bf = (u64 *)((char *)&elm->css.height + sizeof(int));
    *bf |= 0xFFFFFFULL;
}

void ui_remove_backimage(struct element *elm)
{
    u64 *bf = (u64 *)((char *)&elm->css.height + sizeof(int));
    *bf &= ~(0xFFFFFFULL << 32);
}

void ui_remove_border(struct element *elm)
{
    u32 *bf = (u32 *)((char *)&elm->css.height + 3 * sizeof(int));
    *bf &= 0xFFFF;
}

int ui_set_style_file(struct ui_style *style)
{
    if (platform_api->load_style) {
        return platform_api->load_style(style);
    }
    return 0;
}

int ui_framework_init(u8 *lcd)
{
    handl.count = 0;
    handl.window = 0;
    handl.entry.next = &handl.entry;
    handl.entry.prev = &handl.entry;

    struct ui_wait_call *p = (struct ui_wait_call *)malloc(160);
    int i;
    for (i = 0; i < 10; i++) {
        p[i].func = NULL;
        list_add_tail(&p[i].entry, &handl.entry);
    }
    return ui_platform_init(lcd);
}

static struct ui_wait_call *__get_call_entry(void)
{
    struct ui_wait_call *p = NULL;
    struct list_head *node = handl.entry.next;

    if (node != &handl.entry) {
        p = (struct ui_wait_call *)node;
        if (p->func == NULL) {
            /*
             * @note 必须用 __list_del_entry(只做 __list_del 的两次 store),
             *       不能用 list_del —— 本 SDK 的 list_del 额外做了自环初始化
             *       (entry->next = entry->prev = entry), 会多两次 store。
             *       参考 IR 的 b3 里只有两个 store。
             */
            __list_del_entry(node);
            /* @note 原厂在这里确实又判了一次 p(恒真), 照抄以匹配 IR。 */
            if (p) {
                return p;
            }
        }
    }

    p = (struct ui_wait_call *)malloc(sizeof(struct ui_wait_call));
    if (!p) {
        return NULL;
    }
    return p;
}

static void __do_wait_call(void)
{
    struct ui_wait_call *p = (struct ui_wait_call *)handl.entry.next;

    while (&p->entry != &handl.entry) {
        struct ui_wait_call *n = (struct ui_wait_call *)p->entry.next;
        if (p->func) {
            int id = p->id;
            int (*func)(int) = p->func;
            p->func = NULL;
            func(id);
        }
        p = n;
    }
}
