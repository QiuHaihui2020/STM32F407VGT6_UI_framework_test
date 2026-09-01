/*
 * ui_pic.c —— 图片控件(屏上所有图标都走它)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_pic.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_pic.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_pic.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   pic_release@18  pic_highlight@27  pic_onchange@55  pic_onkey@82
 *   pic_ontouch@95  new_ui_pic@114  ui_pic_set_image_index@170
 *   ui_pic_show_image@190  ui_pic_get_normal_image_number@208
 *   ui_pic_get_highlgiht_image_number@223  ..._by_id@238  ..._by_id@251
 *   ui_pic_show_image_by_id@265  ui_pic_set_hide_by_id@277  ui_pic_enable@292
 *
 *   其中 pic_release / pic_highlight 在原库中已被内联进 pic_onchange
 *   (DWARF 有记录但无独立 define), 此处按其原始行号拆回独立函数。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_pic: elm=0 index=72 info=76 handler=80, sizeof=84
 *   struct element: 位域=0 id=4 css=32; css.background_image 在 +52 的 i64 位域内
 *   struct ui_ctrl_info_head: type=0 ctrl_num=1 css_num=2 len=3 page=4 id=8 css=12
 *   struct ui_pic_info: head=0 highlight=16 cent_x=18 cent_y=20
 *                       normal_img=24 highlight_img=28 action=32, sizeof=36
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_pic.data.bss")
#pragma data_seg(".ui_pic.data")
#pragma const_seg(".ui_pic.text.const")
#pragma code_seg(".ui_pic.text")
#endif

#include "ui/ui_pic.h"
#include "ui/control.h"

static void pic_release(struct ui_pic *pic)
{
    ui_core_remove_element(pic);
    ui_core_free(pic);
}

/*
 * @note 两处原库特征, 还原时需照抄, 否则 IR 对不上:
 *   1. 高亮/常态分支的 css 取法不对称: 高亮取 &css[1], 常态取 &css[0]
 *      (IR 中高亮分支为 gep element_css1*, i32 1, 常态分支直接用 css 指针)。
 *   2. "取图并写 background_image" 这段在两个分支里各写了一份, 不是合并到
 *      if/else 之后 —— 参考 IR 里是两份互不相干的副本(无 phi 汇聚, 也没有
 *      残留的 bitcast), 合并写法会多出一次 bitcast 与一个 phi。
 */
static void pic_highlight(struct ui_pic *pic, u8 hi)
{
    struct ui_pic_info *info;
    struct ui_image_list *img;

    info = platform_api->load_widget_info((void *)pic->info, 0xff);

    if (hi) {
        img = platform_api->load_image_list(pic->elm.page, info->highlight_img);
        if (info->head.css_num > 1) {
            ui_core_set_element_css(pic,
                platform_api->load_css(pic->elm.page, &info->head.css[1]));
        }
        if (img && img->num) {
            pic->elm.css.background_image = img->image[pic->index];
        }
    } else {
        img = platform_api->load_image_list(pic->elm.page, info->normal_img);
        if (info->head.css_num > 1) {
            ui_core_set_element_css(pic,
                platform_api->load_css(pic->elm.page, info->head.css));
        }
        if (img && img->num) {
            pic->elm.css.background_image = img->image[pic->index];
        }
    }
}

/*
 * @note 应用层 onchange 返回 true 时通常直接吃掉事件, 但 RELEASE_PROBE/RELEASE
 *       两个事件例外 —— 必须继续往下走, 否则控件内存不会被回收。
 */
