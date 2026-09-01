/*
 * ui_text.c —— 文本控件(歌名、歌词、菜单项文字都走它, 含滚动显示)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_text.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_text.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_text.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   text_highlight@21  text_ontouch@30  do_scroll@43  text_onchange@94
 *   text_release@167  new_ui_text@174  ui_text_set_index@239
 *   ui_text_set_combine_index@291  ui_text_show_index_by_id@315
 *   ui_text_set_str@346  ui_text_set_wstr@367  ui_text_set_utf8_str@384
 *   ui_text_set_str_by_id@399  ui_text_set_text_by_id@412
 *   ui_text_set_textw_by_id@426  ui_text_set_textu_by_id@440
 *   ui_text_set_text_attrs@457  ui_text_set_hide_by_id@466  ui_text_enable@479
 *
 *   text_highlight 在原库已被内联进 text_onchange(无独立 define)。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_text: elm=0 attrs=72 source=92 timer=100 _str[3]=102
 *   (注: timer 已由 u16 加固为 void *, 于是 _str 之后的偏移比上面这行大 2 ——
 *    上面记的是【原库】布局, 留着是为了对照 IR 时不迷路)
 *                   _format[7]=108 str_num=115 index=116 info=120 handler=124
 *                   sizeof=128
 *   struct ui_text_attrs: str=72 format=76 color=80 strlen=84 offset=86
 *                         位域(encode:2,endian:1,flags:5)=88 displen=90
 *   struct ui_text_info: head=0 source=16 code=24 color=32 highlight_color=36
 *                        str=40 action=44
 *   struct ui_text_list: num=0, str=2 (声明是 char str[0], 实际按 u16 用)
 *
 * 【位域小技巧】attrs 的位域整字节里 encode 占低 2 位、endian 第 2 位、
 *   flags 占高 5 位。所以 "整字节 < 0"(bit7) 就等价于 flags & 16,
 *   IR 里的 icmp slt i8 x, 0 就是这个判断。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_text.data.bss")
#pragma data_seg(".ui_text.data")
#pragma const_seg(".ui_text.text.const")
#pragma code_seg(".ui_text.text")
#endif

#include "ui/ui_text.h"

static int text_ontouch(void *_elm, struct element_touch_event *e)
{
    struct ui_text *text = (struct ui_text *)_elm;

    if (text->handler->ontouch) {
        if (text->handler->ontouch(text, e)) {
            return true;
        }
    }

    return false;
}

/*
 * 滚动定时回调(1 秒一次), 每次把 attrs.offset 往前推一格再重绘。
 *
 * 两种格式的推进方式不同:
 *   "strpic" —— 按 8 递增, 到 displen 归 0(整段图片串循环);
 *   "text"   —— 按【一个字符】的字节数递增, 具体几字节看编码:
 *                 encode 0(ascii/gbk): 首字节高位为 1 算 2 字节, 否则 1
 *                 encode 1(unicode)  : 固定 2
 *                 encode 2(utf8)     : 按首字节前缀判 1/2/3/4
 *               剩余不足 4 字节时归 0 重新开始。
 */
static void do_scroll(void *_elm)
{
    struct ui_text *text = (struct ui_text *)_elm;

    if (!text) {
        return;
    }
    if (!text->timer) {
        return;
    }

    if (!strcmp(text->_format, "strpic")) {
        if (text->attrs.offset == text->attrs.displen) {
            text->attrs.offset = 0;
        } else {
            text->attrs.offset += 8;
        }
        if (text->attrs.offset > text->attrs.displen) {
            text->attrs.offset = text->attrs.displen;
        }
    } else if (!strcmp(text->_format, "text")) {
        if (text->attrs.offset + 4 < text->attrs.strlen) {
            switch (text->attrs.encode) {
            case 0:
                text->attrs.offset += (text->attrs.str[text->attrs.offset] < 0) ? 2 : 1;
                break;
            case 1:
                text->attrs.offset += 2;
                break;
            case 2: {
                /*
                 * @note 掩码结果要显式截回 char —— 原厂的掩码运算全在 i8 上做
                 *       (and i8 c, -8 / icmp eq i8 -16)。不加转换的话 c 会先
                 *       提升到 int, 变成 and i32 248, 与原厂对不上。
                 *       末档写 c > -1 对应原厂的 icmp sgt i8 c, -1。
                 */
                char c = text->attrs.str[text->attrs.offset];

                if ((char)(c & 0xf8) == (char)0xf0) {
                    text->attrs.offset += 4;
                } else if ((char)(c & 0xf0) == (char)0xe0) {
                    text->attrs.offset += 3;
                } else if ((char)(c & 0xe0) == (char)0xc0) {
                    text->attrs.offset += 2;
                } else {
                    /* 原为 (c > -1), 即"signed char 为正 = ASCII 单字节"。
                     * ARM 上 char 无符号, 该判断恒为真, 编译器也会告警。
                     * 直接写成 else: 行为与 ARM 上原式的编译结果一致, 且
                     * 保证任何字节都会推进 offset —— 若照 pi32 的有符号语义,
                     * 遇到落单的 UTF-8 续字节(0x80~0xBF)会一步都不前进,
                     * 外层循环就死住了。 */
                    text->attrs.offset += 1;
                }
                break;
            }
            default:
                break;
            }
        } else {
            text->attrs.offset = 0;
        }
    }

    ui_core_redraw(text);
}

