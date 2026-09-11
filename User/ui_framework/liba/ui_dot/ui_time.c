/*
 * ui_time.c —— 时间控件(MUSIC 页的播放时间/总时长、时钟页的时间都走它)
 *
 * 【结构体布局】改字段前先看这里, 控件是按偏移访问的
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
     * @note str 必须是 u8 而不是 char —— 后面用 str[i] - '0' 当字模下标,
     *       char 在本目标上有符号, 分隔符之类 >= 0x80 的字节会算出负下标。
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

    /* 末尾补两个 0xff 结束标记: 先写 i+1 再写 i, 这样即使上面的循环是 break
     * 出来的(buf[i] 已被写成 0xff), 也不会把它覆盖掉 */
    buf[i + 1] = 0xff;
    buf[i] = 0xff;
}

/*
 * @note 这里刻意用位运算 & | 而不是 && || —— 三个取模都没有副作用, 无条件
 *       求值再合并可以编成【无分支】形式, -Oz 下比短路分支更省体积。
 *       两种写法结果相同, 只是求值时机不同。
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
                /* 同上, 用 & 保持无分支形式 */
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
 * @note "ascii" / "image" 两个分支各写一份 text_element_set_text 调用, 没有
 *       把格式串三目一下再统一调 —— 这样两条路径各自看得清楚, 改一条不影响
 *       另一条。
 */
static void time_update(struct ui_time *time)
{
    struct ui_time_info *info;

    info = platform_api->load_widget_info((void *)time->info, 0xff);

    time_vsprintf(time, info, time->buf);

    /* 条件写成"有字模表"在前: 带字模的是常见情况, 放前面更直观 */
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
     * 【必须先 memset】ui_core_malloc 给的是未初始化内存, 而 buf[] / 各时间
     * 字段要到首次 update 才被填上 —— 若 ON_CHANGE_SHOW_PROBE 先于 update
     * 到来, 拿去画的就是随机内容。
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

    /* prj(资源工程号)打包在 css 指针的高 3 位里, 取出来要右移 29 */
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

/*
 * 实现注意事项
 *
 *  1) 【time_onkey / time_ontouch 只判了回调指针】没有再判 time->handler 本身 ——
 *     new_ui_time 已把它兜底成 dumy_handler。若将来加了别的构造路径, 记得
 *     把这两处也补成两层判断(参考 ui_pic.c)。
 *
 *  2) 【两层 handler 不要搞混】time->text.handler 是本模块自己的处理入口
 *     (经 ui_p 分发), time->handler 才是业务层注册的那个。
 *
 *  3) 【定时器只在 auto_cnt 非 0 时起】静态显示的时间控件不需要秒级刷新;
 *     HIDE / RELEASE_PROBE 时要记得删掉, 否则控件释放后定时器还在跑。
 *
 *  4) 【source == "rtc" 时优先取真实时间】ui_core_get_rtc_time 是弱符号,
 *     业务层可以覆盖; 没覆盖时返回 -1, 控件退化为自己按秒累加。
 */
