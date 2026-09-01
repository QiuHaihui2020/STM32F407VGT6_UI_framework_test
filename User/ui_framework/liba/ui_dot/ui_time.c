/*
 * ui_time.c —— 时间控件(MUSIC 页的播放时间/总时长、时钟页的时间都走它)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_time.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_time.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_time.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   time_vsprintf@19  __is_leap_year@90  ui_core_get_rtc_time@96
 *   ui_time_tick@101  time_update@143  time_highlight@157  time_onchange@168
 *   time_onkey@212  time_ontouch@225  new_ui_time@245  ui_time_update@289
 *   ui_time_update_by_id@301  ui_time_enable@323
 *
 *   __is_leap_year / time_update / time_highlight 在原库已被内联(无独立 define),
 *   形参名取自 DWARF: __is_leap_year(year) / time_highlight(time, yes)。
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_time: text=0(element_text, 92 字节) source=92 位域(year:12,month:4)=100
 *                   day=102 hour=103 min=104 sec=105 css_num=106 auto_cnt=107
 *                   css[2]=108 color=116 hi_color=120 buf[20]=124 timer=164
 *                   info=168 handler=172, sizeof=176
 *   struct ui_time_info: head=0 source=16 auto_cnt=24 format=28 color=44
 *                        hi_color=48 number[10]=52 delimiter[10]=72 action=92
 *
 * 【两层 handler 不要搞混】
 *   time->text.handler = &time_event_handler  —— 本模块自己的处理(经 ui_p 分发)
 *   time->handler      = 业务层注册的 handler  —— 由 element_event_handler_for_id 查得
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_time.data.bss")
#pragma data_seg(".ui_time.data")
#pragma const_seg(".ui_time.text.const")
#pragma code_seg(".ui_time.text")
#endif

#include "ui/ui_time.h"
#include "jl_ascii.h"

static const u16 leap_month_table[12] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * 按 info->format 里的占位符拼出时间串, 再按 info->number[]/delimiter[] 换成字模索引。
 *
 * @note 输出有两种形态, 由 info->number[0] 决定:
 *   number[0] 为 0 或 0xffff —— 无字模表, 直接把 ASCII 串拷进 buf(配 "ascii" 格式);
 *   否则                     —— 逐字符查表换成图片索引(配 "image" 格式),
 *                               数字查 number[], 非数字按出现顺序查 delimiter[]。
 *   查表遇到 0xffff 表示表结束, 提前收尾。
 */
