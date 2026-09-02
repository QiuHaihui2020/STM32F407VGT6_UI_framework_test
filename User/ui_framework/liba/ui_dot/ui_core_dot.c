/*
 * ui_core_dot.c —— 点阵屏 UI 核心(元素树 / draw_context / 绘制 / 事件分发)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_core_dot.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_core_dot.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_core_dot.c
 *
 * 【本模块是整个 UI 框架的基础】其余各控件模块都依赖它。原库 72 个独立函数,
 *   其中 6 个(in_rect / get_rect_cover / get_rect_nocover_l/r/t/b)来自已开源的
 *   interface/system/generic/rect.h, 只要 include 就有, 不需要重写。
 *
 * 【几个必须记住的结构约定】
 *   struct element(sizeof=72):
 *     +0  位域 {highlight:1, state:2, ref:5, prj:3, page:21}
 *     +4  id      +8  parent   +12 sibling(list_head)  +20 child(list_head)
 *     +28 focus   +32 css(element_css, 32 字节)  +64 dc   +68 handler
 *
 *   【父子链表的挂法】element.child 是【链表头】, element.sibling 是【链表节点】。
 *   所以遍历子元素时, 拿到的 list_head 指针要【减 12】才是 element 起始
 *   (参考 IR 里到处是 getelementptr i8, %p, i32 -12)。
 *
 *   struct element_css 首字节是位域 {align:2, invisible:1, z_order:5},
 *   所以 IR 里的 `lshr i8 x, 3` 是取 z_order, `lshr 2 & 1` 是取 invisible。
 *
 *   【css 的 left/top/width/height 是万分比】相对父元素, 换算成绝对像素要
 *   乘父矩形再除 10000 —— 见 ui_core_get_element_abs_rect(它是递归的)。
 *
 * 【.ui_ram 段】原库把 ui_core_get_element_abs_rect 与 ui_core_get_dc 放在
 *   .ui_ram(见 ref IR 的 section 属性), 用 rect.h 里的 AT_UI_RAM 宏标注。
 *   get_rect_cover 也在该段, 但它由 rect.h 提供。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_core_dot.data.bss")
#pragma data_seg(".ui_core_dot.data")
#pragma const_seg(".ui_core_dot.text.const")
#pragma code_seg(".ui_core_dot.text")
#endif

#include "ui/ui_core.h"
#include "jl_rect.h"
#include "jl_debug.h"    /* ASSERT / config_asser: 原厂靠别处间接带入 */
#include "ui_port_config.h"   /* UI_PORT_PUSH_TRACE: 上板排查开关 */

/* 应用层没注册 handler 时的兜底; ui_core_set_default_handler 往里填回调 */
struct element_event_handler dumy_handler;

struct ui_platform_api *platform_api;

/* 前置声明: 定义在本文件靠后(2489 行), 但 1909 行就调用了它。
 * 原厂靠 C89 的隐式声明糊过去, C99 起是告警 + 默认 int 返回类型。 */
int ui_core_redraw_old(void *_elm);


/*
 * 延后执行队列: ui_core_show 之类在"正在绘制"时被调用, 就把请求挂进 handl.entry
 * 排队, 等这一轮结束再统一执行(count 是嵌套深度)。
 * 节点是 ui_core_init 里一次 malloc 出来的 30 个连续元素(sizeof = 16)。
 */
struct ui_handl {
    int count;
    struct list_head entry;
};

struct ui_core_wait_call {
    struct list_head entry;
    struct element *elm;
    int (*func)(void *);
};

static struct element root;
static struct draw_context root_dc;
static struct element *touch_focus;
static struct element *lock;
static struct ui_handl handl;
static int rotate;

/*
 * 局部重绘的上下文(本模块私有, 头文件里没有):
 *   rect   要重绘的绝对矩形
 *   elm    发起重绘的元素 —— 遍历到它之后才开始检查"谁盖在它上面"
 *   begin  是否已经遍历到 elm
 *   redraw 结论: 0 完全被不透明兄弟盖住(不用画), 1 要整块重画, 2 部分重叠
 */
struct redraw_t {
    struct rect *rect;
    struct element *elm;
    int begin;
    int redraw;
};

static void ui_core_show_rect(struct element *elm, struct rect *rect);
static void ui_core_get_dc(struct element *elm);
static void __ui_core_show(struct element *elm, struct rect *rect);
static void __ui_core_show_invalid_rect(struct element *elm, struct rect *rect);
static void __ui_core_show_rect(struct element *elm, struct rect *rect);
static void ui_core_redraw_rect(struct element *elm, struct rect *r);
static void ui_core_redraw_rect_old(struct element *elm, struct rect *r);
static int __ui_core_show_old(struct element *elm);
static void __do_wait_call(void);

/* 各控件模块的注册入口, 由 ui_core_init 末尾逐个调用 */
extern void ui_text_enable(void);
extern void ui_pic_enable(void);
extern void ui_battery_enable(void);
extern void ui_time_enable(void);
extern void ui_grid_enable(void);
extern void ui_slider_enable(void);
extern void ui_vslider_enable(void);
extern void ui_number_enable(void);

/*
 * @note ASSERT 宏在这里【手工展开】: 宏体里的 __FILE__ / __LINE__ 会被编进
 *       字符串常量, 用本地路径编出来的常量与原厂对不上(原厂是 90 字符的
 *       /jks/workspace/... 绝对路径, 行号 127)。其余模块凡是带 ASSERT 的
 *       地方(ui_core_api.c 等)都是同样的处理。
 *
 * @note memset 只在 p 非空时执行 —— 原厂 if.then16 只从 "p != NULL" 那条边
 *       进来, 不是无条件 memset。
 */
void *ui_core_malloc(int size)
{
    void *p = platform_api->malloc(size);

    if (config_asser) {
        if (!p) {
            int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
            printf("cpu %d file:%s, line:%d", cnum,
                   "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_dot.c",
                   127);
            puts("ASSERT-FAILD: p != NULL ui_core_malloc");
            cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_dot.c",
                       127, 0, "p != NULL");
        }
    } else {
        if (!p) {
            cpu_assert(NULL, 127, 0, "p != NULL");
        }
    }

    if (p) {
        memset(p, 0, size);
    }

    return p;
}

void ui_core_free(void *buf)
{
    platform_api->free(buf);
}

void get_element_rect(struct element *elm, struct rect *r)
{
    r->left   = elm->css.left;
    r->top    = elm->css.top;
    r->width  = elm->css.width;
    r->height = elm->css.height;
}

/*
 * css 里的 left/top/width/height 是【万分比】(相对父元素的比例), 这里递归
 * 到根节点再一层层乘回去, 得到绝对像素矩形。
 *
 * @note 递归而不是循环 —— 参考 IR 里 if.end 分支就是对 elm->parent 的自调用。
 */
AT_UI_RAM
void ui_core_get_element_abs_rect(struct element *elm, struct rect *rect)
{
    if (!elm->parent) {
        get_element_rect(elm, rect);
        return;
    }

    ui_core_get_element_abs_rect(elm->parent, rect);

    rect->left   = elm->css.left * rect->width / 10000 + rect->left;
    rect->top    = elm->css.top * rect->height / 10000 + rect->top;
    rect->width  = elm->css.width * rect->width / 10000;
    rect->height = elm->css.height * rect->height / 10000;
}

/*
 * 把 child 挂到 parent 的子链表里, 按 css.z_order 【升序】插入 ——
 * 找到第一个 z_order 比自己大的兄弟就插它前面, 都不大就挂到尾部。
 * 这样后续绘制按链表顺序走就是从底到顶。
 *
 * @note child 为 NULL 表示【清空 parent 的子链表】(INIT_LIST_HEAD),
 *       layer_release / layer_new 的失败路径就是这么用的。
 */
void ui_core_element_append_child(struct element *parent, struct element *child)
{
    struct list_head *p;

    if (!child) {
        INIT_LIST_HEAD(&parent->child);
        return;
    }

    child->parent = parent;

    list_for_each(p, &parent->child) {
        struct element *e = (struct element *)((u8 *)p - 12);

        if (e->css.z_order > child->css.z_order) {
            __list_add(&child->sibling, e->sibling.prev, &e->sibling);
            return;
        }
    }

    list_add_tail(&child->sibling, &parent->child);
}

void ui_core_append_child(void *_child)
{
    ui_core_element_append_child(&root, (struct element *)_child);
}

struct element *ui_core_get_first_child()
{
    if (list_empty(&root.child)) {
        return NULL;
    }

    return (struct element *)((u8 *)root.child.next - 12);
}

void ui_core_remove_element(void *_child)
{
    struct element *child = (struct element *)_child;

    if (touch_focus == child) {
        touch_focus = NULL;
    }

    list_del(&child->sibling);
}

/* 原库是空函数(只有 ret), 照留 */
void ui_core_element_show(struct element *elm, int init)
{
}

/* 原库是空函数(只有 ret), 照留 */
void ui_core_ontouch_lose_focus(struct element *elm)
{
}

/* 原库直接 return 0, 淡入淡出在点阵屏上没实现 */
int ui_core_element_fadein(int id, int value)
{
    return 0;
}

int ui_core_element_fadeout(int id, int value)
{
    return 0;
}

void ui_core_set_rotate(int _rotate)
{
    rotate = _rotate;
}

int ui_core_get_rotate()
{
    return rotate;
}

int ui_core_set_default_handler(int (*ontouch)(void *, struct element_touch_event *),
                                int (*onkey)(void *, struct element_key_event *),
                                int (*onchange)(void *, enum element_change_event, void *))
{
    dumy_handler.ontouch  = ontouch;
    dumy_handler.onkey    = onkey;
    dumy_handler.onchange = onchange;

    return 0;
}

void ui_core_ontouch_lock(struct element *elm)
{
    lock = elm;
}

void ui_core_ontouch_unlock(struct element *elm)
{
    if (lock == elm) {
        lock = NULL;
    }
}

int ui_core_open_platform_device(struct draw_context *dc, void *device)
{
    return platform_api->open_device(dc, device);
}

/*
 * 按 id 在所有已登记的页面回调表里查 —— 表本体见 config/ui_port_registry.c
 * 的 g_ui_handler_table, 每个页面一张。
 *
 * 原厂是在链接器拼出来的那一整段 handler 里 p++ 单层遍历; 本移植改成
 * "先遍历页面表, 再遍历表内条目"的双层, 语义一致(全表按 id 找第一个命中)。
 *
 * @note 表都很短(一页十来项), 线性查即可; 控件创建时查一次并把结果存进
 *       xxx->handler, 不在按键/绘制热路径上。
 */
