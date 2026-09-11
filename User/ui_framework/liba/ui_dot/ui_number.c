/*
 * ui_number.c —— 数字控件(曲目号、音量数值等)
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
 *   struct ui_number: text=0(element_text, 92 字节) source=92 number[2]=100
 *                     buf[20]=104 color=144 hi_color=148 css_num=152
 *                     位域(nums:6,type:2)=153 css[2]=156 num_str=164
 *                     info=168 handler=172, sizeof=176
 *   struct ui_number_info: head=0 source=16 format=24 color=40 hi_color=44
 *                          number[10]=48 delimiter[10]=68 space[2]=88 action=92
 *
 * 【位域小技巧】nums 占低 6 位、type 占高 2 位, 所以"整字节 < 64"就等价于
 *   type == TYPE_NUM(0) —— 判 type 时可以少一次移位。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_number.data.bss")
#pragma data_seg(".ui_number.data")
#pragma const_seg(".ui_number.text.const")
#pragma code_seg(".ui_number.text")
#endif

#include "ui/ui_number.h"
#include "jl_debug.h"    /* ASSERT / log_*: 显式包含, 保证本文件自包含 */

/*
 * 按 info->format 拼出数字串, 再按 info->number[]/delimiter[]/space[] 换成字模索引。
 *
 * 支持的格式占位符只有三种: %0Nd / %Nd / %d, 其余一律打印告警后直接返回。
 * N 用来限制取模范围(pos[] 记下 N, 取值时做 number % range[N])。
 */
static void number_vsprintf(struct ui_number *number,
                            struct ui_number_info *info, u16 *buf)
{
    /* 函数内静态表: range[N] = 10^N, 用于 %0Nd 的取模 */
    static const u32 range[10] = {
        0, 10, 100, 1000, 10000,
        100000, 1000000, 10000000, 100000000, 1000000000
    };
    char str[32] = {0};
    /*
     * @note pos[] 只有 2 个元素(下面的 switch 也只支持 1、2 两个占位符),
     *       所以循环里【必须判上界】, 见下方说明 —— 否则格式串里写第三个
     *       占位符就是越界写栈。
     */
    u8 pos[2] = {0};
    /*
     * @note fmt 必须是 u8* 而不是 char* —— switch (*fmt) 会做整型提升,
     *       char 在本目标上有符号, 格式串里 >= 0x80 的字节会变成负值。
     */
    const u8 *fmt = (const u8 *)info->format;
    /*
     * @note 两个计数器分开: num 做 pos[] 的下标(受数组上界约束), nums 最后
     *       存进 number->nums。目前两者同步增长, 分开写是为了将来扩 pos[]
     *       时不至于混淆。
     */
    u8 num = 0;
    u8 nums = 0;
    int len;
    int i, j = 0;
    u16 img;
    u8 c;

    if (number->type == TYPE_NUM) {
        while (1) {
            /*
             * @note 内层扫描写成显式 switch(0 / '%' / 其他三路), 比
             *       while (*fmt && *fmt != '%') 更直白: 遇 0 收尾、遇 '%'
             *       交给下面解析、其余跳过。
             */
            for (;;) {
                switch (*fmt) {
                case 0:
                    goto parse_done;
                case '%':
                    break;
                default:
                    fmt++;
                    continue;
                }
                break;      /* 跳出 for: 此时 *fmt == '%' */
            }
            /*
             * 三种形态各自独立判错(不是用 && 串成一条链) —— 例如 "%0x" 直接
             * 报错, 不会退回去按 "%Nd" 再试一次。
             */
            /*
             * 【必须判上界】pos[] 只有 2 个元素, 格式串里写第三个占位符时
             * pos[2] 就写到栈上别的东西头上了。下面那个 switch 本来也只处理
             * 1 / 2 两种情况, 所以超出的一律当"不支持的格式"。
             */
            if (num >= (u8)(sizeof(pos) / sizeof(pos[0]))) {
                goto unsupported;
            }

            if (fmt[1] == '0') {
                if (fmt[2] < '1' || fmt[2] > '9' || fmt[3] != 'd') {
                    goto unsupported;
                }
                pos[num] = fmt[2] - '0';
            } else if (fmt[1] >= '1' && fmt[1] <= '9') {
                if (fmt[2] != 'd') {
                    goto unsupported;
                }
                pos[num] = fmt[1] - '0';
            } else if (fmt[1] == 'd') {
                pos[num] = 9;
            } else {
                goto unsupported;
            }
            num++;
            nums++;
            fmt++;
        }

parse_done:
        number->nums = nums;

        switch (number->nums) {
        case 1:
            sprintf(str, info->format, number->number[0] % range[pos[0]]);
            break;
        case 2:
            sprintf(str, info->format,
                    number->number[0] % range[pos[0]],
                    number->number[1] % range[pos[1]]);
            break;
        default:
            break;
        }
    } else {
        strcpy(str, (char *)number->num_str);
    }

