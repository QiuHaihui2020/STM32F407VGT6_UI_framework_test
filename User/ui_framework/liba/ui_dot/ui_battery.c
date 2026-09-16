/*
 * ui_battery.c —— 电池控件
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的:
 *   struct ui_battery: elm=0 src=72 index=76 charge_image=78 normal_image=80
 *                      entry=84(next=84,prev=88) info=92 handler=96, sizeof=100
 *   struct ui_battery_info: head=0 normal_image=16 charge_image=20 action=24
 *   entry 在 +84, 所以 list_entry 从链表节点回推控件基址时减的就是 84。
 *
 * 【本模块特点】所有电池控件挂在一条静态链表 head 上, 便于
 *   ui_battery_level_change() 一次性刷新全部电池控件。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_battery.data.bss")
#pragma data_seg(".ui_battery.data")
#pragma const_seg(".ui_battery.text.const")
#pragma code_seg(".ui_battery.text")
#endif

#include "ui/ui_battery.h"

static LIST_HEAD(head);

/*
 * 设置背景图就是一句 `battery->elm.css.background_image = src;`,
 * 【两处调用点直接写赋值】, 不单独包一个 helper ——
 * 那是个位域写(64 位读-改-写, 展开约 6 条指令), -Oz 下编译器会判定
 * "不内联更省体积", 于是多出一个函数体, 反而变大。
 */

/*
 * @note 充电态与常态取图方式不同:
 *   充电态 —— 按 index 逐帧轮播(每调一次前进一帧, 到末帧回 0), 形成充电动画;
 *   常态   —— 按电量百分比换算帧号 persent / (100 / num + 1)。
 */
static void battery_level_change(void *_battery, int persent, int incharge)
{
    struct ui_battery *battery = (struct ui_battery *)_battery;
    struct ui_image_list *img;
    u16 image;
    u8 index;

    if (incharge) {
        img = platform_api->load_image_list(battery->elm.page,
                                            (void *)(u32)battery->charge_image);
        if (!img || !img->num) {
            return;
        }
        image = img->image[battery->index];
        /*
         * @note 自增要用 u8 局部变量过一手: 直接写
         *       battery->index + 1 == img->num 的话, C 的整型提升会让加法在
         *       int 上做 —— index 是 u8, 到 255 时应当回绕, 在 int 上算就不会。
         */
        index = battery->index + 1;
        battery->index = (index == img->num) ? 0 : index;
    } else {
        img = platform_api->load_image_list(battery->elm.page,
                                            (void *)(u32)battery->normal_image);
        if (!img || !img->num) {
            return;
        }
        image = img->image[persent / (100 / img->num + 1)];
    }

    if (image == battery->src) {
        return;
    }
    battery->src = image;
    battery->elm.css.background_image = image;   /* 见文件开头关于位域写的说明 */
    ui_core_redraw(battery);
}

/*
 * @note 应用层 onchange 返回 true 时通常吃掉事件, 但 RELEASE_PROBE / RELEASE
 *       是例外, 必须继续往下走: 前者要摘链, 后者要释放内存 —— 漏了就会在
 *       链表里留下野指针。
 */