const struct element_event_handler *element_event_handler_for_id(u32 id)
{
    const struct ui_handler_group *const *pp;
    const struct element_event_handler *p;

    for (pp = g_ui_handler_table; *pp != NULL; pp++) {
        for (p = (*pp)->begin; p < (*pp)->end; p++) {
            if (p->id == (int)id) {
                return p;
            }
        }
    }

    return NULL;
}

/*
 * 原厂在这里按名字从 .ui_style 段里选一套风格, 把它的 handler 表边界装进
 * elm_event_handler_begin/end。本移植去掉了"风格"这一层: 只有一套资源,
 * g_ui_handler_table 里所有页面的表全部生效, 不需要按名字选。
 *
 * 保留函数是因为 ui_resources_manager.c 会拿资源文件名调它。返回恒 0 ——
 * 顺带消掉了原来"资源文件名与 STYLE_NAME 对不上 -> 整屏静默无响应"那类 bug。
 */
int ui_core_set_style(const char *style)
{
    printf("ui style: %s\n", style ? style : "(null)");
    return 0;
}

/*
 * 在以 elm 为根的子树里按 id 深度优先查找。
 * @note 先比自己再递归子节点; 命中即返回。
 */
struct element *get_element_by_id(struct element *elm, u32 id)
{
    struct list_head *p;

    if (elm->id == id) {
        return elm;
    }

    list_for_each(p, &elm->child) {
        struct element *e = get_element_by_id((struct element *)((u8 *)p - 12), id);

        if (e) {
            return e;
        }
    }

    return NULL;
}

struct element *ui_core_get_element_by_id(u32 id)
{
    return get_element_by_id(&root, id);
}

/*
 * @return 1 可见, 0 不可见, -ENOENT(-14) 找不到该 id
 * @note 返回的是 !invisible, 所以 IR 里是 lshr 2 -> and 1 -> xor 1。
 */
int ui_core_get_disp_status_by_id(u32 id)
{
    struct element *elm = get_element_by_id(&root, id);

    if (!elm) {
        return -ENOENT;
    }

    return !elm->css.invisible;
}

/*
 * 开 draw_context 之前, 自底向上(先递归子节点, 再通知自己)给整棵子树发一次
 * ON_CHANGE_TRY_OPEN_DC, 让各控件有机会按需要调整 dc(例如 grid 改可视区)。
 *
 * @note 递归在【前】、通知自己在【后】—— 参考 IR 里 for.end(循环结束)之后
 *       才取 handler 调 onchange。
 * @note 传给 onchange 的 arg 是 dc 本身(IR 里是 &dc->ref, 即 dc 首字段地址)。
 */
static void __try_open_draw_context(struct draw_context *dc, struct element *elm)
{
    struct list_head *p;

    list_for_each(p, &elm->child) {
        __try_open_draw_context(dc, (struct element *)((u8 *)p - 12));
    }

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_TRY_OPEN_DC, (void *)dc);
    }
}

/*
 * @note 成功时才把 dc 挂到 elm->dc 上; 失败直接返回平台层的错误码。
 */
int ui_core_open_draw_context(struct draw_context *dc, struct element *elm)
{
    int err;

    dc->elm   = elm;
    dc->alpha = elm->css.alpha;

    __try_open_draw_context(dc, elm);

    ui_core_get_element_abs_rect(elm, &dc->rect);
    dc->draw = dc->rect;

    err = platform_api->open_draw_context(dc);
    if (err) {
        return err;
    }

    elm->dc = dc;

    return 0;
}

/* @note 只有双缓冲(buf_num == 2)且平台实现了 put 时才真的调 */
int ui_core_put_draw_context(struct draw_context *dc)
{
    if (dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            return platform_api->put_draw_context(dc);
        }
    }

    return 0;
}

int ui_core_close_draw_context(struct draw_context *dc)
{
    return platform_api->close_draw_context(dc);
}

int ui_core_invert_rect(struct draw_context *dc)
{
    return platform_api->invert_rect(dc, 0);
}

/*
 * 引用计数: element 的位域 ref 占 bits[3:7](5 位)。
 * @return 加过之后的引用数; elm 为空或【已经是 0】时返回 -EINVAL ——
 *         ref 为 0 表示这个 element 已经废弃, 不允许再被引用。
 */
static int ui_core_get_element(struct element *elm)
{
    if (!elm || elm->ref == 0) {
        return -EINVAL;
    }

    elm->ref++;

    return elm->ref;
}

/*
 * 引用计数减一; 减到 0 才真的释放 —— 清空子链表并给自己发 ON_CHANGE_RELEASE。
 * @note ref 是 5 位位域, 自减靠位域自然回绕(IR 里是 +31 再 &31)。
 */
static void ui_core_put_element(struct element *elm)
{
    elm->ref--;

    if (elm->ref) {
        return;
    }

    INIT_LIST_HEAD(&elm->child);

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_RELEASE, NULL);
    }
}

/*
 * 把资源里的 element_css1(打包格式)展开到 element 内嵌的 element_css(位域格式),
 * 并把 element 复位成"刚建好"的状态(ref = 1, 子链表与兄弟链表都自环)。
 *
 * @note 两处容易漏:
 *   1. z_order 为 0 时要改成 31(IR: css 首字节 < 8 时 or -8)。z_order 0 表示
 *      资源没填, 按"最上层"处理。
 *   2. image_quadrant 先按资源写一次, 最后又【清零】(IR 里对
 *      background_color 那个 i64 位域有两次 store, 最后一次把高 8 位清掉)。
 *
 * @note action 参数原库【没有使用】(IR 标了 readnone)。
 */
void ui_core_element_init(struct element *elm, u32 id, u8 page, u8 prj,
                          struct element_css1 *css,
                          const struct element_event_handler *handler,
                          const struct element_event_action *action)
{
    elm->parent = NULL;
    elm->dc     = NULL;
    elm->focus  = NULL;
    elm->id     = id;

    elm->highlight = 0;
    elm->state     = 0;
    elm->ref       = 1;
    elm->prj       = prj;
    elm->page      = page;

    elm->handler = handler;

    elm->css.align     = css->align;
    elm->css.invisible = css->invisible;
    elm->css.z_order   = css->z_order;
    elm->css.left      = css->left;
    elm->css.top       = css->top;
    elm->css.width     = css->width;
    elm->css.height    = css->height;

    elm->css.background_color = css->background_color;
    elm->css.alpha            = css->alpha;
    elm->css.background_image = css->background_image;
    elm->css.image_quadrant   = css->image_quadrant;

    elm->css.border.left   = css->border.left;
    elm->css.border.top    = css->border.top;
    elm->css.border.right  = css->border.right;
    elm->css.border.bottom = css->border.bottom;
    elm->css.border.color  = css->border.color;

    elm->css.image_quadrant = 0;

    if (elm->css.z_order == 0) {
        elm->css.z_order = 31;
    }

    INIT_LIST_HEAD(&elm->child);
    INIT_LIST_HEAD(&elm->sibling);
}

/*
 * 释放前的"预告"遍历: 深度优先给整棵子树发 ON_CHANGE_RELEASE_PROBE,
 * 让控件有机会先停定时器、放外部资源。
 *
 * @note 必须用 list_for_each_safe(先把 next 存下来再递归) —— 递归里可能会
 *       动链表。参考 IR 里 for.body 是先 load p->next 保存, 再处理 p。
 */
static void ui_core_element_release_probe(struct element *elm)
{
    struct list_head *p;
    struct list_head *n;

    list_for_each_safe(p, n, &elm->child) {
        ui_core_element_release_probe((struct element *)((u8 *)p - 12));
    }

    if (touch_focus == elm) {
        touch_focus = NULL;
    }

    ui_core_element_on_focus(elm, 0);

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_RELEASE_PROBE, NULL);
    }
}

static void ui_core_element_release(struct element *elm)
{
    struct list_head *p;
    struct list_head *n;

    list_for_each_safe(p, n, &elm->child) {
        ui_core_element_release((struct element *)((u8 *)p - 12));
    }

    ui_core_put_element(elm);
}

void ui_core_release_child_probe(struct element *elm)
{
    struct list_head *p;
    struct list_head *n;

    list_for_each_safe(p, n, &elm->child) {
        ui_core_element_release_probe((struct element *)((u8 *)p - 12));
    }
}

void ui_core_release_child(struct element *elm)
{
    struct list_head *p;
    struct list_head *n;

    list_for_each_safe(p, n, &elm->child) {
        ui_core_element_release((struct element *)((u8 *)p - 12));
    }

    INIT_LIST_HEAD(&elm->child);
}

/*
 * @note 30 个 wait_call 节点是【一次 malloc 出来的连续数组】(480 = 30 * 16),
 *       全部挂进空闲链表; 之后 ui_core_show 之类要延后执行时从链表里取。
 * @note 末尾逐个调各控件模块的 xxx_enable() —— 那是让控件工厂注册进
 *       .control_ops 的入口(见 README 6.6)。
 */
int ui_core_init(struct ui_platform_api *api, struct rect *rect)
{
    struct ui_core_wait_call *p;
    int i;

    handl.count = 0;
    INIT_LIST_HEAD(&handl.entry);

    p = malloc(30 * sizeof(struct ui_core_wait_call));

    for (i = 0; i < 30; i++, p++) {
        p->func = NULL;
        list_add_tail(&p->entry, &handl.entry);
    }

    platform_api = api;

    memset(&root, 0, sizeof(root));
    root.dc          = &root_dc;
    root.css.left    = rect->left;
    root.css.top     = rect->top;
    root.css.width   = rect->width;
    root.css.height  = rect->height;
    INIT_LIST_HEAD(&root.child);

    ui_text_enable();
    ui_pic_enable();
    ui_battery_enable();
    ui_time_enable();
    ui_grid_enable();
    ui_slider_enable();
    ui_vslider_enable();
    ui_number_enable();

    return 0;
}

/*
 * 焦点链: root.focus 是链头, 每个 element 的 focus 字段指向下一个 ——
 * 一条"当前焦点路径"的单链表。
 *
 * yes != 0: 若 elm 已在链里就先把它摘掉(prev->focus = elm->focus), 然后压到链头。
 * yes == 0: 从链里摘掉并把自己的 focus 清空。
 *
 * @note 取焦那一支里, 摘掉之后【还继续遍历】(IR 里 if.end 跳的是 for.inc,
 *       不是退出); 只有 elm 恰好就是链头(prev == NULL)时才 break。照抄。
 */