static void time_vsprintf(struct ui_time *time, struct ui_time_info *info, u16 *buf)
{
    /*
     * @note str 必须是 u8 而不是 char —— 后面 str[i] - '0' 取字模下标时,
     *       原厂是 zext(无符号提升), 用 char 会变成 sext, 与原厂对不上。
     */
    u8 str[64];
    u8 *p = str;
    const char *fmt = info->format;
    int i, j = 0;
    int len;
    u16 img;

    while (*fmt) {
        switch (*fmt) {
        case 'Y':
            ASCII_IntToStr(p, time->year, 4, 4);
            p += 4;
            break;
        case 'M':
            ASCII_IntToStr(p, time->month, 2, 2);
            p += 2;
            break;
        case 'D':
            ASCII_IntToStr(p, time->day, 2, 2);
            p += 2;
            break;
        case 'h':
            ASCII_IntToStr(p, time->hour, 2, 2);
            p += 2;
            break;
        case 'm':
            ASCII_IntToStr(p, time->min, 2, 2);
            p += 2;
            break;
        case 's':
            ASCII_IntToStr(p, time->sec, 2, 2);
            p += 2;
            break;
        default:
            *p++ = *fmt;
            break;
        }
        fmt++;
    }

    len = p - str;

    if (info->number[0] == 0 || info->number[0] == 0xffff) {
        memcpy(buf, str, len);
        return;
    }

    for (i = 0; i < len; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            img = info->number[str[i] - '0'];
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

    /* 两个赋值顺序照抄原厂(先 i+1 再 i), 顺序反过来 IR 的 store 次序就对不上 */
    buf[i + 1] = 0xff;
    buf[i] = 0xff;
}

/*
 * @note 这里刻意用位运算 & | 而不是 && || —— 原厂编出来的是【无分支】形式
 *       (三个 urem 与 month==2 全部无条件求值后用 i1 的 and/or 合并), -Oz 下
 *       不带分支更省体积。写成 && || 会产生短路分支, 与原厂对不上。
 *       两种写法结果相同(操作数都无副作用), 只是求值时机不同。
 */
static int __is_leap_year(u32 year)
{
    return ((year % 4 == 0) & (year % 100 != 0)) | (year % 400 == 0);
}

/*
 * 取 RTC 时间。弱符号 —— 业务层可覆盖它来接真实 RTC;
 * 未覆盖时返回 -1, 控件退化为自己按秒累加。
 */
__attribute__((weak))
int ui_core_get_rtc_time(struct ui_time *time)
{
    return -1;
}

/* 1 秒定时回调 */
static void ui_time_tick(void *_elm)
{
    struct ui_time *time = (struct ui_time *)_elm;
    int days;

    if (!time->timer) {
        puts("ui_timer should be killed\n");
        return;
    }

    /* source 为 "rtc" 且取到了真实时间, 就不用自己累加 */
    if (!strcmp(time->source, "rtc")) {
        if (ui_core_get_rtc_time(time) == 0) {
            ui_core_redraw(time);
            return;
        }
    }

    time->sec++;
    if (time->sec > 59) {
        time->sec = 0;
        time->min++;
        if (time->min > 59) {
            time->min = 0;
            time->hour++;
            if (time->hour > 23) {
                time->hour = 0;
                /* 同上, 用 & 保持无分支形式; 且闰年判断写在前面以对齐原厂的求值顺序 */
                if (!__is_leap_year(time->year) & (time->month == 2)) {
                    days = 28;
                } else {
                    days = leap_month_table[time->month - 1];
                }
                time->day++;
                if (time->day > days) {
                    time->day = 1;
                    time->month++;
                    if (time->month > 12) {
                        time->month = 1;
                        time->year++;
                        if (time->year > 2100) {
                            time->year = 2020;
                        }
                    }
                }
            }
        }
    }

    ui_core_redraw(time);
}

/*
 * @note "ascii"/"image" 两个分支各写了一份 text_element_set_text 调用, 不是把格式
 *       串三目一下再统一调 —— 参考 IR 里是两次独立的 call, 合并写法只会有一次。
 */
static void time_update(struct ui_time *time)
{
    struct ui_time_info *info;

    info = platform_api->load_widget_info((void *)time->info, 0xff);

    time_vsprintf(time, info, time->buf);

    /* 条件写成"有字模表"在前, 与原厂的基本块排布一致(取反会把两块顺序换掉) */
    if (info->number[0] != 0 && info->number[0] != 0xffff) {
        text_element_set_text(&time->text, (char *)time->buf, "image",
                              time->text.elm.highlight ? time->hi_color : time->color);
    } else {
        text_element_set_text(&time->text, (char *)time->buf, "ascii",
                              time->text.elm.highlight ? time->hi_color : time->color);
    }
}

static void time_highlight(struct ui_time *time, int yes)
{
    if (time->css_num > 1) {
        ui_core_set_element_css(time,
            platform_api->load_css(time->text.elm.page,
                                   (void *)time->css[yes ? 1 : 0]));
    }
}

static int time_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_time *time = (struct ui_time *)_elm;

    if (time->handler && time->handler->onchange) {
        if (time->handler->onchange(time, event, arg)) {
            if (event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
                return true;
            }
        }
    }

    switch (event) {
    case ON_CHANGE_SHOW_PROBE:
        time_update(time);
        break;
    case ON_CHANGE_SHOW_POST:
        /* auto_cnt 非 0 才起定时器 —— 静态显示的时间控件不需要它 */
        if (!time->timer && time->auto_cnt) {
            time->timer = platform_api->set_timer(time, ui_time_tick, 1000);
        }
        break;
    case ON_CHANGE_HIDE:
    case ON_CHANGE_RELEASE_PROBE:
        if (time->timer) {
            platform_api->del_timer(time->timer);
            time->timer = NULL;
        }
        break;
    case ON_CHANGE_RELEASE:
        ui_core_remove_element(time);
        ui_core_free(time);
        break;
    case ON_CHANGE_HIGHLIGHT:
        time_highlight(time, (int)arg);
        break;
    default:
        break;
    }

    return true;
}

