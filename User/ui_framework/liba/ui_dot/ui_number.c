/*
 * ui_number.c —— 数字控件(曲目号、音量数值等)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_number.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_number.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_number.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   number_vsprintf@19  number_update@119  number_highlight@132
 *   number_onchange@140  number_onkey@176  number_ontouch@189
 *   new_ui_number@210  ui_number_update@248  ui_number_update_by_id@265
 *   ui_number_enable@293
 *
 *   number_update / number_highlight 在原库已被内联(无独立 define)。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_number: text=0(element_text, 92 字节) source=92 number[2]=100
 *                     buf[20]=104 color=144 hi_color=148 css_num=152
 *                     位域(nums:6,type:2)=153 css[2]=156 num_str=164
 *                     info=168 handler=172, sizeof=176
 *   struct ui_number_info: head=0 source=16 format=24 color=40 hi_color=44
 *                          number[10]=48 delimiter[10]=68 space[2]=88 action=92
 *
 * 【位域小技巧】nums 占低 6 位、type 占高 2 位, 所以"整字节 < 64"就等价于
 *   type == TYPE_NUM(0), IR 里到处是 icmp ult i8 x, 64, 就是这个判断。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_number.data.bss")
#pragma data_seg(".ui_number.data")
#pragma const_seg(".ui_number.text.const")
#pragma code_seg(".ui_number.text")
#endif

#include "ui/ui_number.h"
#include "jl_debug.h"    /* ASSERT / log_*: 原厂靠别处间接带入, 这里补成自包含 */

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
     * @note 原库这里只开了 2 字节且【不判上界】, 格式串里超过 2 个占位符就
     *       越界写栈。数组大小保持 2 不变(下面的 switch 也只支持 1、2 两种),
     *       但循环里【已补上界检查】, 见下方加固注释。
     */
    u8 pos[2] = {0};
    /*
     * @note fmt 必须是 u8* 而不是 char* —— switch (*fmt) 会做整型提升,
     *       signed char 走 sext 之后 LLVM 收不回 i8, 原厂是 switch i8。
     */
    const u8 *fmt = (const u8 *)info->format;
    /*
     * @note 原厂这里是【两个各自独立的计数器】: 一个做 pos[] 下标, 一个最后
     *       存进 number->nums(IR 里 b3 有两个 i8 phi, 值始终相同)。写成一个
     *       变量少一个 phi, 对不上。
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
             * @note 内层扫描必须写成显式 switch —— 原厂 IR 里是一条 3 路
             *       switch(0 / '%' / 其他)。写成 while (*fmt && *fmt != '%')
             *       或两个独立 if, clang 都只生成两次 icmp + 分支, 对不上。
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
             * 三种分支各自独立判错(不是用 && 串成一条链) —— 例如 "%0x" 会直接
             * 报错, 而不会退回去按 "%Nd" 再试一次。参考 IR 的 CFG 就是这样。
             */
            /*
             * 加固【越界写栈】: pos[] 只有 2 个元素, 而原库【不判上界】——
             * 格式串里写第三个占位符, pos[2] 就写到栈上别的东西头上了。
             * 下面那个 switch 本来也只处理 nums == 1 / 2 两种情况, 所以
             * 超出的一律当"不支持的格式"处理。
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

    /* 两个赋值顺序照抄原厂(先 i+1 再 i) */
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

    /* 条件写成"有字模表"在前, 与原厂的基本块排布一致 */
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

    /* case 顺序照抄原厂(RELEASE 在 HIGHLIGHT 之前), 换顺序会改变基本块的排布 */
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
     * 加固: 原库【没有 memset】(ui_battery / ui_pic 等模块都有)。
     * ui_core_malloc 给的是未初始化内存, 而 nums / type / number[] / buf[]
     * 这些字段要到首次 ui_number_update 才被填上 —— 若 ON_CHANGE_SHOW_PROBE
     * 先于 update 到来, 拿去画的就是随机内容。
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

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
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
 *       ui_number_update 是直接返回 -EINVAL。原库如此。
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
 * 原库缺陷清单 + 加固状态(描述的是【原库】行为; 方括号是当前处理结果,
 * 差异已登记在 accept/ 并锁定指纹)。
 *
 *  [已修] 1. number_vsprintf 的 pos[2] 只够两个占位符, 而原库【不判上界】,
 *            格式串里写第三个 %d 就越界写栈。数组大小保持 2(下面的 switch 也
 *            只支持 1、2 两种), 循环里补了上界检查, 超出按"不支持的格式"处理。
 *  [已修] 2. new_ui_number 没有 memset(ui_time 也一样) —— ui_core_malloc 给的
 *            是未初始化内存, 而 nums/type/number[]/buf[] 要到首次
 *            ui_number_update 才被填上, 若 ON_CHANGE_SHOW_PROBE 先于 update
 *            到来, 拿去画的就是随机内容。两个文件都补了 memset。
 *  1. number_vsprintf 的 pos[2] 只够两个占位符, 格式串里写第三个 %d 就越界写栈。
 *  2. new_ui_number 没有 memset(ui_time 也一样), nums/type/number[]/buf[] 在
 *     首次 ui_number_update 之前是未初始化的, 而 ON_CHANGE_SHOW_PROBE 可能
 *     先于 update 到来。
 */