void ui_core_element_on_focus(struct element *elm, int yes)
{
    struct element *p;
    struct element *prev;

    if (yes) {
        for (p = root.focus, prev = NULL; p; prev = p, p = p->focus) {
            if (p == elm) {
                if (!prev) {
                    break;
                }
                prev->focus = elm->focus;
            }
        }

        elm->focus = root.focus;
        root.focus = elm;
        return;
    }

    for (p = root.focus, prev = NULL; p; prev = p, p = p->focus) {
        if (p == elm) {
            if (!prev) {
                root.focus = elm->focus;
            } else {
                prev->focus = elm->focus;
            }
            elm->focus = NULL;
            break;
        }
    }
}

/*
 * 递归把整棵子树的 highlight 位设成 yes, 每层都发一次 ON_CHANGE_HIGHLIGHT。
 * @return 状态没变化时返回 -1(不做任何事), 否则 0。
 * @note onchange 的 arg 是把 yes 当指针传(IR: inttoptr i32 %yes to i8*)。
 */
int ui_core_highlight_element(struct element *elm, int yes)
{
    struct list_head *p;

    if (elm->highlight == yes) {
        return -1;
    }

    elm->highlight = yes;

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_HIGHLIGHT, (void *)yes);
    }

    list_for_each(p, &elm->child) {
        ui_core_highlight_element((struct element *)((u8 *)p - 12), yes);
    }

    return 0;
}

/*
 * 换一套 css(高亮态/常态切换时用)。与 ui_core_element_init 里那段的区别:
 *   1. invisible 位【保持原值】—— 进来先存下来, 最后再写回(IR 开头的
 *      bf.clear = bf.load & 4 与末尾的 bf.set126 就是这件事)。
 *      也就是说"是否隐藏"由 ui_core_show/hide 管, 不跟着 css 走。
 *   2. 没有 image_quadrant 清零, 也没有 z_order == 0 -> 31 的兜底。
 */
struct element_css *ui_core_set_element_css(void *_elm, const struct element_css1 *css)
{
    struct element *elm = (struct element *)_elm;
    u8 invisible = elm->css.invisible;

    elm->css.align     = css->align;
    elm->css.invisible = css->invisible;
    elm->css.z_order   = css->z_order;
    elm->css.left      = css->left;
    elm->css.top       = css->top;
    elm->css.width     = css->width;
    elm->css.height    = css->height;

    elm->css.background_color = css->background_color;
    elm->css.alpha            = css->alpha;
    elm->css.background_image = css->background_image;
    elm->css.image_quadrant   = css->image_quadrant;

    elm->css.border.left   = css->border.left;
    elm->css.border.top    = css->border.top;
    elm->css.border.right  = css->border.right;
    elm->css.border.bottom = css->border.bottom;
    elm->css.border.color  = css->border.color;

    elm->css.invisible = invisible;

    return &elm->css;
}

/*
 * 锁住某个 layer 的显示缓冲(双缓冲时取用后端 buffer, 防止绘制过程被刷屏打断)。
 *
 * @note 元素自己没有 dc 时要【沿 parent 往上找】第一个有 dc 的祖先 ——
 *       dc 是挂在 layer 上的, 控件本身没有。找到根都没有就返回 -EINVAL。
 */
int ui_lock_layer(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    struct draw_context *dc;

    if (!elm) {
        return -EINVAL;
    }

    dc = elm->dc;

    if (!dc) {
        struct element *parent = elm->parent;

        while (!parent->dc) {
            parent = parent->parent;
            if (!parent) {
                return -EINVAL;
            }
        }

        dc = parent->dc;
    }

    if (dc->buf_num == 2) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(dc);
        }
    }

    return 0;
}

int ui_unlock_layer(int id)
{
    struct element *elm = ui_core_get_element_by_id(id);
    struct draw_context *dc;

    if (!elm) {
        return -EINVAL;
    }

    dc = elm->dc;

    if (!dc) {
        struct element *parent = elm->parent;

        while (!parent->dc) {
            parent = parent->parent;
            if (!parent) {
                return -EINVAL;
            }
        }

        dc = parent->dc;
    }

    if (dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(dc);
        }
    }

    return 0;
}

/*
 * 四向导航: 在【同一个 parent 的兄弟】里找方向上最近的那个。
 *
 * 算法(以"上"为例): 遍历兄弟, 拿它的下边沿 bottom = css.top + css.height 与
 * 自己的 css.top 比:
 *   bottom <= 自己的 top  -> 在自己【上方】, 记距离最小的那个(min)
 *   bottom >  自己的 top  -> 在自己【下方】, 记距离最大的那个(max)
 * 最后优先返回 min_elm(真正在上方的最近者), 没有才返回 max_elm ——
 * 也就是【绕回到最远的那一端】, 实现循环导航。
 *
 * @note 比较是【无符号】的(IR: icmp ugt), 且 min/max 初值就是未初始化的
 *       (IR 里 phi 的初值是 undef) —— 靠 min_elm/max_elm 为 NULL 来兜第一次。
 *       照抄, 不要"顺手"初始化成 0。
 */
struct element *ui_core_get_up_element(struct element *elm)
{
    struct list_head *p;
    struct element *min_elm = NULL;
    struct element *max_elm = NULL;
    u32 min;
    u32 max;

    list_for_each(p, &elm->parent->child) {
        struct element *e = (struct element *)((u8 *)p - 12);
        u32 edge;

        if (e == elm) {
            continue;
        }

        edge = e->css.height + e->css.top;

        if (edge > (u32)elm->css.top) {
            if (!max_elm || max < edge - elm->css.top) {
                max = edge - elm->css.top;
                max_elm = e;
            }
        } else {
            if (!min_elm || min > elm->css.top - edge) {
                min = elm->css.top - edge;
                min_elm = e;
            }
        }
    }

    return min_elm ? min_elm : max_elm;
}

/*
 * 取一个"临时" draw_context: 从 elm 所属 layer 的 dc 上把缓冲区/格式等一整套
 * 参数复制过来, 再把绘制区域设成 elm 的绝对矩形(或调用者给的 draw)。
 * 控件绘制自己时用它, 不会动 layer 的原 dc。
 *
 * @note elm 自己没有 dc 时沿 parent 往上找第一个有 dc 的祖先, 并把它【记回
 *       elm->dc】(缓存), 下次就不用再找。
 *
 * @note width 取的是找到的那个 pdc, 而 height/lines/col_align/row_align 取的是
 *       【elm->dc】(刚被赋值的那个) —— 原厂 IR 里 %8 与 %38 是两次不同的读,
 *       混用的。值相同, 但照抄以对齐 IR。
 */
int ui_core_get_draw_context(struct draw_context *dc, struct element *elm,
                             struct rect *draw)
{
    struct draw_context *pdc = elm->dc;

    if (!pdc) {
        struct element *parent = elm;

        do {
            parent = parent->parent;
        } while (!parent->dc);

        /* ASSERT 手工展开, 行号与路径照原厂(见 ui_core_malloc 的说明) */
        if (config_asser) {
            if (!parent) {
                int cnum = 0;   /* 原为 pi32 读 cnum(CPU 编号); Cortex-M4 单核恒 0 */
                printf("cpu %d file:%s, line:%d", cnum,
                       "/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_dot.c",
                       273);
                printf("ASSERT-FAILD: parent != NULL ");
                cpu_assert("/jks/workspace/manifest_dev_soundbox_export/btsdk/lib/utils/ui/ui_framework/ui_core_dot.c",
                           273, 0, "parent != NULL");
            }
        } else {
            if (!parent) {
                cpu_assert(NULL, 273, 0, "parent != NULL");
            }
        }

        elm->dc = parent->dc;
        pdc = parent->dc;
    }

    dc->handl            = pdc->handl;
    dc->alpha            = elm->css.alpha;
    dc->align            = elm->css.align;
    dc->background_color = elm->css.background_color;
    dc->data_format      = pdc->data_format;
    dc->page             = elm->page;
    dc->dc               = pdc->dc;
    dc->buf              = pdc->buf;
    dc->buf0             = pdc->buf0;
    dc->buf1             = pdc->buf1;
    dc->len              = pdc->len;
    dc->fbuf             = pdc->fbuf;
    dc->fbuf_len         = pdc->fbuf_len;
    dc->width            = pdc->width;
    dc->height           = elm->dc->height;
    dc->lines            = elm->dc->lines;
    dc->col_align        = elm->dc->col_align;
    dc->row_align        = elm->dc->row_align;

    /* @note disp / draw 都是【逐字段】拷(原厂 IR 是 4 次 load+store,
     *       不是结构体整体赋值/memcpy) */
    dc->disp.left   = pdc->disp.left;
    dc->disp.top    = pdc->disp.top;
    dc->disp.width  = pdc->disp.width;
    dc->disp.height = pdc->disp.height;

    ui_core_get_element_abs_rect(elm, &dc->rect);

    {
        struct rect *src = draw ? draw : &dc->rect;

        dc->draw.left   = src->left;
        dc->draw.top    = src->top;
        dc->draw.width  = src->width;
        dc->draw.height = src->height;
    }

    dc->mask = NULL;

    return 0;
}

/*
 * 与 ui_core_get_up_element 同构, 只是把 top/height 换成 left/width。
 */
struct element *ui_core_get_left_element(struct element *elm)
{
    struct list_head *p;
    struct element *min_elm = NULL;
    struct element *max_elm = NULL;
    u32 min;
    u32 max;

    list_for_each(p, &elm->parent->child) {
        struct element *e = (struct element *)((u8 *)p - 12);
        u32 edge;

        if (e == elm) {
            continue;
        }

        edge = e->css.width + e->css.left;

        if (edge > (u32)elm->css.left) {
            if (!max_elm || max < edge - elm->css.left) {
                max = edge - elm->css.left;
                max_elm = e;
            }
        } else {
            if (!min_elm || min > elm->css.left - edge) {
                min = elm->css.left - edge;
                min_elm = e;
            }
        }
    }

    return min_elm ? min_elm : max_elm;
}