    len = strlen(str);
    ASSERT(len < 20);

    if (info->number[0] == 0 || info->number[0] == 0xffff) {
        /* 无字模表: 直接出 ASCII。数字型按 len 拷, 字符串型按 C 串拷 */
        if (number->type == TYPE_NUM) {
            memcpy(buf, str, len);
        } else {
            strcpy((char *)buf, str);
        }
        return;
    }

    for (i = 0; i < len; i++) {
        c = str[i];
        if (c == ' ') {
            img = info->space[0];
            if (img == 0xffff) {
                buf[i] = 0xff;
                break;
            }
        } else if (c >= '0' && c <= '9') {
            img = info->number[c - '0'];
            if (img == 0xffff) {
                buf[i] = 0xff;
                break;
            }
        } else {
            img = info->delimiter[j];
            if (img == 0xffff) {
                buf[i] = 0xff;
                break;
            }
            j++;
        }
        buf[i] = img;
    }

    /* 末尾补两个 0xff 结束标记: 先写 i+1 再写 i, 这样即使上面的循环是 break
     * 出来的(buf[i] 已被写成 0xff), 也不会把它覆盖掉 */
    buf[i + 1] = 0xff;
    buf[i] = 0xff;
    return;

unsupported:
    printf("the format %s not support yet!\n", info->format);
}

static void number_update(struct ui_number *number)
{
    struct ui_number_info *info;

    info = platform_api->load_widget_info((void *)number->info, 0xff);

    number_vsprintf(number, info, number->buf);

    /* 条件写成"有字模表"在前: 带字模的是常见情况, 放前面更直观 */
    if (info->number[0] != 0 && info->number[0] != 0xffff) {
        text_element_set_text(&number->text, (char *)number->buf, "image",
                              number->text.elm.highlight ? number->hi_color
                                                         : number->color);
    } else {
        text_element_set_text(&number->text, (char *)number->buf, "ascii",
                              number->text.elm.highlight ? number->hi_color
                                                         : number->color);
    }
}

static void number_highlight(struct ui_number *number, int yes)
{
    if (number->css_num > 1) {
        ui_core_set_element_css(number,
            platform_api->load_css(number->text.elm.page,
                                   (void *)number->css[yes ? 1 : 0]));
    }
}

