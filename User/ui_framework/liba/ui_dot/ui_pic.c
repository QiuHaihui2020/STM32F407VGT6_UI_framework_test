/*
 * ui_pic.c —— 图片控件(屏上所有图标都走它)
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
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
 * @note 两点容易看错的地方:
 *   1. 高亮/常态分支的 css 取法不对称: 高亮取 &css[1], 常态取 css[0] ——
 *      资源里高亮态的 css 就排在常态那一项的后面。
 *   2. "取图并写 background_image" 这段在两个分支里各写一份, 没有合并到
 *      if/else 之后 —— 两边取的 img 来源不同, 合并反而要多一个中间变量。
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

    /* handler 本身也判一层: new_ui_pic 里已兜底成 dumy_handler, 这里是防
     * 日后有别的构造路径绕过兜底。 */
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

    /* 同 pic_onkey: handler 本身也判一层。 */
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

    /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
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
 * @note 高亮分支用 img->image[pic->index](u8 下标), 常态分支用 image[index]
 *       (int 下标); 常态分支会【重新调一次】load_image_list 而不是复用上面
 *       那个 img —— 上面那次取的是 highlight_img, 不能混用。
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
        /* 这里要再判一次 img: 函数开头那次判的是另一次调用的结果, 中途资源
         * 可能被换出而返回 NULL。 */
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
    /* load_image_list 的返回值也要判, 资源缺图时它会返回 NULL。 */
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
    /* 同上: load_image_list 的返回值也要判。 */
    if (!img) {
        return -EINVAL;
    }
    return img->num;
}

/*
 * @note 下面两个 _by_id 里, 【判空必须写在解引用之前】—— id 查不到控件时
 *       ui_core_get_element_by_id 返回 NULL, 顺序写反就等于没判。
 */
int ui_pic_get_normal_image_number_by_id(int id)
{
    struct ui_pic *pic = (struct ui_pic *)ui_core_get_element_by_id(id);
    struct ui_pic_info *info;
    struct ui_image_list *img;

    /* 判空要在解引用 pic->info / pic->elm.page 【之前】, 否则等于没判;
     * info / img 的返回值也一并检查。 */
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

    /* 同 ui_pic_get_normal_image_number_by_id: 判空在解引用之前。 */
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
 * 实现注意事项
 *
 *  1) 【_by_id 系列: 判空在解引用之前】ui_core_get_element_by_id 查不到就返回
 *     NULL, 判断写在 pic->info / pic->elm.page 之后等于没判。
 *
 *  2) 【load_widget_info / load_image_list 的返回值一律判】资源缺图或页面没
 *     加载时它们会返回 NULL, 直接取 img->image[...] 就是空指针解引用。
 *     ui_pic_set_image_index 的 else 分支是【第二次】调用 load_image_list,
 *     要单独再判一次。
 *
 *  3) 【handler 判两层】pic_onkey / pic_ontouch 先判 pic->handler 再判具体
 *     回调。new_ui_pic 已兜底成 dumy_handler, 这层是纵深防御。
 *
 *  4) 【RELEASE_PROBE / RELEASE 不能被应用层吃掉】否则控件内存不会被回收。
 *
 *  5) 【高亮态的 css 紧跟在常态后面】pic_highlight 里高亮取 &css[1]、常态取
 *     css[0], 这是资源里的排布约定。
 */