/*
 * "下"/"右"的结构与"上"/"左"【相反】: 参照边 edge 是【自己的】下(右)边沿,
 * 在循环外算一次; 循环里比的是兄弟的 top(left)。
 *   e->top <  edge -> 在自己上方或重叠, 记距离最大的(max)
 *   e->top >= edge -> 在自己下方,       记距离最小的(min)
 * 同样优先返回 min_elm, 没有才用 max_elm 绕回。
 */
struct element *ui_core_get_down_element(struct element *elm)
{
    struct list_head *p;
    struct element *min_elm = NULL;
    struct element *max_elm = NULL;
    u32 min;
    u32 max;
    u32 edge = elm->css.height + elm->css.top;

    list_for_each(p, &elm->parent->child) {
        struct element *e = (struct element *)((u8 *)p - 12);

        if (e == elm) {
            continue;
        }

        if ((u32)e->css.top < edge) {
            if (!max_elm || max < edge - e->css.top) {
                max = edge - e->css.top;
                max_elm = e;
            }
        } else {
            if (!min_elm || min > e->css.top - edge) {
                min = e->css.top - edge;
                min_elm = e;
            }
        }
    }

    return min_elm ? min_elm : max_elm;
}

struct element *ui_core_get_right_element(struct element *elm)
{
    struct list_head *p;
    struct element *min_elm = NULL;
    struct element *max_elm = NULL;
    u32 min;
    u32 max;
    u32 edge = elm->css.width + elm->css.left;

    list_for_each(p, &elm->parent->child) {
        struct element *e = (struct element *)((u8 *)p - 12);

        if (e == elm) {
            continue;
        }

        if ((u32)e->css.left < edge) {
            if (!max_elm || max < edge - e->css.left) {
                max = edge - e->css.left;
                max_elm = e;
            }
        } else {
            if (!min_elm || min > e->css.left - edge) {
                min = e->css.left - edge;
                min_elm = e;
            }
        }
    }

    return min_elm ? min_elm : max_elm;
}

/*
 * 触摸事件分发。
 *
 * 【遍历方向】子元素一律【从尾往前】(child.prev 起) —— child 链表按 z_order
 *   升序挂, 所以反向就是从最上层往下问, 谁先消费谁说话。而且必须是 safe 版
 *   (先把 prev 存下来再递归), 因为 handler 里可能把自己摘掉。
 *
 * 【四种分支】
 *   R/L/D/U_MOVE: 纯分发, 不动 touch_focus, 自己的 handler 返回值也不用。
 *   DOWN:  先清 touch_focus; 命中矩形才继续(lock 住的元素跳过命中判定);
 *          子元素优先, 都不要时自己 handler 消费了就把 touch_focus 设成自己。
 *   MOVE:  只发给 touch_focus, 且【重新组一个 event】——
 *          只填 onfocus/event/pos 三样, 其余字段是栈上的未初始化值(原库如此)。
 *   HOLD/UP(default): 直接发给 touch_focus; UP 之后清掉 touch_focus。
 *
 * @note MOVE 与 default 两支调 touch_focus->handler->ontouch 时【都没判空】,
 *       且 put 之前【重新读了一次 touch_focus】(handler 里可能改过它)。
 *       见文末 TODO。
 */
int ui_core_element_ontouch(struct element *elm, struct element_touch_event *e)
{
    struct rect r;
    struct element_touch_event ev;
    struct list_head *p;
    struct list_head *n;
    int ret;

    switch (e->event) {
    case ELM_EVENT_TOUCH_R_MOVE:
    case ELM_EVENT_TOUCH_L_MOVE:
    case ELM_EVENT_TOUCH_D_MOVE:
    case ELM_EVENT_TOUCH_U_MOVE:
        for (p = elm->child.prev, n = p->prev; p != &elm->child; p = n, n = p->prev) {
            struct element *c = (struct element *)((u8 *)p - 12);

            if (c->css.invisible) {
                continue;
            }
            if (ui_core_element_ontouch(c, e)) {
                return 1;
            }
        }

        if (elm->handler && elm->handler->ontouch) {
            elm->handler->ontouch(elm, e);
        }

        return 0;

    case ELM_EVENT_TOUCH_DOWN:
        touch_focus = NULL;

        if (lock != elm) {
            ui_core_get_element_abs_rect(elm, &r);
            if (!in_rect(&r, &e->pos)) {
                return 0;
            }
        }

        if (ui_core_get_element(elm) < 0) {
            return 0;
        }

        ret = 0;

        for (p = elm->child.prev, n = p->prev; p != &elm->child; p = n, n = p->prev) {
            struct element *c = (struct element *)((u8 *)p - 12);

            if (c->css.invisible) {
                continue;
            }
            if (ui_core_element_ontouch(c, e)) {
                ret = 1;
                goto _exit;
            }
        }

        if (elm->handler && elm->handler->ontouch) {
            if (elm->handler->ontouch(elm, e)) {
                touch_focus = elm;
                ret = 1;
            }
        }

_exit:
        ui_core_put_element(elm);
        return ret;

    case ELM_EVENT_TOUCH_MOVE:
        if (!touch_focus) {
            return 0;
        }

        ui_core_get_element_abs_rect(elm, &r);

        /* 加固: 原库只填 onfocus / event / pos 三样就把 ev 交给 handler,
         * 其余字段(xoffset / yoffset / hold_up / move_dir / private_data /
         * has_energy)全是栈上的未初始化值。先整体清零再填。 */
        memset(&ev, 0, sizeof(ev));

        ev.onfocus = in_rect(&r, &e->pos);
        ev.event   = e->event;
        ev.pos.x   = e->pos.x;
        ev.pos.y   = e->pos.y;

        if (ui_core_get_element(touch_focus) < 0) {
            return 0;
        }

        /* 加固: 原库直接调, 没判 handler 与 ontouch(同函数上面两处都判了)。 */
        ret = 0;
        if (touch_focus->handler && touch_focus->handler->ontouch) {
            ret = touch_focus->handler->ontouch(touch_focus, &ev);
        }
        ui_core_put_element(touch_focus);

        return ret;

    default:
        if (!touch_focus) {
            return 0;
        }

        if (ui_core_get_element(touch_focus) < 0) {
            return 0;
        }

        /* 加固: 同 MOVE 支, 原库直接调没判空。 */
        ret = 0;
        if (touch_focus->handler && touch_focus->handler->ontouch) {
            ret = touch_focus->handler->ontouch(touch_focus, e);
        }
        ui_core_put_element(touch_focus);

        if (e->event == ELM_EVENT_TOUCH_UP) {
            touch_focus = NULL;
        }

        return ret;
    }
}

/*
 * 触摸入口: 被 lock 住且可见时只发给它; 否则从根的子元素【尾部往前】依次问。
 */
int ui_core_ontouch(struct element_touch_event *e)
{
    struct list_head *p;
    struct list_head *n;

    if (lock && !lock->css.invisible) {
        return ui_core_element_ontouch(lock, e);
    }

    for (p = root.child.prev, n = p->prev; p != &root.child; p = n, n = p->prev) {
        if (ui_core_element_ontouch((struct element *)((u8 *)p - 12), e)) {
            return 1;
        }
    }

    return 0;
}

/*
 * 按键分发(递归): 子元素【从尾往前】优先, 都不消费才轮到自己的 handler。
 * 自己消费掉按键时顺便把焦点设到自己身上。
 *
 * @note 全程用 get/put_element 保护 —— handler 里有可能把这个 element 释放掉。
 *       get 返回负数(ref 已经是 0, 说明正在被销毁)就直接不管。
 */
int __ui_core_onkey(struct element *elm, struct element_key_event *e)
{
    struct list_head *p;
    struct list_head *n;
    int ret = 0;

    if (ui_core_get_element(elm) < 0) {
        return 0;
    }

    for (p = elm->child.prev, n = p->prev; p != &elm->child; p = n, n = p->prev) {
        struct element *c = (struct element *)((u8 *)p - 12);

        if (c->css.invisible) {
            continue;
        }
        if (__ui_core_onkey(c, e)) {
            ret = 1;
            goto _ret;
        }
    }

    if (elm->handler && elm->handler->onkey) {
        if (elm->handler->onkey(elm, e)) {
            ui_core_element_on_focus(elm, 1);
            ret = 1;
        }
    }

_ret:
    ui_core_put_element(elm);

    return ret;
}

/*
 * 按键入口, 两条完全不同的路子:
 *
 *   没有焦点(root.focus 为空)或焦点不可见 -> 走【传入的 elm】那棵树:
 *     自己 handler 先试, 不消费再从尾往前问子元素。
 *
 *   有可见焦点 -> 走【焦点元素】: 先给焦点自己, 不消费就沿 parent 一路往上冒泡。
 *
 * @note 三处原库缺陷(保持等价, 见文末 TODO):
 *   1. 焦点那条路上调 focus->handler->onkey 时【没判 handler 与 onkey 为空】。
 *   2. 冒泡那段里 ui_core_get_element 返回值判的是 > 0(不是 >= 0), 且拿到
 *      引用后【重新 load 了一次 handler 与 onkey】才调用。
 *   3. 第一条路子里子元素消费掉按键后, 返回值仍然是 0(原厂 do.end 的 phi
 *      在那条边上给的就是 0) —— 上层会以为没人处理。
 */
int ui_core_element_onkey(struct element *elm, struct element_key_event *e)
{
    struct list_head *p;
    struct list_head *n;
    struct element *focus = root.focus;
    int ret;

    if (!focus || focus->css.invisible) {
        if (ui_core_get_element(elm) < 0) {
            return 0;
        }

        ret = 0;

        if (elm->handler && elm->handler->onkey) {
            if (elm->handler->onkey(elm, e)) {
                ret = 1;
                goto _put_elm;
            }
        }

        for (p = elm->child.prev, n = p->prev; p != &elm->child; p = n, n = p->prev) {
            struct element *c = (struct element *)((u8 *)p - 12);

            if (c->css.invisible) {
                continue;
            }
            if (__ui_core_onkey(c, e)) {
                /*
                 * 加固: 原库这里【只 break 不置 ret】(原厂 do.end 的 phi 在这条
                 * 边上给的就是 0), 于是子元素明明已经消费掉按键, 返回给上层的
                 * 却是"没人处理"。
                 * @note 这会改变上层行为(原本可能继续把该键分发给别人),
                 *       需要真机验证一遍按键响应。
                 */
                ret = 1;
                break;
            }
        }

_put_elm:
        ui_core_put_element(elm);
        return ret;
    }

    if (ui_core_get_element(focus) < 0) {
        return 0;
    }

    ret = 0;

    /* 加固: 原库这里【直接调】focus->handler->onkey, 既没判 handler 也没判
     * onkey —— 焦点元素没注册 handler 时就是空指针解引用。注意同函数上面那条
     * 路子(无焦点时)判了两层, 只有这里漏了。 */
    if (focus->handler && focus->handler->onkey
        && focus->handler->onkey(focus, e)) {
        ret = 1;
    } else {
        struct element *q;

        for (q = focus->parent; q; q = q->parent) {
            if (q->handler && q->handler->onkey) {
                if (ui_core_get_element(q) > 0) {
                    ret = q->handler->onkey(q, e);
                    ui_core_put_element(q);
                    if (ret) {
                        break;
                    }
                }
            }
        }
    }

    ui_core_put_element(focus);

    return ret;
}