/*
 * @note 1. load_widget_info 是【无条件先调】的, 在应用层 onchange 之前。
 *       2. 与 ui_pic 等不同, 这里【不判】text->handler 本身是否为 NULL。
 *       3. flags 的 bit3(&8) 控制"显示后自动起滚动定时器",
 *          bit4(&16) 控制"高亮时才滚动"; 二者互斥地决定定时器的开关时机。
 */
static int text_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_text *text = (struct ui_text *)_elm;
    struct ui_text_info *info;

    info = platform_api->load_widget_info((void *)text->info, 0xff);

    if (text->handler->onchange) {
        if (text->handler->onchange(text, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    switch (event) {
    case ON_CHANGE_HIDE:
    case ON_CHANGE_RELEASE_PROBE:
        if (text->timer) {
            platform_api->del_timer(text->timer);
            text->timer = NULL;
        }
        break;

    case ON_CHANGE_RELEASE:
        text_release(text);
        break;

    case ON_CHANGE_SHOW_POST:
        platform_api->show_text((struct draw_context *)arg, &text->attrs);
        if (text->attrs.flags & 8) {
            if (!text->timer) {
                text->timer = platform_api->set_timer(text, do_scroll, 1000);
            }
        } else if (!(text->attrs.flags & 16)) {
            if (text->timer) {
                platform_api->del_timer(text->timer);
                text->timer = NULL;
            }
        }
        break;

    case ON_CHANGE_HIGHLIGHT:
        /* text_highlight@21 被内联到这里 */
        /*
         * @note 两侧的判断【顺序不对称】, 照抄原厂:
         *   取消高亮(arg==NULL): 先判 timer 再判 flags;
         *   进入高亮(arg!=NULL): 先判 flags 再判 timer。
         *   而且 if 写的是 !arg 在前 —— 换成 arg 在前会把两个块的排布调换。
         */
        if (!arg) {
            if (text->timer) {
                if (text->attrs.flags & 16) {
                    platform_api->del_timer(text->timer);
                    text->timer = NULL;
                    text->attrs.offset = 0;
                }
            }
            text->attrs.color = info->color & 0xffff;
        } else {
            if (text->attrs.flags & 16) {
                if (!text->timer) {
                    text->timer = platform_api->set_timer(text, do_scroll, 1000);
                }
            }
            text->attrs.color = info->highlight_color & 0xffff;
        }
        info = platform_api->load_widget_info((void *)text->info, 0xff);
        if (info->head.css_num > 1) {
            ui_core_set_element_css(text,
                platform_api->load_css(text->elm.page,
                                       &info->head.css[arg ? 1 : 0]));
        }
        break;

    default:
        break;
    }

    return true;
}

static const struct element_event_handler text_event_handler = {
    .id       = 0,
    .ontouch  = text_ontouch,
    .onkey    = NULL,
    .onchange = text_onchange,
};

void text_release(struct ui_text *text)
{
    ui_core_remove_element(text);
    ui_core_free(text);
}

void *new_ui_text(const void *_info, struct element *parent)
{
    struct ui_text *text;
    struct ui_text_info *info;
    struct ui_text_list *list;
    struct element_css1 *css;
    u16 *str;

    text = ui_core_malloc(sizeof(struct ui_text));
    if (!text) {
        return NULL;
    }

    info = platform_api->load_widget_info((void *)_info, 0xff);

    strcpy(text->source, info->source);
    text->info = _info;
    text->attrs.color = info->color & 0xffff;

    /* 资源里的字符串 id 表, 前 3 个缓存进 _str[] */
    /*
     * @note 这里必须用 info->head.page, 不能用 text->elm.page ——
     *       此时 ui_core_element_init 还没调用, elm 里的 page 是未初始化的。
     *       原厂 IR 正是 load info->head.page。
     */
    list = platform_api->load_text_list(info->head.page, info->str);
    if (!list) {
        str = text->_str;
    } else {
        u16 num = list->num;
        u16 n = (num < UI_TEXT_LIST_MAX_NUM) ? num : UI_TEXT_LIST_MAX_NUM;

        text->str_num = num;
        str = text->_str;
        memcpy(text->_str, list->str, n * 2);
    }

    memcpy(text->_format, info->code, 7);
    text->attrs.str = (char *)str;
    text->attrs.format = text->_format;
    /* "ascii" 格式的内容由业务层后续 set, 这里先清空 */
    if (!strcmp(text->_format, "ascii")) {
        text->attrs.str = NULL;
    }

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
    ui_core_element_init(&text->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &text_event_handler, info->action);
    ui_core_element_append_child(parent, &text->elm);

    text->handler = element_event_handler_for_id(info->head.id);
    if (!text->handler) {
        text->handler = &dumy_handler;
    }
    if (text->handler->onchange) {
        text->handler->onchange(text, ON_CHANGE_INIT, NULL);
    }

    return text;
}

/*
 * 把 attrs.str 指向第 index 个字符串 id。
 * 前 UI_TEXT_LIST_MAX_NUM(3) 个直接指向 _str[] 里的缓存, 第 3 个及以后
 * 从资源里现取一个塞进 _str[2] 再指过去。
 */
int ui_text_set_index(struct ui_text *text, int index)
{
    struct ui_text_info *info;
    struct ui_text_list *list;
    u16 *p;
    int i;
    /*
     * @note 前一个循环的计数器是【无符号】的 —— 原厂 IR 为 icmp ult i32 n, 2;
     *       用 int 会得到 icmp slt。后一个循环用的是有符号 i(原厂为 slt)。
     */
    u32 n;

    if (index < 0 || text->str_num < index) {
        return -EINVAL;
    }

    p = text->_str;
    for (n = 0; n < 2; n++) {
        if (n == index) {
            text->attrs.str = (char *)p;
            return 0;
        }
        p++;
    }

    info = platform_api->load_widget_info((void *)text->info, 0xff);
    list = platform_api->load_text_list(text->elm.page, info->str);
    if (!list) {
        return -EINVAL;
    }

    p = &((u16 *)list->str)[2];
    for (i = 2; i < text->str_num; i++) {
        if (i == index) {
            text->_str[2] = *p;
            text->attrs.str = (char *)&text->_str[2];
            return 0;
        }
        p++;
    }

    return -EINVAL;
}

/*
 * 把多个字符串 id 拼成一串放进调用方给的 store_buf, 再让 attrs.str 指向它。
 * @note store_buf 必须是全局/静态(不能是局部), 大小为 index_num + 1。
 */
int ui_text_set_combine_index(struct ui_text *text, u16 *store_buf,
                              u8 *index_buf, int index_num)
{
    struct ui_text_info *info;
    struct ui_text_list *list;
    int i;

    info = platform_api->load_widget_info((void *)text->info, 0xff);
    list = platform_api->load_text_list(text->elm.page, info->str);
    if (!list) {
        return -EINVAL;
    }

    memset(store_buf, 0, index_num * 2 + 2);

    for (i = 0; i < index_num; i++) {
        if (index_buf[i] >= list->num) {
            return -EINVAL;
        }
        store_buf[i] = ((u16 *)list->str)[index_buf[i]];
    }

    text->attrs.str = (char *)store_buf;

    return 0;
}

int ui_text_show_index_by_id(int id, int index)
{
    struct ui_text *text = (struct ui_text *)ui_core_get_element_by_id(id);
    int ret;

    if (!text) {
        return -EINVAL;
    }

    ret = ui_text_set_index(text, index);
    if (ret) {
        return ret;
    }

    if (text->elm.css.invisible) {
        ui_core_show(text, 0);
    } else {
        ui_core_redraw(text);
    }

    return 0;
}

/*
 * @note 下面三个 set_*str 的差别只在 attrs 的位域怎么写:
 *   set_str      encode=0 endian=0 flags=flags  (整字节覆盖)
 *   set_wstr     encode=1 endian=endian flags=flags
 *   set_utf8_str encode=2 flags=flags, endian【保留原值】(IR 为读-改-写)
 */
int ui_text_set_str(struct ui_text *text, const char *format, const char *str,
                    int strlen, u32 flags)
{
    if (!text) {
        return -EINVAL;
    }

    text->attrs.offset = 0;
    text->attrs.format = format;
    text->attrs.str    = str;
    text->attrs.strlen = strlen;
    text->attrs.encode = 0;
    text->attrs.endian = 0;
    text->attrs.flags  = flags;

    return 0;
}

int ui_text_set_wstr(struct ui_text *text, const char *format, const char *str,
                     int strlen, int endian, u32 flags)
{
    if (!text) {
        return -EINVAL;
    }

    text->attrs.offset = 0;
    text->attrs.format = format;
    text->attrs.str    = str;
    text->attrs.strlen = strlen;
    text->attrs.encode = 1;
    text->attrs.endian = endian;
    text->attrs.flags  = flags;

    return 0;
}

int ui_text_set_utf8_str(struct ui_text *text, const char *format,
                         const char *str, int strlen, u32 flags)
{
    if (!text) {
        return -EINVAL;
    }

    text->attrs.offset = 0;
    text->attrs.format = format;
    text->attrs.str    = str;
    text->attrs.strlen = strlen;
    text->attrs.encode = 2;
    text->attrs.flags  = flags;

    return 0;
}

int ui_text_set_str_by_id(int id, const char *format, const char *str)
{
    struct ui_text *text = (struct ui_text *)ui_core_get_element_by_id(id);

    if (!text) {
        return -EINVAL;
    }

    ui_text_set_str(text, format, str, 0, 0);

    if (!text->elm.css.invisible) {
        ui_core_redraw(text);
    }

    return 0;
}

/* @note 这三个 *_by_id 返回的是 attrs.displen(排版后实际显示长度), 不是 0 */
int ui_text_set_text_by_id(int id, const char *str, int strlen, u32 flags)
{
    struct ui_text *text = (struct ui_text *)ui_core_get_element_by_id(id);

    if (!text) {
        return -EINVAL;
    }

    ui_text_set_str(text, "text", str, strlen, flags);

    if (!text->elm.css.invisible) {
        ui_core_redraw(text);
    }

    return text->attrs.displen;
}

int ui_text_set_textw_by_id(int id, const char *str, int strlen, int endian,
                            u32 flags)
{
    struct ui_text *text = (struct ui_text *)ui_core_get_element_by_id(id);

    if (!text) {
        return -EINVAL;
    }

    ui_text_set_wstr(text, "text", str, strlen, endian, flags);

    if (!text->elm.css.invisible) {
        ui_core_redraw(text);
    }

    return text->attrs.displen;
}

int ui_text_set_textu_by_id(int id, const char *str, int strlen, u32 flags)
{
    struct ui_text *text = (struct ui_text *)ui_core_get_element_by_id(id);

    if (!text) {
        return -EINVAL;
    }

    ui_text_set_utf8_str(text, "text", str, strlen, flags);

    if (!text->elm.css.invisible) {
        ui_core_redraw(text);
    }

    return text->attrs.displen;
}

void ui_text_set_text_attrs(struct ui_text *text, const char *str, int strlen,
                            u8 encode, u8 endian, u32 flags)
{
    text->attrs.str    = str;
    text->attrs.strlen = strlen;
    text->attrs.encode = encode;
    text->attrs.endian = endian;
    text->attrs.flags  = flags;
}

int ui_text_set_hide_by_id(int id, int hide)
{
    struct element *elm = ui_core_get_element_by_id(id);

    if (!elm) {
        return -EINVAL;
    }

    elm->css.invisible = hide;

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_text_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_TEXT)
.new = new_ui_text,
};

/*
 * 原库缺陷清单 + 加固状态(描述的是【原库】行为; 方括号是当前处理结果,
 * 差异已登记在 accept/ 并锁定指纹)。
 *
 *  [已修] struct ui_text 的 timer 字段是 u16, 存的却是 set_timer 返回的
 *         【指针】(IR 为 ptrtoint 后 trunc 到 i16, del_timer 时再 zext 回去)。
 *         目前碰巧安全 —— platform 层的 jlui_set_timer 返回的其实是
 *         sys_timer_add() 的定时器 ID 转成的伪指针, 而那是个小整数。但这等于
 *         依赖"ID 永远不超过 16 位"这个未文档化的假设。字段已改为 void *,
 *         三处收发也不再做 trunc / zext。
 *  struct ui_text 的 timer 字段是 u16, 但存的是 set_timer 返回的【指针】
 *  (IR 为 ptrtoint 后 trunc 到 i16, del_timer 时再 zext 回指针)。
 *  指针被截断成 16 位, 只在定时器句柄恰好落在低 16 位时才正确。
 */