int battery_on_change(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_battery *battery = (struct ui_battery *)_elm;

    /* handler 本身也判一层: new_ui_battery 里已兜底为 dumy_handler, 这里是
     * 纵深防御。 */
    if (battery->handler && battery->handler->onchange) {
        if (battery->handler->onchange(battery, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    switch (event) {
    case ON_CHANGE_RELEASE_PROBE:
        list_del(&battery->entry);
        break;
    case ON_CHANGE_RELEASE:
        ui_core_remove_element(battery);
        ui_core_free(battery);
        break;
    default:
        break;
    }

    return true;
}

static const struct element_event_handler battery_div_handler = {
    .id       = 0,
    .ontouch  = NULL,
    .onkey    = NULL,
    .onchange = battery_on_change,
};

static void *new_ui_battery(const void *_info, struct element *parent)
{
    struct ui_battery *battery;
    struct ui_battery_info *info;
    struct ui_image_list *img;
    struct element_css1 *css;

    info = platform_api->load_widget_info((void *)_info, 0xff);

    battery = ui_core_malloc(sizeof(struct ui_battery));
    if (!battery) {
        return NULL;
    }
    memset(battery, 0, sizeof(struct ui_battery));

    list_add(&battery->entry, &head);
    battery->info  = _info;
    battery->index = 0;

    battery->normal_image = (u16)(u32)info->normal_image;
    battery->charge_image = (u16)(u32)info->charge_image;

    /* img 要判空再取 image[0], 否则资源缺图时就是空指针解引用。
     * 取不到就把 src 留作 0(battery 已 memset 清零), 控件仍建得起来 ——
     * 表现为电池图标不显示, 而不是整机死机。 */
    img = platform_api->load_image_list(info->head.page, info->normal_image);
    if (img && img->num) {
        battery->src = img->image[0];
    }

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
    ui_core_element_init(&battery->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &battery_div_handler, info->action);
    ui_core_element_append_child(parent, &battery->elm);

    battery->handler = element_event_handler_for_id(info->head.id);
    if (!battery->handler) {
        battery->handler = &dumy_handler;
    }
    if (battery->handler->onchange) {
        battery->handler->onchange(battery, ON_CHANGE_INIT, NULL);
    }

    battery->elm.css.background_image = battery->src;  /* 同上, 直接写位域 */

    return battery;
}

int ui_battery_set_level_by_id(int id, int persent, int incharge)
{
    struct list_head *pos;
    struct ui_battery *battery;

    list_for_each(pos, &head) {
        battery = list_entry(pos, struct ui_battery, entry);
        if (battery->elm.id == id) {
            battery_level_change(battery, persent, incharge);
            return 0;
        }
    }

    return -EINVAL;
}

/*
 * @note 与 battery_level_change 逻辑相同, 但【不写 background_image、不 redraw】,
 *       只更新 battery->src —— 供 ON_CHANGE_INIT 阶段使用(该阶段禁止 redraw)。
 */
int ui_battery_set_level(struct ui_battery *battery, int persent, int incharge)
{
    struct ui_image_list *img;
    u16 image;
    u8 index;

    if (incharge) {
        img = platform_api->load_image_list(battery->elm.page,
                                            (void *)(u32)battery->charge_image);
        if (!img || !img->num) {
            return -EINVAL;
        }
        image = img->image[battery->index];
        /* @note 自增要用 u8 局部变量过一手, 理由同 battery_level_change。 */
        index = battery->index + 1;
        battery->index = (index == img->num) ? 0 : index;
    } else {
        img = platform_api->load_image_list(battery->elm.page,
                                            (void *)(u32)battery->normal_image);
        if (!img || !img->num) {
            return -EINVAL;
        }
        image = img->image[persent / (100 / img->num + 1)];
    }

    if (image != battery->src) {
        battery->src = image;
    }

    return 0;
}

void ui_battery_level_change(int persent, int incharge)
{
    struct list_head *pos;
    struct ui_battery *battery;

    list_for_each(pos, &head) {
        battery = list_entry(pos, struct ui_battery, entry);
        battery_level_change(battery, persent, incharge);
    }
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_battery_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_BATTERY)
.new = new_ui_battery,
};

/*
 * 实现注意事项
 *
 *  1) 【load_image_list 的返回值一律判空】资源缺图时它会返回 NULL, 直接取
 *     img->image[0] 就是空指针解引用。取不到图时把 src 留作 0, 控件仍建得
 *     起来 —— 表现为图标不显示, 而不是整机死机。
 *
 *  2) 【handler 判两层】battery_on_change 里先判 battery->handler 再判
 *     onchange。new_ui_battery 已把 handler 兜底成 dumy_handler, 所以这层
 *     是纵深防御。
 *
 *  3) 【RELEASE_PROBE / RELEASE 不能被应用层吃掉】前者要摘链、后者要释放
 *     内存, 漏了会在静态链表里留下野指针。
 *
 *  4) 【充电态与常态取图方式不同】充电态按 index 逐帧轮播(形成动画), 常态按
 *     电量百分比换算帧号。index 的自增要在 u8 上做, 见函数内说明。
 */