/*
 * 确保 elm->dc 有值: 没有就沿 parent 往上找第一个有 dc 的祖先并记下来。
 * 与 ui_core_get_draw_context 里那段是同一件事, 只是这里不拷贝任何参数。
 */
AT_UI_RAM
static void ui_core_get_dc(struct element *elm)
{
    struct element *parent;

    if (elm->dc) {
        return;
    }

    parent = elm;

    do {
        parent = parent->parent;
    } while (!parent->dc);

    elm->dc = parent->dc;
}

struct element *ui_core_get_next_elm(struct element *elm)
{
    if (list_empty(&elm->child)) {
        return NULL;
    }

    return (struct element *)((u8 *)elm->child.next - 12);
}

/*
 * 判断"重绘 re->elm 时它有没有被上层兄弟遮住", 结论写回 re->redraw。
 *
 * 遍历顺序是【正向】(z_order 从低到高), 所以遍历到 re->elm 之后再遇到的
 * 都是盖在它上面的。re->begin 就是"已经走过 re->elm 了"这个开关。
 *
 * @return 1 表示已经得出结论(调用方不用再往下找), 0 表示这一层没找到。
 *
 * @note background_color == 0xffffff 被当作"透明/不画背景"的哨兵值: 遇到这种
 *       兄弟直接返回 1 而【不改 redraw】—— 它盖住了但不会真的挡住内容。
 *       (同一个哨兵值在 window_show 里也出现过。)
 * @note redraw 的取值: 覆盖区与目标矩形完全相同 -> 0(整块被盖住);
 *       只是部分重叠 -> 2。
 */
int ui_core_if_disp(struct element *elm, struct redraw_t *re)
{
    struct rect a;
    struct rect c;
    struct list_head *p;

    list_for_each(p, &elm->child) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (!q->css.invisible) {
            if (q == re->elm) {
                re->begin  = 1;
                re->redraw = 1;
            } else if (re->begin) {
                ui_core_get_dc(q);
                ui_core_get_element_abs_rect(q, &a);

                if (get_rect_cover(&a, re->rect, &c)) {
                    if (q->css.background_color == 0xffffff) {
                        return 1;
                    }

                    if (c.left == re->rect->left && c.top == re->rect->top &&
                        c.width == re->rect->width && c.height == re->rect->height) {
                        re->redraw = 0;
                    } else {
                        re->redraw = 2;
                    }

                    return 1;
                }
            }
        }

        ui_core_if_disp(q, re);
    }

    return 0;
}

/*
 * 把 rect 这块区域的背景重画一遍: 先画 parent 自己与 rect 相交的部分,
 * 再递归各个可见子元素 —— 但【跳过 elm】(它自己马上要重画, 不用当背景)。
 */
int ui_core_show_background(struct element *parent, struct rect *rect,
                            struct element *elm)
{
    struct rect a;
    struct rect c;
    struct list_head *p;

    ui_core_get_element_abs_rect(parent, &a);

    if (get_rect_cover(&a, rect, &c)) {
        ui_core_show_rect(parent, &c);
    }

    list_for_each(p, &parent->child) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (q == elm || q->css.invisible) {
            continue;
        }

        ui_core_show_background(q, rect, elm);
    }

    return 0;
}

/*
 * 真正画一个元素的一块区域。整个绘制流程的最底层。
 *
 * 顺序(每一步都要照抄, 控件靠这几个回调分工):
 *   1. ON_CHANGE_SHOW_PROBE(4)  —— 给控件调整内容的机会(arg 为 NULL)
 *   2. ui_core_get_draw_context —— 组一个临时 dc
 *   3. ON_CHANGE_SHOW(5)        —— 控件自己画; 【返回 0 就整个结束】
 *      (grid/text 这些自绘控件就是在这里返回 0 接管绘制的)
 *   4. 背景色: background_color != 0xffffff 才填(0xffffff 是"透明"哨兵)
 *   5. 背景图: background_image > 0 才画, 象限参数是 image_quadrant
 *   6. 边框:   border.color 非 0 才画
 *   7. ON_CHANGE_SHOW_POST(6)
 *   8. 第一次显示时(state != 2)置 state = 2 并补发 ON_CHANGE_FIRST_SHOW(3)
 *
 * @note 第 8 步的 onchange 是【直接调】的, 不再判空 —— 能走到这里说明第 7 步
 *       已经判过了。
 */
static void ui_core_show_rect(struct element *elm, struct rect *rect)
{
    struct draw_context dc;

    memset(&dc, 0, sizeof(dc));

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_SHOW_PROBE, NULL);
    }

    ui_core_get_draw_context(&dc, elm, rect);

    if (elm->handler && elm->handler->onchange) {
        if (elm->handler->onchange(elm, ON_CHANGE_SHOW, &dc) == 0) {
            return;
        }
    }

    if (elm->css.background_color != 0xffffff) {
        platform_api->fill_rect(&dc, elm->css.background_color);
    }

    if (elm->css.background_image > 0) {
        platform_api->draw_image(&dc, elm->css.background_image,
                                 elm->css.image_quadrant, dc.mask);
    }

    if (elm->css.border.color) {
        platform_api->draw_rect(&dc, &elm->css.border);
    }

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_SHOW_POST, &dc);

        if (elm->state != 2) {
            elm->state = 2;
            elm->handler->onchange(elm, ON_CHANGE_FIRST_SHOW, &dc);
        }
    }
}

/*
 * "谁来画 rect 这块区域的背景"。两个函数结构同构、结论相反:
 *
 *   __ui_core_show_invalid_rect: 自己【有】背景就自己画; 没有才往上找祖先。
 *   __ui_core_show_rect:         自己【有】背景就什么都不做(调用方会画);
 *                                没有才往上找祖先画背景。
 *
 * "有背景"的判定分两种:
 *   是 layer((id & 0x3f0000) == 0x40000, 即 page 类型 4) -> 背景色或背景图任一有
 *   其它元素                                             -> 只看背景色
 * 0xffffff 是"无背景"的哨兵值。
 *
 * 往上找的循环【到 layer 为止】—— layer 是一层的绘制边界, 再往上不管了。
 */
static void __ui_core_show_invalid_rect(struct element *elm, struct rect *rect)
{
    struct element *parent;

    if ((elm->id & 0x3f0000) == 0x40000) {
        if (elm->css.background_color != 0xffffff || elm->css.background_image > 0) {
            ui_core_show_rect(elm, rect);
            return;
        }
    } else {
        if (elm->css.background_color != 0xffffff) {
            ui_core_show_rect(elm, rect);
            return;
        }
    }

    parent = elm;

    do {
        parent = parent->parent;

        if ((parent->id & 0x3f0000) == 0x40000) {
            if (parent->css.background_color != 0xffffff ||
                parent->css.background_image > 0) {
                ui_core_show_background(parent, rect, elm);
                return;
            }
        } else {
            if (parent->css.background_color != 0xffffff) {
                ui_core_show_background(parent, rect, elm);
                return;
            }
        }
    } while ((parent->id & 0x3f0000) != 0x40000);
}

static void __ui_core_show_rect(struct element *elm, struct rect *rect)
{
    struct element *parent;

    if ((elm->id & 0x3f0000) == 0x40000) {
        if (!(elm->css.background_image <= 0 &&
              elm->css.background_color == 0xffffff)) {
            return;
        }
    } else {
        if (elm->css.background_color != 0xffffff) {
            return;
        }
    }

    parent = elm;

    do {
        parent = parent->parent;

        if ((parent->id & 0x3f0000) == 0x40000) {
            if (parent->css.background_color != 0xffffff ||
                parent->css.background_image > 0) {
                ui_core_show_background(parent, rect, elm);
                return;
            }
        } else {
            if (parent->css.background_color != 0xffffff) {
                ui_core_show_background(parent, rect, elm);
                return;
            }
        }
    } while ((parent->id & 0x3f0000) != 0x40000);
}

/*
 * 绘制主递归: 画 elm 自己在 rect 范围内的部分, 再递归所有可见子元素。
 *
 * 按 dc->buf_num 分两种路子:
 *   buf_num == 2(双缓冲): 直接 ui_core_show_rect(elm, NULL) —— 整块重画,
 *      不做裁剪(反正是往后台缓冲画)。
 *   buf_num == 1(单缓冲): 要把 elm 的绝对矩形【裁剪到 dc->rect 之内】再画,
 *      裁剪后宽或高变负数就直接放弃(连子元素也不画了)。
 *      根的直接子元素(parent == &root)跳过自身绘制, 只递归。
 *
 * @note 双缓冲时首尾各做一次 get/put_draw_context(锁住后台 buffer)。
 * @note 递归子元素时用 get_element(...) > 0 保护, 且用 safe 遍历。
 * @note 子元素画完后给自己发一次 ON_CHANGE_SHOW_COMPLETED(12)。
 */