static int pic_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_pic *pic = (struct ui_pic *)_elm;

    if (pic->handler && pic->handler->onchange) {
        if (pic->handler->onchange(pic, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    switch (event) {
    case ON_CHANGE_RELEASE:
        pic_release(pic);
        break;
    case ON_CHANGE_HIGHLIGHT:
        pic_highlight(pic, (u8)(u32)arg);
        break;
    default:
        break;
    }

    return true;
}

static int pic_onkey(void *_elm, struct element_key_event *e)
{
    struct ui_pic *pic = (struct ui_pic *)_elm;

    /* 加固: 原库未判 handler 本身。当前由 new_ui_pic 兜底指向 dumy_handler,
     * 尚不会触发; 补上是防日后有别的构造路径绕过兜底。 */
    if (pic->handler && pic->handler->onkey) {
        if (pic->handler->onkey(pic, e)) {
            return true;
        }
    }

    return false;
}

static int pic_ontouch(void *_elm, struct element_touch_event *e)
{
    struct ui_pic *pic = (struct ui_pic *)_elm;

    /* 加固: 同 pic_onkey。 */
    if (pic->handler && pic->handler->ontouch) {
        if (pic->handler->ontouch(pic, e)) {
            return true;
        }
    }

    return false;
}

static const struct element_event_handler pic_event_handler = {
    .id       = 0,
    .ontouch  = pic_ontouch,
    .onkey    = pic_onkey,
    .onchange = pic_onchange,
};

void *new_ui_pic(const void *_info, struct element *parent)
{
    struct ui_pic *pic;
    struct ui_pic_info *info;
    struct element_css1 *css;
    struct ui_image_list *img;

    pic = (struct ui_pic *)ui_core_malloc(sizeof(struct ui_pic));
    if (!pic) {
        return NULL;
    }

    info = platform_api->load_widget_info((void *)_info, 0xff);
    pic->info = (const struct ui_pic_info *)_info;

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
    ui_core_element_init(&pic->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &pic_event_handler, info->action);
    ui_core_element_append_child(parent, &pic->elm);

    if (info->normal_img) {
        img = platform_api->load_image_list(info->head.page, info->normal_img);
        if (img) {
            pic->elm.css.background_image = img->image[0];
        }
    }

    pic->handler = element_event_handler_for_id(info->head.id);
    if (!pic->handler) {
        pic->handler = &dumy_handler;
    }
    if (pic->handler->onchange) {
        pic->handler->onchange(pic, ON_CHANGE_INIT, NULL);
    }

    if (info->highlight) {
        ui_core_highlight_element(&pic->elm, 1);
    }

    return pic;
}

/*
 * @note 高亮分支用 img->image[pic->index](u8 下标), 常态分支用 image[index](int 下标),
 *       且常态分支重新调了一次 load_image_list 而非复用上面的 img。均为原库行为。
 */
int ui_pic_set_image_index(struct ui_pic *pic, int index)
{
    struct ui_pic_info *info;
    struct ui_image_list *img;

    info = platform_api->load_widget_info((void *)pic->info, 0xff);
    img = platform_api->load_image_list(pic->elm.page, info->normal_img);
    if (!img || img->num <= index) {
        return -EINVAL;
    }

    pic->index = index;

    img = platform_api->load_image_list(pic->elm.page, info->highlight_img);
    if (pic->elm.highlight && img && img->num) {
        pic->elm.css.background_image = img->image[pic->index];
    } else {
        img = platform_api->load_image_list(pic->elm.page, info->normal_img);
        /* 加固: 原库此处不判 img 就取 image[index]。函数开头虽已判过一次,
         * 但这是【第二次调用】, 中途资源可能被换出而返回 NULL。 */
        if (!img || img->num <= index) {
            return -EINVAL;
        }
        pic->elm.css.background_image = img->image[index];
    }

    return 0;
}

int ui_pic_show_image(struct ui_pic *pic, int index)
{
    int ret = ui_pic_set_image_index(pic, index);

    if (ret) {
        return ret;
    }

    if (pic->elm.css.invisible) {
        ui_core_show(pic, 0);
    } else {
        ui_core_redraw(pic);
    }

    return 0;
}

int ui_pic_get_normal_image_number(struct ui_pic *pic)
{
    struct ui_pic_info *info;
    struct ui_image_list *img;

    info = platform_api->load_widget_info((void *)pic->info, 0xff);
    if (!info) {
        return -EINVAL;
    }

    img = platform_api->load_image_list(pic->elm.page, info->normal_img);
    /* 加固: 原库判了 info 却没判 load_image_list 的返回值。 */
    if (!img) {
        return -EINVAL;
    }
    return img->num;
}

int ui_pic_get_highlgiht_image_number(struct ui_pic *pic)
{
    struct ui_pic_info *info;
    struct ui_image_list *img;

    info = platform_api->load_widget_info((void *)pic->info, 0xff);
    if (!info) {
        return -EINVAL;
    }

    img = platform_api->load_image_list(pic->elm.page, info->highlight_img);
    /* 加固: 同上, 原库未判 load_image_list 的返回值。 */
    if (!img) {
        return -EINVAL;
    }
    return img->num;
}

/*
 * @note 原库缺陷: 下面两个 _by_id 在判 pic 是否为 NULL 之前就解引用了 pic->info
 *       与 pic->elm.page(IR 中两次 platform_api 调用位于 icmp null 之前, 且调用
 *       不可被投机提升, 说明源码本身就是这个顺序)。
 *       该问题【已加固】(判空提到解引用之前), 见文末"加固记录"。
 */
int ui_pic_get_normal_image_number_by_id(int id)
{
    struct ui_pic *pic = (struct ui_pic *)ui_core_get_element_by_id(id);
    struct ui_pic_info *info;
    struct ui_image_list *img;

    /* 加固: 原库【先解引用 pic->info 与 pic->elm.page, 之后才判 pic 是否为 NULL】
     * —— 判断写在解引用后面, 等于没判, id 查不到控件时必然空指针解引用。
     * 这里把判断提到解引用之前, 并补上 info / img 的返回值检查。 */
    if (!pic) {
        return -EINVAL;
    }

    info = platform_api->load_widget_info((void *)pic->info, 0xff);
    if (!info) {
        return -EINVAL;
    }

    img = platform_api->load_image_list(pic->elm.page, info->normal_img);
    if (!img) {
        return -EINVAL;
    }

    return img->num;
}

int ui_pic_get_highlgiht_image_number_by_id(int id)
{
    struct ui_pic *pic = (struct ui_pic *)ui_core_get_element_by_id(id);
    struct ui_pic_info *info;
    struct ui_image_list *img;

    /* 加固: 同 ui_pic_get_normal_image_number_by_id, 原库判断写在解引用之后。 */
    if (!pic) {
        return -EINVAL;
    }

    info = platform_api->load_widget_info((void *)pic->info, 0xff);
    if (!info) {
        return -EINVAL;
    }

    img = platform_api->load_image_list(pic->elm.page, info->highlight_img);
    if (!img) {
        return -EINVAL;
    }

    return img->num;
}

int ui_pic_show_image_by_id(int id, int index)
{
    struct ui_pic *pic = (struct ui_pic *)ui_core_get_element_by_id(id);

    if (pic) {
        return ui_pic_show_image(pic, index);
    }

    return -EINVAL;
}

int ui_pic_set_hide_by_id(int id, int hide)
{
    struct element *elm = ui_core_get_element_by_id(id);

    if (!elm) {
        return -EINVAL;
    }

    elm->css.invisible = hide;

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_pic_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_PIC)
.new = new_ui_pic,
};

/*
 * 加固记录(原库缺陷已全部修完, 见 README 第 8 节):
 *
 * [已修] 1. ui_pic_get_normal_image_number_by_id / ..._highlgiht_..._by_id
 *           先解引用 pic 再判空(判断写在解引用后面, 等于没判), 且不检查
 *           load_widget_info / load_image_list 的返回值。
 *           -> 判空提到解引用之前, 并补齐 info / img 判空。
 * [已修] 2. ui_pic_get_normal_image_number / ..._highlgiht_image_number 判了 info
 *           却未判 load_image_list 的返回值。-> 补 img 判空。
 * [已修] 3. ui_pic_set_image_index 的 else 分支未判 img 就取 img->image[index]。
 *           -> 补 img 判空与 num 上界(那是第二次 load_image_list, 中途资源
 *              可能被换出, 开头那次判空管不着)。
 * [已修] 4. pic_onkey / pic_ontouch 未判 pic->handler 为 NULL(当前由 new_ui_pic
 *           兜底指向 dumy_handler, 属纵深防御)。-> 补 handler 判空。
 *
 * 差异已登记在 cpu/br27/tools/ui_reimpl/accept/ui_pic.txt 并锁定指纹。
 * 注: ui_pic_show_image_by_id 本身没改, 它出现差异是因为尾调用的
 *     ui_pic_set_image_index 长了 12 字节, 相对位移随之变化。
 */