static int number_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_number *number = (struct ui_number *)_elm;

    if (number->handler && number->handler->onchange) {
        if (number->handler->onchange(number, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    /* case 的先后与 enum 的声明顺序一致, 便于对照 element_change_event */
    switch (event) {
    case ON_CHANGE_SHOW_PROBE:
        number_update(number);
        break;
    case ON_CHANGE_RELEASE:
        ui_core_remove_element(number);
        ui_core_free(number);
        break;
    case ON_CHANGE_HIGHLIGHT:
        number_highlight(number, (int)arg);
        break;
    default:
        break;
    }

    return true;
}

static int number_onkey(void *_elm, struct element_key_event *e)
{
    struct ui_number *number = (struct ui_number *)_elm;

    if (number->handler->onkey) {
        if (number->handler->onkey(number, e)) {
            return true;
        }
    }

    return false;
}

static int number_ontouch(void *_elm, struct element_touch_event *e)
{
    struct ui_number *number = (struct ui_number *)_elm;

    if (number->handler->ontouch) {
        if (number->handler->ontouch(number, e)) {
            return true;
        }
    }

    return false;
}

static const struct element_event_handler number_event_handler = {
    .id       = 0,
    .ontouch  = number_ontouch,
    .onkey    = number_onkey,
    .onchange = number_onchange,
};

void *new_ui_number(const void *_info, struct element *parent)
{
    struct ui_number *number;
    struct ui_number_info *info;
    struct element_css1 *css;

    number = ui_core_malloc(sizeof(struct ui_number));
    if (!number) {
        return NULL;
    }

    /*
     * 【必须先 memset】ui_core_malloc 给的是未初始化内存, 而 nums / type /
     * number[] / buf[] 要到首次 ui_number_update 才被填上 —— 若
     * ON_CHANGE_SHOW_PROBE 先于 update 到来, 拿去画的就是随机内容。
     */
    memset(number, 0, sizeof(struct ui_number));

    info = platform_api->load_widget_info((void *)_info, 0xff);

    strcpy(number->source, info->source);
    number->info     = _info;
    number->color    = info->color & 0xffffff;
    number->hi_color = info->hi_color & 0xffffff;
    number->css_num  = info->head.css_num;
    number->css[0]   = (u32)info->head.css;
    number->css[1]   = (u32)(info->head.css + 1);

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
    text_element_init(&number->text, info->head.id, info->head.page,
                      (u8)((u32)info->head.css >> 29), css, info->action);
    text_element_set_event_handler(&number->text, number, &number_event_handler);
    ui_core_element_append_child(parent, &number->text.elm);

    number->handler = element_event_handler_for_id(info->head.id);
    if (!number->handler) {
        number->handler = &dumy_handler;
    }
    if (number->handler->onchange) {
        number->handler->onchange(number, ON_CHANGE_INIT, NULL);
    }

    return number;
}

int ui_number_update(struct ui_number *number, struct unumber *n)
{
    switch (n->type) {
    case TYPE_NUM:
        number->nums      = n->numbs;
        number->number[0] = n->number[0];
        number->number[1] = n->number[1];
        break;
    case TYPE_STRING:
        number->num_str = n->num_str;
        break;
    default:
        puts("number type is invalid.Please Select TYPE_NUM or TYPE_STRING.");
        return -EINVAL;
    }

    number->type = n->type;

    return 0;
}

/*
 * @note 与 ui_number_update 的差别不只是多了 redraw: 非法 type 在这里是
 *       puts + ASSERT(0) 之后【继续往下走】(仍会写 type 并刷新), 而
 *       ui_number_update 直接返回 -EINVAL —— 前者是 by_id 调用方通常不看
 *       返回值, 至少要让界面刷新出来。
 */
int ui_number_update_by_id(int id, struct unumber *n)
{
    struct ui_number *number = (struct ui_number *)ui_core_get_element_by_id(id);

    if (!number) {
        return -EINVAL;
    }

    switch (n->type) {
    case TYPE_NUM:
        number->nums      = n->numbs;
        number->number[0] = n->number[0];
        number->number[1] = n->number[1];
        break;
    case TYPE_STRING:
        number->num_str = n->num_str;
        break;
    default:
        puts("number type is invalid.Please Select TYPE_NUM or TYPE_STRING.");
        ASSERT(0);
        break;
    }

    number->type = n->type;

    if (!number->text.elm.css.invisible) {
        ui_core_redraw(number);
    }

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_number_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_NUMBER)
.new = new_ui_number,
};

/*
 * 实现注意事项
 *
 *  1) 【pos[] 的上界必须判】它只有 2 个元素, 与下面 switch 支持的占位符个数
 *     对应。格式串里写第三个 %d 时若不挡住, 就是越界写栈。
 *
 *  2) 【new_ui_number 必须 memset】nums / type / number[] / buf[] 要到首次
 *     ui_number_update 才被填上, 而 ON_CHANGE_SHOW_PROBE 可能先到。
 *
 *  3) 【支持的格式只有 %0Nd / %Nd / %d】其余一律打印告警后返回, 不显示。
 *     N 用来限制取模范围(number % 10^N)。
 *
 *  4) 【两个 update 的失败行为不同】ui_number_update 遇非法 type 直接返回
 *     -EINVAL; ui_number_update_by_id 是 ASSERT 之后继续走完并刷新。
 */