static void __ui_core_show(struct element *elm, struct rect *rect)
{
    struct rect r;
    struct rect a;
    struct rect c;
    struct draw_context *dc;
    struct list_head *p;
    struct list_head *n;

    ui_core_get_dc(elm);

    if (elm->dc->buf_num == 2) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(elm->dc);
        }
    }

    ui_core_get_element_abs_rect(elm, &r);

    dc = elm->dc;

    switch (dc->buf_num) {
    case 2:
        ui_core_show_rect(elm, NULL);
        break;

    case 1:
        if (elm->parent == &root) {
            break;
        }

        if (r.left < 0) {
            r.width += r.left;
            if (r.width < 0) {
                return;
            }
            r.left = 0;
        }

        if (r.top < 0) {
            r.height += r.top;
            if (r.height < 0) {
                return;
            }
            r.top = 0;
        }

        if (dc->rect.top + dc->rect.height < r.top) {
            return;
        }

        if (dc->rect.left + dc->rect.width < r.left) {
            return;
        }

        if (r.left + r.width > dc->rect.left + dc->rect.width) {
            r.width = dc->rect.left + dc->rect.width - r.left;
        }

        if (r.top + r.height > dc->rect.top + dc->rect.height) {
            r.height = dc->rect.top + dc->rect.height - r.top;
        }

        ui_core_get_element_abs_rect(elm, &a);

        if (get_rect_cover(&a, rect, &c)) {
            ui_core_show_rect(elm, &c);
        }
        break;

    default:
        break;
    }

    for (p = elm->child.next, n = p->next; p != &elm->child; p = n, n = p->next) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (q->css.invisible) {
            continue;
        }

        if (ui_core_get_element(q) > 0) {
            __ui_core_show(q, rect);
            ui_core_put_element(q);
        }
    }

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_SHOW_COMPLETED, NULL);
    }

    if (elm->dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(elm->dc);
        }
    }
}

/*
 * 单缓冲刷屏主循环。点阵屏的 buffer 通常放不下整屏, 所以要【分条带】刷:
 * 每轮 get_draw_context 拿到一条 disp 区域, 把落在这条里的内容画完再 put,
 * 直到 disp 走完整个 need_draw。
 *
 * 【为什么要对齐】点阵屏按列/行分组寻址(col_align / row_align), 刷新区域必须
 * 对齐到组边界。所以先把 elm 的绝对矩形向外扩到对齐边界(r), 原始矩形留在
 * orig 里; 扩出来的那一圈(左/右/上/下四条)不属于 elm 自己, 得让别人来画背景
 * —— 那就是 rect_l/r/t/b 与 __ui_core_show_invalid_rect 的用途。
 * col_align/row_align 为 1 时不需要扩, 对应的那两条也就不用算。
 *
 * @note get_rect_nocover_* 返回 0(没有多出来的那一条)时要把对应 rect 清零,
 *       否则里面是栈上的垃圾, 后面 get_rect_cover 会误判。
 * @note elm->parent == &root 时先换成它的第一个子元素(root 的直接子元素是
 *       window, 真正带 dc 的是下面的 layer)。
 */
int _ui_core_show(struct element *elm)
{
    struct rect r;
    struct rect orig;
    struct rect rect_l;
    struct rect rect_r;
    struct rect rect_t;
    struct rect rect_b;
    struct rect c;
    struct draw_context *dc;
    int col_align;
    int row_align;
    int left;
    int top;
    int right;
    int bottom;

    ui_core_get_dc(elm);

#if UI_PORT_PUSH_TRACE
    printf("[show] enter id=0x%x parent_is_root=%d\n",
           elm->id, (elm->parent == &root));
#endif

    if (elm->parent == &root) {
        elm = ui_core_get_next_elm(elm);
        if (!elm) {
#if UI_PORT_PUSH_TRACE
            /* window 底下没有 layer -> 什么都不会画, 且返回值被 window.c 忽略 */
            printf("[show] BAIL: window has no child layer\n");
#endif
            return -EINVAL;
        }
#if UI_PORT_PUSH_TRACE
        printf("[show] descend to id=0x%x\n", elm->id);
#endif
    }

    if (elm->dc->buf_num != 1) {
#if UI_PORT_PUSH_TRACE
        printf("[show] BAIL: buf_num=%d != 1\n", elm->dc->buf_num);
#endif
        return 0;
    }

    if (elm->parent == &root) {
#if UI_PORT_PUSH_TRACE
        printf("[show] BAIL: elm still root child\n");
#endif
        return 0;
    }

    ui_core_get_element_abs_rect(elm, &r);
    orig = r;

    dc        = elm->dc;
    col_align = dc->col_align;
    row_align = dc->row_align;

    left   = r.left - r.left % col_align;
    top    = r.top - r.top % row_align;

    right  = col_align - 1 + r.left + r.width;
    right  = right - right % col_align;
    if (right > dc->width) {
        right = dc->width;
    }

    bottom = row_align - 1 + r.top + r.height;
    bottom = bottom - bottom % row_align;
    if (bottom > dc->height) {
        bottom = dc->height;
    }

    r.left   = left;
    r.width  = right - left;
    r.top    = top;
    r.height = bottom - top;

    if (col_align != 1) {
        if (!get_rect_nocover_l(&orig, &r, &rect_l)) {
            memset(&rect_l, 0, sizeof(rect_l));
        }
        if (!get_rect_nocover_r(&orig, &r, &rect_r)) {
            memset(&rect_r, 0, sizeof(rect_r));
        }
    }

    if (row_align != 1) {
        if (!get_rect_nocover_t(&orig, &r, &rect_t)) {
            memset(&rect_t, 0, sizeof(rect_t));
        }
        if (!get_rect_nocover_b(&orig, &r, &rect_b)) {
            memset(&rect_b, 0, sizeof(rect_b));
        }
    }

    elm->dc->need_draw = r;
    memset(&elm->dc->disp, 0, sizeof(struct rect));

#if UI_PORT_PUSH_TRACE
    printf("[show] need_draw [%d,%d,%d,%d] align c=%d r=%d\n",
           r.left, r.top, r.width, r.height, col_align, row_align);
#endif

    while (elm->dc->need_draw.top + elm->dc->need_draw.height >
           elm->dc->disp.top + elm->dc->disp.height) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(elm->dc);
        }
        if (platform_api->set_draw_context) {
            platform_api->set_draw_context(elm->dc);
        }

        if (col_align != 1) {
            if (get_rect_cover(&elm->dc->disp, &rect_l, &c)) {
                __ui_core_show_invalid_rect(elm, &c);
            }
            if (get_rect_cover(&elm->dc->disp, &rect_r, &c)) {
                __ui_core_show_invalid_rect(elm, &c);
            }
        }

        if (row_align != 1) {
            if (get_rect_cover(&elm->dc->disp, &rect_t, &c)) {
                __ui_core_show_invalid_rect(elm, &c);
            }
            if (get_rect_cover(&elm->dc->disp, &rect_b, &c)) {
                __ui_core_show_invalid_rect(elm, &c);
            }
        }

        __ui_core_show_rect(elm, &elm->dc->disp);
        __ui_core_show(elm, &elm->dc->disp);

        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(elm->dc);
        }
    }

    return 0;
}

/*
 * @param init 非 0 表示"首次显示", 此时【不动】invisible 位;
 *             为 0 时若元素处于隐藏态就先取消隐藏再画。
 */
int ui_core_show(void *_elm, int init)
{
    struct element *elm = (struct element *)_elm;
    int err;

    if (!_elm) {
        return -EINVAL;
    }

    if (ui_core_get_element(elm) < 0) {
        return -EINVAL;
    }

    if (init == 0) {
        if (elm->css.invisible) {
            elm->css.invisible = 0;
        }
    }

    err = _ui_core_show(elm);
    ui_core_put_element(elm);

    return err;
}

/*
 * 隐藏: 置 state = 1 与 invisible = 1, 发 ON_CHANGE_HIDE, 然后把它原来占的
 * 位置重绘掉(让底下的内容露出来)。
 *
 * @note 已经是隐藏态就直接返回 0(不重复发事件)。
 * @note 只有 elm->dc->elm != elm 时才重绘 —— dc 就是它自己的(layer 级)时,
 *       整个 dc 都要关掉, 不走这条路。
 */
int ui_core_hide(void *_elm)
{
    struct element *elm = (struct element *)_elm;
    int err = 0;

    if (ui_core_get_element(elm) < 0) {
        return -EINVAL;
    }

    if (elm->css.invisible) {
        ui_core_put_element(elm);
        return 0;
    }

    elm->state         = 1;
    elm->css.invisible = 1;

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_HIDE, NULL);
    }

    if (elm->dc->elm != elm) {
        err = ui_core_redraw_old(_elm);
    }

    ui_core_put_element(elm);

    return err;
}

/*
 * 从空闲队列里取一个延后调用的节点; 队列空(或队头那个还在用)就 malloc 一个。
 * 与 ui_core_api.c 里的同名函数是同一套写法。
 *
 * @note 用 __list_del_entry(只做 __list_del 的两次 store), 不是 list_del ——
 *       后者会额外做自环初始化。取到之后还判了一次 p(恒真), 照抄。
 */
static struct ui_core_wait_call *__get_call_entry(void)
{
    struct ui_core_wait_call *p = NULL;
    struct list_head *node = handl.entry.next;

    if (node != &handl.entry) {
        p = (struct ui_core_wait_call *)node;
        if (p->func == NULL) {
            __list_del_entry(node);
            if (p) {
                return p;
            }
        }
    }

    p = malloc(sizeof(struct ui_core_wait_call));
    if (!p) {
        return NULL;
    }

    return p;
}

/*
 * 重绘 elm 与 r 相交的那块, 并递归所有可见子元素。
 * 双缓冲时首尾各 get/put 一次后台 buffer; 与 r 完全不相交时直接收尾返回。
 */
static void ui_core_redraw_rect(struct element *elm, struct rect *r)
{
    struct rect a;
    struct rect c;
    struct list_head *p;

    ui_core_get_dc(elm);

    if (elm->dc->buf_num == 2) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(elm->dc);
        }
    }

    ui_core_get_element_abs_rect(elm, &a);

    if (!get_rect_cover(&a, r, &c)) {
        if (elm->dc->buf_num == 2) {
            if (platform_api->put_draw_context) {
                platform_api->put_draw_context(elm->dc);
            }
        }
        return;
    }

    ui_core_show_rect(elm, &c);

    list_for_each(p, &elm->child) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (q->css.invisible) {
            continue;
        }

        if (ui_core_get_element(q) > 0) {
            ui_core_redraw_rect(q, &c);
            ui_core_put_element(q);
        }
    }

    if (elm->dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(elm->dc);
        }
    }
}

/*
 * 把排队的延后调用全部执行一遍(取出时先把 func 清空, 节点即回到空闲状态)。
 */
static void __do_wait_call(void)
{
    struct ui_core_wait_call *p;
    struct ui_core_wait_call *n;

    for (p = (struct ui_core_wait_call *)handl.entry.next,
         n = (struct ui_core_wait_call *)p->entry.next;
         &p->entry != &handl.entry;
         p = n, n = (struct ui_core_wait_call *)p->entry.next) {
        if (p->func) {
            int (*func)(void *) = p->func;
            void *elm = p->elm;

            p->func = NULL;
            func(elm);
        }
    }
}

