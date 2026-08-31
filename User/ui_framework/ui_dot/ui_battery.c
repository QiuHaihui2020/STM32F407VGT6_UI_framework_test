/*
 * ui_battery.c —— 电池控件
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_battery.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_battery.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_battery.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   battery_set_image_src@14  battery_level_change@20  battery_on_change@51
 *   new_ui_battery@84  ui_battery_set_level_by_id@127  ui_battery_set_level@139
 *   ui_battery_level_change@169  ui_battery_enable@180
 *
 *   battery_set_image_src 在原库中已被内联(无独立 define), 其签名取自 DWARF:
 *   arg1=battery, arg2=src, 无返回值。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_battery: elm=0 src=72 index=76 charge_image=78 normal_image=80
 *                      entry=84(next=84,prev=88) info=92 handler=96, sizeof=100
 *   struct ui_battery_info: head=0 normal_image=16 charge_image=20 action=24
 *   list_entry 反算: IR 里 gep(entry, -84) 得控件基址、gep(entry, -80) 得 elm.id
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
 * 原库在 ui_battery.c:14 有一个 4 行的静态 helper:
 *     static void battery_set_image_src(struct ui_battery *battery, int src)
 *     { battery->elm.css.background_image = src; }
 * (签名取自 DWARF: arg1=battery, arg2=src)
 * 它在原厂构建里被内联掉了, 没有留下独立函数体。
 *
 * 这里【直接在两处调用点写赋值】而不保留 helper: 因为写成 helper 时本地 clang
 * 在 -Oz 下判定"不内联更省体积"(位域写是 64 位 读-改-写, 展开约 6 条指令, 比
 * 一次 call 大), 于是多出一个函数体, 并连带使 battery_level_change 与
 * new_ui_battery 无法与原厂逐条比对。直接展开后产出的代码与原厂完全一致。
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
         * @note index 必须用 u8 局部变量过一手 —— 原库的自增是在 u8 上做的
         *       (IR 为 add i8 后再 zext 比较)。若直接写
         *       battery->index + 1 == img->num, C 的整型提升会让加法在 int 上做,
         *       IR 变成 add i32 + trunc, 与原厂对不上。
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
    battery->elm.css.background_image = image;   /* 原为 battery_set_image_src() */
    ui_core_redraw(battery);
}

/*
 * @note 1. 与 ui_pic 不同, 此处【没有】判 battery->handler 本身是否为 NULL,
 *          只判了 handler->onchange —— 原库行为(由 new_ui_battery 兜底为
 *          dumy_handler, 暂不会触发)。
 *       2. 应用层 onchange 返回 true 时通常吃掉事件, 但 RELEASE_PROBE/RELEASE
 *          例外, 必须继续往下走: 前者要摘链, 后者要释放内存, 漏了会挂链表野指针。
 */
int battery_on_change(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_battery *battery = (struct ui_battery *)_elm;

    /* 加固: 原库未判 handler 本身(当前由 new_ui_battery 兜底为 dumy_handler)。 */
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

    /* 加固: 原库不判 img 就取 image[0], 资源缺图时必然空指针解引用。
     * 取不到就把 src 留作 0(battery 已 memset 清零), 控件仍建得起来 ——
     * 表现为电池图标不显示, 而不是整机死机。 */
    img = platform_api->load_image_list(info->head.page, info->normal_image);
    if (img && img->num) {
        battery->src = img->image[0];
    }

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
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

    battery->elm.css.background_image = battery->src;  /* 原为 battery_set_image_src() */

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
        /*
         * @note index 必须用 u8 局部变量过一手 —— 原库的自增是在 u8 上做的
         *       (IR 为 add i8 后再 zext 比较)。若直接写
         *       battery->index + 1 == img->num, C 的整型提升会让加法在 int 上做,
         *       IR 变成 add i32 + trunc, 与原厂对不上。
         */
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
 * 加固记录(原库缺陷已全部修完, 见 README 第 8 节):
 *
 * [已修] 1. new_ui_battery 不检查 load_image_list 的返回值就取 img->image[0],
 *           资源缺图时必然空指针解引用。-> 补判空; 取不到就把 src 留作 0
 *           (battery 已 memset 清零), 控件仍建得起来, 表现为图标不显示,
 *           而不是整机死机。
 * [已修] 2. battery_on_change 未判 battery->handler 为 NULL(当前由 new_ui_battery
 *           兜底为 dumy_handler, 属纵深防御)。-> 补 handler 判空。
 *
 * 差异已登记在 cpu/br27/tools/ui_reimpl/accept/ui_battery.txt 并锁定指纹。
 */