static int time_onkey(void *_elm, struct element_key_event *e)
{
    struct ui_time *time = (struct ui_time *)_elm;

    if (time->handler->onkey) {
        if (time->handler->onkey(time, e)) {
            return true;
        }
    }

    return false;
}

static int time_ontouch(void *_elm, struct element_touch_event *e)
{
    struct ui_time *time = (struct ui_time *)_elm;

    if (time->handler->ontouch) {
        if (time->handler->ontouch(time, e)) {
            return true;
        }
    }

    return false;
}

static const struct element_event_handler time_event_handler = {
    .id       = 0,
    .ontouch  = time_ontouch,
    .onkey    = time_onkey,
    .onchange = time_onchange,
};

void *new_ui_time(const void *_info, struct element *parent)
{
    struct ui_time *time;
    struct ui_time_info *info;
    struct element_css1 *css;

    time = ui_core_malloc(sizeof(struct ui_time));
    if (!time) {
        return NULL;
    }

    /*
     * 加固: 原库【没有 memset】(ui_battery / ui_pic 等模块都有)。
     * ui_core_malloc 给的是未初始化内存, 而 nums / type / number[] / buf[]
     * 这些字段要到首次 ui_number_update 才被填上 —— 若 ON_CHANGE_SHOW_PROBE
     * 先于 update 到来, 拿去画的就是随机内容。
     */
    memset(time, 0, sizeof(struct ui_time));

    info = platform_api->load_widget_info((void *)_info, 0xff);

    strcpy(time->source, info->source);
    time->info     = _info;
    time->color    = info->color & 0xffffff;
    time->hi_color = info->hi_color & 0xffffff;
    time->auto_cnt = info->auto_cnt;
    time->css_num  = info->head.css_num;
    time->css[0]   = (u32)info->head.css;
    time->css[1]   = (u32)(info->head.css + 1);

    if (time->auto_cnt && !strcmp(time->source, "rtc")) {
        ui_core_get_rtc_time(time);
    }

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
    text_element_init(&time->text, info->head.id, info->head.page,
                      (u8)((u32)info->head.css >> 29), css, info->action);
    text_element_set_event_handler(&time->text, time, &time_event_handler);
    ui_core_element_append_child(parent, &time->text.elm);

    time->handler = element_event_handler_for_id(info->head.id);
    if (!time->handler) {
        time->handler = &dumy_handler;
    }
    if (time->handler->onchange) {
        time->handler->onchange(time, ON_CHANGE_INIT, NULL);
    }

    return time;
}

int ui_time_update(struct ui_time *time, struct utime *t)
{
    time->year  = t->year;
    time->month = t->month;
    time->day   = t->day;
    time->hour  = t->hour;
    time->min   = t->min;
    time->sec   = t->sec;

    return 0;
}

int ui_time_update_by_id(int id, struct utime *time)
{
    struct ui_time *t = (struct ui_time *)ui_core_get_element_by_id(id);

    if (!t) {
        return -EINVAL;
    }

    t->year  = time->year;
    t->month = time->month;
    t->day   = time->day;
    t->hour  = time->hour;
    t->min   = time->min;
    t->sec   = time->sec;

    if (!t->text.elm.css.invisible) {
        ui_core_redraw(t);
    }

    return 0;
}

/* 空函数, 供业务层显式引用以把本模块链进来(控件工厂注册才会生效) */
void ui_time_enable()
{
}

REGISTER_CONTROL_OPS(CTRL_TYPE_TIME)
.new = new_ui_time,
};