/*
 * 局部重绘入口。整个模块最复杂的一个函数。
 *
 * 【重入保护】handl.count 非 0 说明当前正在绘制中(可能是某个控件的 onchange
 * 里又调了 redraw), 这时把请求挂进队列直接返回, 等最外层那次结束后由
 * __do_wait_call 统一补做。
 *
 * 【流程】
 *   1. elm 还没有 dc -> 走一次完整的 ui_core_show。
 *   2. 把 elm 的绝对矩形裁剪到 dc->rect 之内, 再按 col/row_align 向外扩齐,
 *      扩出来的四条边(rect_l/r/t/b)记下来。
 *   3. 双缓冲(buf_num == 2): 直接 ui_core_redraw_rect(dc->elm, &r)。
 *   4. 单缓冲: 先用 ui_core_if_disp 问一句"这块区域到底要不要重画"
 *      (re.redraw 为 0 就整块被别人盖住了, 一次都不用刷);
 *      要画就分条带循环, 每条带里:
 *        - 补画扩齐多出来的四条边(不属于 elm, 让祖先画背景)
 *        - 再问一次 if_disp, 按结论选三种画法:
 *            redraw == 2 -> 从 dc->elm 整个重画这条带
 *            redraw == 3 -> 先画自己背景, 再从【父元素】重画
 *            其它        -> 先画自己背景, 再从自己重画
 *
 * @note 早退路径(裁剪后没有可见区域)【直接返回, 没有 put_element 也没有把
 *       handl.count 减回去】—— 原库如此, 见文末 TODO, 这是个真缺陷。
 */
int ui_core_redraw(void *_elm)
{
    struct element *elm = (struct element *)_elm;
    struct rect r;
    struct rect orig;
    struct rect rect_l;
    struct rect rect_r;
    struct rect rect_t;
    struct rect rect_b;
    struct rect c;
    struct redraw_t re;
    struct redraw_t re2;
    struct draw_context *dc;
    int col_align;
    int row_align;
    int left;
    int top;
    int right;
    int bottom;
    int err;

    if (handl.count) {
        struct ui_core_wait_call *p = __get_call_entry();

        if (!p) {
            return -ENOMEM;
        }

        p->elm  = elm;
        p->func = ui_core_redraw;
        list_add_tail(&p->entry, &handl.entry);

        return 0;
    }

    handl.count = 1;

    if (ui_core_get_element(elm) < 0) {
        /* 加固: 这一处【不能走 _end】—— 引用根本没拿到, 不该 put。
         * 但 handl.count 上一行刚置 1, 原库直接 return 就把它永久卡住了,
         * 之后所有 redraw 都会被当成"正在绘制"排队, 界面再也不刷新。 */
        handl.count = 0;
        return -EINVAL;
    }

    if (!elm->dc) {
        err = ui_core_show(_elm, 0);
        goto _end;
    }

    ui_core_get_element_abs_rect(elm, &r);

    if (r.left < 0) {
        r.width += r.left;
        if (r.width < 0) {
            /* 加固: 原库这里【直接 return, 不走 _end】—— 引用没还回去
             * (ui_core_put_element), ui_core_redraw 里还漏了把 handl.count
             * 减回去。后者更要命: count 一旦卡在 1, 之后所有 redraw 都会被
             * 当成"正在绘制"排进队列, 【界面就再也不刷新了】。 */
            err = -EINVAL;
            goto _end;
        }
        r.left = 0;
    }

    if (r.top < 0) {
        r.height += r.top;
        if (r.height < 1) {
            /* 加固: 原库这里【直接 return, 不走 _end】—— 引用没还回去
             * (ui_core_put_element), ui_core_redraw 里还漏了把 handl.count
             * 减回去。后者更要命: count 一旦卡在 1, 之后所有 redraw 都会被
             * 当成"正在绘制"排进队列, 【界面就再也不刷新了】。 */
            err = -EINVAL;
            goto _end;
        }
        r.top = 0;
    }

    dc = elm->dc;

    if (dc->rect.top + dc->rect.height < r.top) {
        /* 加固: 同上, 原库直接 return 不走 _end, 引用与 handl.count 都没还。 */
        err = -EINVAL;
        goto _end;
    }

    if (dc->rect.left + dc->rect.width < r.left) {
        /* 加固: 同上, 原库直接 return 不走 _end, 引用与 handl.count 都没还。 */
        err = -EINVAL;
        goto _end;
    }

    if (r.left + r.width > dc->rect.left + dc->rect.width) {
        r.width = dc->rect.left + dc->rect.width - r.left;
    }

    if (r.top + r.height > dc->rect.top + dc->rect.height) {
        r.height = dc->rect.top + dc->rect.height - r.top;
    }

    orig      = r;
    col_align = dc->col_align;
    row_align = dc->row_align;

    left   = r.left - r.left % col_align;
    top    = r.top - r.top % row_align;

    right  = col_align - 1 + r.left + r.width;
    right  = right - right % col_align;
    if (right > dc->width) {
        right = dc->width;
    }

    bottom = row_align - 1 + r.top + r.height;
    bottom = bottom - bottom % row_align;
    if (bottom > dc->height) {
        bottom = dc->height;
    }

    r.left   = left;
    r.width  = right - left;
    r.top    = top;
    r.height = bottom - top;

    if (col_align != 1) {
        if (!get_rect_nocover_l(&orig, &r, &rect_l)) {
            memset(&rect_l, 0, sizeof(rect_l));
        }
        if (!get_rect_nocover_r(&orig, &r, &rect_r)) {
            memset(&rect_r, 0, sizeof(rect_r));
        }
    }

    if (row_align != 1) {
        if (!get_rect_nocover_t(&orig, &r, &rect_t)) {
            memset(&rect_t, 0, sizeof(rect_t));
        }
        if (!get_rect_nocover_b(&orig, &r, &rect_b)) {
            memset(&rect_b, 0, sizeof(rect_b));
        }
    }

    switch (elm->dc->buf_num) {
    case 2:
        ui_core_redraw_rect(elm->dc->elm, &r);
        err = 0;
        break;

    case 1:
        elm->dc->need_draw = r;
        memset(&elm->dc->disp, 0, sizeof(struct rect));

        re.begin  = 0;
        re.redraw = 0;
        re.rect   = &r;
        re.elm    = elm;
        ui_core_if_disp(elm->dc->elm, &re);

        err = -EINVAL;

        while (re.redraw &&
               elm->dc->need_draw.top + elm->dc->need_draw.height >
               elm->dc->disp.top + elm->dc->disp.height) {
            if (platform_api->get_draw_context) {
                platform_api->get_draw_context(elm->dc);
            }
            if (platform_api->set_draw_context) {
                platform_api->set_draw_context(elm->dc);
            }

            if (col_align != 1) {
                if (get_rect_cover(&elm->dc->disp, &rect_l, &c)) {
                    __ui_core_show_invalid_rect(elm, &c);
                }
                if (get_rect_cover(&elm->dc->disp, &rect_r, &c)) {
                    __ui_core_show_invalid_rect(elm, &c);
                }
            }

            if (row_align != 1) {
                if (get_rect_cover(&elm->dc->disp, &rect_t, &c)) {
                    __ui_core_show_invalid_rect(elm, &c);
                }
                if (get_rect_cover(&elm->dc->disp, &rect_b, &c)) {
                    __ui_core_show_invalid_rect(elm, &c);
                }
            }

            re2.begin  = 0;
            re2.redraw = 0;
            re2.rect   = &elm->dc->disp;
            re2.elm    = elm;
            ui_core_if_disp(elm->dc->elm, &re2);

            if (re2.redraw == 2) {
                ui_core_redraw_rect(elm->dc->elm, &elm->dc->disp);
            } else {
                __ui_core_show_rect(elm, &elm->dc->disp);

                if (re2.redraw == 3) {
                    ui_core_redraw_rect(elm->parent, &elm->dc->disp);
                } else {
                    ui_core_redraw_rect(elm, &elm->dc->disp);
                }
            }

            if (platform_api->put_draw_context) {
                platform_api->put_draw_context(elm->dc);
            }

            err = 0;
        }
        break;

    default:
        err = -EINVAL;
        break;
    }

_end:
    ui_core_put_element(elm);

    handl.count--;
    if (handl.count == 0) {
        __do_wait_call();
    }

    return err;
}

/*
 * ==== *_old 系列 ====
 * 这四个是旧版绘制路径, 不做 ui_core_if_disp 的遮挡判断、也不处理对齐扩边,
 * 一律整块重画。ui_core_hide 就是走这条路把隐藏元素占的位置刷掉的。
 * 结构与新版一一对应, 只是简单得多。
 */

/*
 * 与 ui_core_redraw_rect 完全同构, 唯一区别是递归子元素时传的是【原始 r】
 * 而不是相交后的 c(所以子元素各自再和 r 求交)。
 */
static void ui_core_redraw_rect_old(struct element *elm, struct rect *r)
{
    struct rect a;
    struct rect c;
    struct list_head *p;

    ui_core_get_dc(elm);

    if (elm->dc->buf_num == 2) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(elm->dc);
        }
    }

    ui_core_get_element_abs_rect(elm, &a);

    if (!get_rect_cover(&a, r, &c)) {
        if (elm->dc->buf_num == 2) {
            if (platform_api->put_draw_context) {
                platform_api->put_draw_context(elm->dc);
            }
        }
        return;
    }

    ui_core_show_rect(elm, &c);

    list_for_each(p, &elm->child) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (q->css.invisible) {
            continue;
        }

        if (ui_core_get_element(q) > 0) {
            ui_core_redraw_rect_old(q, r);
            ui_core_put_element(q);
        }
    }

    if (elm->dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(elm->dc);
        }
    }
}

/*
 * 旧版整树绘制。与 __ui_core_show 的区别:
 *   - 单缓冲那支自己就把条带循环做完了(每条带调 ui_core_redraw_rect),
 *     而不是交给上层的 _ui_core_show;
 *   - 不做对齐扩边;
 *   - 递归子元素时用的是 __ui_core_show_old 自己。
 */
static int __ui_core_show_old(struct element *elm)
{
    struct rect r;
    struct draw_context *dc;
    struct list_head *p;
    struct list_head *n;

    if (!elm->dc) {
        struct element *parent = elm;

        do {
            parent = parent->parent;
        } while (!parent->dc);

        elm->dc = parent->dc;
    }

    dc = elm->dc;

    if (dc->buf_num == 2) {
        if (platform_api->get_draw_context) {
            platform_api->get_draw_context(dc);
        }
    }

    ui_core_get_element_abs_rect(elm, &r);

    switch (elm->dc->buf_num) {
    case 2:
        ui_core_show_rect(elm, NULL);
        break;

    case 1:
        if (elm->parent == &root) {
            break;
        }

        if (r.left < 0) {
            r.width += r.left;
            if (r.width < 0) {
                return -EINVAL;
            }
            r.left = 0;
        }

        if (r.top < 0) {
            r.height += r.top;
            if (r.height < 0) {
                return -EINVAL;
            }
            r.top = 0;
        }

        if (dc->rect.top + dc->rect.height < r.top) {
            return -EINVAL;
        }

        if (dc->rect.left + dc->rect.width < r.left) {
            return -EINVAL;
        }

        if (r.left + r.width > dc->rect.left + dc->rect.width) {
            r.width = dc->rect.left + dc->rect.width - r.left;
        }

        if (r.top + r.height > dc->rect.top + dc->rect.height) {
            r.height = dc->rect.top + dc->rect.height - r.top;
        }

        dc->need_draw = r;
        memset(&elm->dc->disp, 0, sizeof(struct rect));

        while (elm->dc->need_draw.top + elm->dc->need_draw.height >
               elm->dc->disp.top + elm->dc->disp.height) {
            if (platform_api->get_draw_context) {
                platform_api->get_draw_context(elm->dc);
            }
            if (platform_api->set_draw_context) {
                platform_api->set_draw_context(elm->dc);
            }

            ui_core_redraw_rect(elm->dc->elm, &elm->dc->disp);

            if (platform_api->put_draw_context) {
                platform_api->put_draw_context(elm->dc);
            }
        }
        break;

    default:
        break;
    }

    for (p = elm->child.next, n = p->next; p != &elm->child; p = n, n = p->next) {
        struct element *q = (struct element *)((u8 *)p - 12);

        if (q->css.invisible) {
            continue;
        }

        if (ui_core_get_element(q) > 0) {
            __ui_core_show_old(q);
            ui_core_put_element(q);
        }
    }

    if (elm->handler && elm->handler->onchange) {
        elm->handler->onchange(elm, ON_CHANGE_SHOW_COMPLETED, NULL);
    }

    if (elm->dc->buf_num == 2) {
        if (platform_api->put_draw_context) {
            platform_api->put_draw_context(elm->dc);
        }
    }

    return 0;
}

int ui_core_show_old(void *_elm, int init)
{
    struct element *elm = (struct element *)_elm;
    int err;

    if (!_elm) {
        return -EINVAL;
    }

    if (ui_core_get_element(elm) < 0) {
        return -EINVAL;
    }

    if (init == 0) {
        if (elm->css.invisible) {
            elm->css.invisible = 0;
        }
    }

    err = __ui_core_show_old(elm);
    ui_core_put_element(elm);

    return err;
}

/*
 * 旧版局部重绘。没有重入排队、没有遮挡判断、没有对齐扩边 ——
 * 裁剪完就整块交给 ui_core_redraw_rect_old。
 *
 * @note 早退路径同样【不 put_element】(与 ui_core_redraw 一样的缺陷), 照抄。
 */
int ui_core_redraw_old(void *_elm)
{
    struct element *elm = (struct element *)_elm;
    struct rect r;
    struct draw_context *dc;
    int err;

    if (ui_core_get_element(elm) < 0) {
        return -EINVAL;
    }

    if (!elm->dc) {
        err = ui_core_show_old(_elm, 0);
        goto _end;
    }

    ui_core_get_element_abs_rect(elm, &r);

    if (r.left < 0) {
        r.width += r.left;
        if (r.width < 0) {
            /* 加固: 原库这里【直接 return, 不走 _end】—— 引用没还回去
             * (ui_core_put_element), ui_core_redraw 里还漏了把 handl.count
             * 减回去。后者更要命: count 一旦卡在 1, 之后所有 redraw 都会被
             * 当成"正在绘制"排进队列, 【界面就再也不刷新了】。 */
            err = -EINVAL;
            goto _end;
        }
        r.left = 0;
    }

    if (r.top < 0) {
        r.height += r.top;
        if (r.height < 0) {
            /* 加固: 原库这里【直接 return, 不走 _end】—— 引用没还回去
             * (ui_core_put_element), ui_core_redraw 里还漏了把 handl.count
             * 减回去。后者更要命: count 一旦卡在 1, 之后所有 redraw 都会被
             * 当成"正在绘制"排进队列, 【界面就再也不刷新了】。 */
            err = -EINVAL;
            goto _end;
        }
        r.top = 0;
    }

    dc = elm->dc;

    if (dc->rect.top + dc->rect.height < r.top) {
        /* 加固: 同上, 原库直接 return 不走 _end, 引用与 handl.count 都没还。 */
        err = -EINVAL;
        goto _end;
    }

    if (dc->rect.left + dc->rect.width < r.left) {
        /* 加固: 同上, 原库直接 return 不走 _end, 引用与 handl.count 都没还。 */
        err = -EINVAL;
        goto _end;
    }

    if (r.left + r.width > dc->rect.left + dc->rect.width) {
        r.width = dc->rect.left + dc->rect.width - r.left;
    }

    if (r.top + r.height > dc->rect.top + dc->rect.height) {
        r.height = dc->rect.top + dc->rect.height - r.top;
    }

    switch (dc->buf_num) {
    case 2:
        ui_core_redraw_rect_old(dc->elm, &r);
        err = 0;
        break;

    case 1:
        dc->need_draw = r;
        memset(&elm->dc->disp, 0, sizeof(struct rect));

        err = -EINVAL;

        while (elm->dc->need_draw.top + elm->dc->need_draw.height >
               elm->dc->disp.top + elm->dc->disp.height) {
            if (platform_api->get_draw_context) {
                platform_api->get_draw_context(elm->dc);
            }
            if (platform_api->set_draw_context) {
                platform_api->set_draw_context(elm->dc);
            }

            ui_core_redraw_rect_old(elm->dc->elm, &elm->dc->disp);

            if (platform_api->put_draw_context) {
                platform_api->put_draw_context(elm->dc);
            }

            err = 0;
        }
        break;

    default:
        err = -EINVAL;
        break;
    }

_end:
    ui_core_put_element(elm);

    return err;
}

/*
 * 原库缺陷清单 + 加固状态(描述的是【原库】行为; 方括号是当前处理结果,
 * 差异已登记在 accept/ 并锁定指纹)。
 *
 *  [已修] 1. ui_core_redraw / ui_core_redraw_old 的【早退路径不释放引用】——
 *            裁剪后没有可见区域时直接 return -EINVAL, 没走 ui_core_put_element;
 *            ui_core_redraw 还漏了把 handl.count 减回去。后者更严重: count 一旦
 *            卡在 1, 之后所有 redraw 都会被当成"正在绘制"排进队列, 【界面就再也
 *            不刷新了】。八处早退已全部改走 _end; 唯独 get_element 失败那处
 *            【不能走 _end】(引用根本没拿到, 不该 put), 改为就地复位 count。
 *  [已修] 2. ui_core_element_onkey 的焦点路径上 focus->handler->onkey 是【直接
 *            调】的, 没判 handler 与 onkey 为空 —— 焦点元素没注册 handler 时
 *            空指针解引用。(同函数无焦点那条路子是判了两层的, 只有这里漏了。)
 *  [保留] 3. 冒泡那段里 ui_core_get_element 的返回值判的是 > 0 而不是 >= 0。
 *            【这不是缺陷】: ui_core_get_element 失败返回 -EINVAL, 成功返回
 *            elm->ref 自增【之后】的值, 而进入时 ref != 0, 所以成功时必 >= 2
 *            —— > 0 与 >= 0 在这里完全等价。至于"拿到引用后又重新 load 一次
 *            handler/onkey": 单线程 UI 任务里不存在 TOCTOU, 保持原样。
 *  [已修] 4. 第一条路子里子元素消费掉按键之后返回值仍是 0(原厂 do.end 的 phi
 *            在那条边上给的就是 0), 上层会以为没人处理。已置 ret = 1。
 *            @note 这会改变上层行为(原本可能继续把该键分发给别人), 需真机验证。
 *  [已修] 5. ui_core_element_ontouch 的 MOVE 与 HOLD/UP 两支调
 *            touch_focus->handler->ontouch 时同样没判空。已补。
 *  [已修] 6. MOVE 支里那个临时 event 只填了 onfocus/event/pos 三样, 其余字段
 *            (xoffset/yoffset/hold_up/move_dir/private_data/has_energy)是栈上
 *            未初始化值就传给了 handler。已在填之前整体 memset。
 *
 *  1. ui_core_redraw / ui_core_redraw_old 的【早退路径不释放引用】——
 *     裁剪后没有可见区域时直接 return -EINVAL, 没走 ui_core_put_element,
 *     ui_core_redraw 还漏了把 handl.count 减回去。后者更严重: count 一旦
 *     卡在 1, 之后所有 redraw 都会被当成"正在绘制"而排进队列, 界面就再也
 *     不刷新了。修法: 早退改为 goto _end。
 *
 *  2. ui_core_element_onkey 的焦点路径上, focus->handler->onkey 是【直接调】的,
 *     没判 handler 与 onkey 为空。焦点元素没注册 handler 时空指针解引用。
 *
 *  3. 同函数冒泡那段里 ui_core_get_element 的返回值判的是 > 0 而不是 >= 0,
 *     且拿到引用后又重新 load 了一次 handler/onkey 才调用。
 *
 *  4. ui_core_element_onkey 第一条路子里, 子元素消费掉按键之后返回值仍然是 0
 *     (原厂 do.end 的 phi 在那条边上给的就是 0), 上层会以为没人处理。
 *
 *  5. ui_core_element_ontouch 的 MOVE 与 HOLD/UP 两支调
 *     touch_focus->handler->ontouch 时同样没判空。
 *
 *  6. ui_core_element_ontouch 的 MOVE 支里那个临时 event 只填了
 *     onfocus/event/pos 三样, 其余字段(xoffset/yoffset/hold_up/move_dir/
 *     private_data/has_energy)是栈上的未初始化值就传给了 handler。
 */
