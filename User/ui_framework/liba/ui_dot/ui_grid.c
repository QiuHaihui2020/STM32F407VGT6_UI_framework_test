/*
 * ui_grid.c —— 网格控件(列表/菜单等)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_grid.c.o 还原。
 *   该库交付的是 LLVM bitcode 且保留完整调试信息, 故按 IR + DWARF 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_grid.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_grid.c
 *
 * 【函数原始行号(DISubprogram)】按此顺序排列, 便于与参考 IR 逐函数对照:
 *   item_highlight@52  ui_grid_set_item_num@111  ui_grid_update_by_id_dynamic@125
 *   ui_grid_set_slide_direction@159  ui_grid_set_hi_index@165
 *   ui_grid_set_pix_scroll@174  ui_grid_get_hindex@183
 *   ui_grid_set_hindex_dynamic@188  ui_grid_set_base_dynamic@206
 *   ui_grid_get_hindex_dynamic@217  ui_grid_cur_item_dynamic@223
 *   ui_grid_set_scroll_area@359  ui_grid_slide_with_callback_dynamic@365
 *   ui_grid_slide_with_callback@964  ui_grid_slide@1167
 *   ui_grid_dynamic_create@1200  ui_grid_dynamic_set_item_by_id@1216
 *   ui_grid_dynamic_reset@1238  ui_grid_dynamic_release@1272
 *   ui_grid_dynamic_set_prepare@1294  ui_grid_dynamic_cur_item@1316
 *   ui_grid_dynamic_slide@1342  ui_grid_add_dynamic@1580
 *   ui_grid_del_dynamic@1650  ui_grid_init_dynamic@1734
 *   ui_grid_add_dynamic_by_id@1779  ui_grid_del_dynamic_by_id@1786
 *   ui_grid_release@2723  ui_grid_child_init@2747
 *   ui_grid_highlight_child@2878  ui_grid_highlight_item@3069
 *   ui_grid_highlight_item_by_id@3117  ui_grid_enable@3211
 *   new_ui_grid@2949  ui_grid_on_focus@3000  ui_grid_lose_focus@3008
 *   ui_grid_state_reset@3016
 *
 * 【内部函数(define internal fastcc)】
 *   __grid_ajust         动态网格尺寸调整, 设置item坐标+invisible位
 *   grid_scroll_dynamic  动态网格高亮滚动(被ui_grid_highlight_child调用)
 *   grid_scroll          普通网格高亮滚动(被ui_grid_highlight_child调用)
 *
 * 【结构体偏移校验】(与 IR 中的 getelementptr 逐一吻合)
 *   struct ui_grid:
 *     elm=0  hi_index=1(+1,1byte)  touch_index=2(+1)  onfocus=3(+1)
 *     page_mode=4(+1)  slide_direction=5(+1)  col_num=6(+1)  row_num=7(+1)
 *     show_row=8(+1)  show_col=9(+1)  avail_item_num=10(+1)  pix_scroll=11(+1)
 *     ctrl_num=12(+1)  x_interval=13(+4)  y_interval=14(+4)
 *     max_show_left=15(+4)  max_show_top=16(+4)
 *     min_show_left=17(+4)  min_show_top=18(+4)
 *     max_left=19(+4)  max_top=20(+4)  min_left=21(+4)  min_top=22(+4)
 *     area=23(+4)  item=24(+4)  item_info=25(+4)  dynamic=26(+4)
 *     pos=27(+8)  dc=35(28bytes?)  info=?  handler=?
 *   sizeof(struct ui_grid_dynamic)=60bytes:
 *     dhi_index=0(+4)  dcol_num=4(+4)  drow_num=8(+4)
 *     min_row_index=12(+4)  max_row_index=16(+4)
 *     min_col_index=20(+4)  max_col_index=24(+4)
 *     min_show_row_index=28(+4)  max_show_row_index=32(+4)
 *     min_show_col_index=36(+4)  max_show_col_index=40(+4)
 *     grid_xval=44(+4)  grid_yval=48(+4)
 *     grid_col_num=52(+1)  grid_row_num=53(+1)
 *     grid_show_row=54(+1)  grid_show_col=55(+1)
 *     base_index_once=56(+4)
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_grid.data.bss")
#pragma data_seg(".ui_grid.data")
#pragma const_seg(".ui_grid.text.const")
#pragma code_seg(".ui_grid.text")
#endif

#include "ui/ui_grid.h"
#include "ui/ui.h"
#include "jl_ui_api.h"
#include "ui/control.h"
#include "jl_os_api.h"
#include "ui/layout.h"
#include <stdlib.h>
#include <errno.h>
#include "jl_debug.h"    /* ASSERT / log_*: 原厂靠别处间接带入, 这里补成自包含 */

static int grid_ontouch(void *_elm, struct element_touch_event *e);
static int grid_onkey(void *_elm, struct element_key_event *e);
static int grid_onchange(void *_elm, enum element_change_event event, void *arg);
static void *new_ui_grid(const void *_info, struct element *parent);


/* ------------------------------------------------------------
 *  链表版动态列表节点(sizeof = 32, 由 zalloc 分配)
 *  与 struct ui_grid_dynamic 那一套是两套并行实现:
 *  grid->dynamic != NULL 时用 ui_grid_dynamic, 否则查这条链表。
 * ------------------------------------------------------------ */
struct grid_dynamic {
    struct list_head entry;                         /* +0  */
    int list_total;                                 /* +8  列表总项数 */
    int offset;                                     /* +12 像素级滚动偏移(1 屏 = 10000) */
    int base_index;                                 /* +16 首个可见项在列表中的下标 */
    int (*event_handler_cb)(void *, int, int, int); /* +20 子控件刷新回调 */
    void *grid;                                     /* +24 所属 grid */
    int (*prepare_cb)(void *, int, int, int);       /* +28 滚动前预取回调 */
};

static struct list_head dynamic_head SEC(.ui_grid.data) = LIST_HEAD_INIT(dynamic_head);

const struct element_event_handler grid_elm_handler SEC(.ui_grid.text.const) = {
    .ontouch = grid_ontouch,
    .onkey = grid_onkey,
    .onchange = grid_onchange,
};

REGISTER_CONTROL_OPS(CTRL_TYPE_GRID)
.new = new_ui_grid,
};

static void __grid_ajust(struct ui_grid *grid, int new_row, int new_col);
static int grid_scroll_dynamic(struct ui_grid *grid, int index, u8 key_direction, u8 init);
static void grid_scroll(struct ui_grid *grid, int index, u8 key_direction, u8 init);
static void ui_grid_highlight_child(struct ui_grid *grid, int item, int init);
static void ui_grid_child_init(struct ui_grid *grid, struct ui_grid_info *info);

/* ============================================================
 *  item_highlight - line: 52
 *  注意第一个参数是 struct element*, 不是 struct layout*。
 *  原厂先把 css 的 left/top/width/height 存下来,
 *  调 ui_core_highlight_element 之后再写回去。
 * ============================================================ */
static void item_highlight(struct element *item, int yes)
{
    int top, left, width, height;

    top    = item->css.top;
    left   = item->css.left;
    width  = item->css.width;
    height = item->css.height;

    ui_core_highlight_element(item, yes);

    item->css.top    = top;
    item->css.left   = left;
    item->css.width  = width;
    item->css.height = height;
}

/* ============================================================
 *  ui_grid_set_item_num - line: 111
 * ============================================================ */
int ui_grid_set_item_num(struct ui_grid *grid, int item_num)
{
    struct ui_grid_info *info;

    info = platform_api->load_widget_info((void *)grid->info, 0xff);
    if (!info) {
        return -EINVAL;
    }

    ASSERT(item_num <= info->head.ctrl_num,
           ", item_num = %d, info->head.ctrl_num = %d, item_num must be less then grid ctrl_num.",
           item_num, info->head.ctrl_num);

    grid->ctrl_num = item_num;
    return 0;
}

/* ============================================================
 *  ui_grid_update_by_id_dynamic - line: 125
 * ============================================================ */
int ui_grid_update_by_id_dynamic(int id, int index, int redraw)
{
    struct element *elm;
    struct ui_grid *grid;
    struct ui_grid_dynamic *dynamic;
    int row, col, dindex;
    u8 grid_col_num, grid_row_num;

    elm = ui_core_get_element_by_id(id);
    grid = (struct ui_grid *)elm;
    dynamic = grid->dynamic;

    grid_col_num = dynamic->grid_col_num;
    row = index / grid_col_num;
    grid_row_num = dynamic->grid_row_num;
    row = row % grid_row_num;
    col = index % grid_col_num;

    row = dynamic->min_row_index + row;
    col = dynamic->min_col_index + col;
    dindex = row * dynamic->dcol_num + col;

    printf("byid index %d, cur_dindex %d\n", index, dindex);

    {
        list_for_each_child_element(elm, &grid->item[index].elm) {
            if (elm->handler && elm->handler->onchange) {
                elm->handler->onchange(elm, ON_CHANGE_UPDATE_ITEM, (void *)dindex);
            }
        }
    }

    if (redraw == 1) {
        ui_core_redraw(&grid->item[index].elm);
    }

    return 0;
}

/* ============================================================
 *  ui_grid_set_slide_direction - line: 159
 * ============================================================ */
int ui_grid_set_slide_direction(struct ui_grid *grid, int dir)
{
    grid->slide_direction = (u8)dir;
    return 0;
}

/* ============================================================
 *  ui_grid_set_hi_index - line: 165
 * ============================================================ */
int ui_grid_set_hi_index(struct ui_grid *grid, int hi_index)
{
    if (!grid->item) {
        return -1;
    }
    if (grid->avail_item_num > hi_index) {
        grid->hi_index = hi_index;
        return 0;
    }
    return -1;
}

/* ============================================================
 *  ui_grid_set_pix_scroll - line: 174
 * ============================================================ */
int ui_grid_set_pix_scroll(struct ui_grid *grid, int enable)
{
    if (!grid) {
        return -1;
    }
    grid->pix_scroll = enable;
    return 0;
}

/* ============================================================
 *  ui_grid_get_hindex - line: 183
 * ============================================================ */
int ui_grid_get_hindex(struct ui_grid *grid)
{
    return grid->hi_index;
}

/* ============================================================
 *  ui_grid_set_hindex_dynamic - line: 188
 * ============================================================ */
int ui_grid_set_hindex_dynamic(struct ui_grid *grid, int dhindex, int init, int hi_index)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;

    if (!dynamic) {
        return -1;
    }

    if (dynamic->drow_num * dynamic->dcol_num > dhindex) {
        dynamic->dhi_index = dhindex;
        if (init) {
            if (grid->avail_item_num > hi_index) {
                grid->hi_index = hi_index;
                return 0;
            }
            return -1;
        }
        return 0;
    }

    return -1;
}

/* ============================================================
 *  ui_grid_set_base_dynamic - line: 206
 * ============================================================ */
int ui_grid_set_base_dynamic(struct ui_grid *grid, u32 base_index_once)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;

    if (!dynamic) {
        return -1;
    }

    if (dynamic->drow_num * dynamic->dcol_num > base_index_once) {
        dynamic->base_index_once = base_index_once;
        return 0;
    }

    return -1;
}

/* ============================================================
 *  ui_grid_get_hindex_dynamic - line: 217
 * ============================================================ */
int ui_grid_get_hindex_dynamic(struct ui_grid *grid)
{
    return grid->dynamic->dhi_index;
}

/* ============================================================
 *  ui_grid_cur_item_dynamic - line: 223
 * ============================================================ */
int ui_grid_cur_item_dynamic(struct ui_grid *grid)
{
    struct ui_grid_dynamic *dynamic;
    int dhi_index, row, col;
    char touch_index;
    u8 grid_col_num, grid_row_num;

    dynamic = grid->dynamic;
    touch_index = grid->touch_index;

    if (touch_index >= 0) {
        grid_col_num = dynamic->grid_col_num;
        row = touch_index / grid_col_num;
        grid_row_num = dynamic->grid_row_num;
        row = row % grid_row_num;
        col = touch_index % grid_col_num;

        row = dynamic->min_row_index + row;
        col = dynamic->min_col_index + col;

        dhi_index = row * dynamic->dcol_num + col;
        return dhi_index;
    }

    return dynamic->dhi_index;
}

/* ============================================================
 *  ui_grid_set_scroll_area - line: 359
 * ============================================================ */
void ui_grid_set_scroll_area(struct ui_grid *grid, struct scroll_area *area)
{
    grid->area = area;
}

/* ============================================================
 *  __grid_ajust - 内部函数 (internal fastcc)
 *  动态网格尺寸调整:
 *  - 第一个item放在 (min_left, min_top)
 *  - 按网格布局计算每个item的left/top坐标
 *  - 根据grid_xval/grid_yval判断哪些item在可视区外, 设 invisible(bit 2)
 *  - 把最后计算出的可视行列写入 dynamic->grid_show_row / grid_show_col
 * ============================================================ */
static void __grid_ajust(struct ui_grid *grid, int new_row, int new_col)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    struct element_css *css;
    int item_width, item_height;
    int row_span;
    u32 left, top;
    int right, bottom;
    int first_top, first_left;
    int i, index;

    if (grid->row_num < new_row) {
        new_row = grid->row_num;
    }
    if (grid->col_num < new_col) {
        new_col = grid->col_num;
    }

    item_width  = grid->item[0].elm.css.width;
    item_height = grid->item[0].elm.css.height;
    /* 一整行走完要回退的横向距离 */
    row_span = (grid->x_interval + item_width) * (grid->col_num - 1);

    left = grid->min_left;
    grid->item[0].elm.css.left = left;
    top = grid->min_top;
    grid->item[0].elm.css.top = top;

    printf("min_left %d, min_top %d\n", left, top);
    printf("item_width %d, item_height %d\n", item_width, item_height);
    printf("new_row %d, new_col %d\n", new_row, new_col);

    /* 第一遍: 按 grid->col_num 把每个 item 的坐标排好, 先全部置隐藏 */
    for (i = 1; i < grid->avail_item_num; i++) {
        if (i % grid->col_num == 0) {
            left = left - row_span;
            if (left > (u32) - 5) {
                left = 0;       /* 定点比例累积出的 -1..-4, 归零 */
            }
            top = top + item_height + grid->y_interval;
        } else {
            left = left + item_width + grid->x_interval;
        }
        grid->item[i].elm.css.left = left;
        grid->item[i].elm.css.top = top;
        grid->item[i].elm.css.invisible = 1;
    }

    dynamic->grid_row_num = new_row;
    dynamic->grid_col_num = new_col;

    /*
     * 可视窗口右/下边界(0..10000 定点比例)。
     * @note 两个下限都拿 item_width 比 —— 原厂如此(纵向本该用 item_height),
     *       这里按 1:1 还原, 不做修正。
     */
    dynamic->grid_xval = 10000 - (grid->show_col - new_col) * (grid->x_interval + item_width);
    dynamic->grid_yval = 10000 - (grid->show_row - new_row) * (grid->y_interval + item_height);
    if (dynamic->grid_yval < item_width) {
        dynamic->grid_yval = item_width;
    }
    if (dynamic->grid_xval < item_width) {
        dynamic->grid_xval = item_width;
    }
    if (dynamic->grid_yval > 10000) {
        dynamic->grid_yval = 10000;
    }
    if (dynamic->grid_xval > 10000) {
        dynamic->grid_xval = 10000;
    }
    printf("xval %d, yval %d\n", dynamic->grid_xval, dynamic->grid_yval);

    /* 第二遍: 逐个判断是否落在可视窗口内, 并统计可视行列数 */
    first_top = -1;
    first_left = -1;
    index = 0;
    i = 0;
    while (index < grid->avail_item_num) {
        if (i != 0 && i % new_col == 0) {
            /* 动态窗口每行只用前 new_col 列, 跳过本行剩下的 item */
            index = grid->col_num - new_col + index;
            if (index >= grid->avail_item_num) {
                return;
            }
        }

        css = &grid->item[index].elm.css;

        if (first_left == -1) {
            first_left = css->left;
        }
        if (first_top == -1) {
            first_top = css->top;
        }

        right  = css->left + css->width;
        bottom = css->top + css->height;

        printf("index %d, left %d, top %d, right %d, bottom %d\n",
               index, css->left, css->top, right, bottom);

        if (css->left < 0 || right >> 2 > dynamic->grid_xval >> 2 ||
            css->top < 0 || bottom >> 2 > dynamic->grid_yval >> 2) {
            css->invisible = 1;
        } else {
            css->invisible = 0;
            if (css->left == first_left) {
                dynamic->grid_show_row++;
            }
            if (css->top == first_top) {
                dynamic->grid_show_col++;
            }
        }

        if ((i / new_col) % new_row == new_row - 1 &&
            i % new_col == new_col - 1) {
            return;
        }

        index++;
        i++;
    }
}

/* ============================================================
 *  ui_grid_slide_with_callback_dynamic - line: 365
 *  动态网格像素级滚动(正向 steps>0 / 反向 steps<0)
 *  这是一个约600行的大型函数, 核心流程:
 *    1. 正向: 检查下一个步长下 middle_rect 边界
 *       若能滚动, 所有item平移 xstep/ystep 像素
 *    2. 平移后若跨了整个item, 则更新 min_row/min_col/dhi_index 索引,
 *       并对每个item重发 ON_CHANGE_INIT_PROBE/FIRST_SHOW
 *    3. 反向同理
 * ============================================================ */
int ui_grid_slide_with_callback_dynamic(struct ui_grid *grid, int direction,
                                        int steps, void (*callback)(void *))
{
    struct rect item_rect;
    struct rect grid_rect;
    struct rect middle;
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    struct element_css *css;
    struct layout *items;
    struct element *p;
    int hi_index = grid->hi_index;
    int total = dynamic->grid_col_num * dynamic->grid_row_num;
    int center_top, center_left;
    int limit_top, limit_left;
    int item_width, item_height;
    int dhi, dhi_init;
    int x_total, y_total;
    int offset, abs_offset, gap, abs_gap;
    int row, col;
    int base_local;
    int i, j;
    u8 moved, moved2, same;

    if (steps == 0 || dynamic->drow_num == 0) {
        return 0;
    }

    if (steps > 0) {
        /* ================= 正向(往 left/top 增大方向)滑动 ================= */
        items = grid->item;
        css = &items[0].elm.css;

        item_height = items[0].elm.css.height;
        center_top  = (dynamic->grid_yval - item_height) / 2;
        item_width  = items[0].elm.css.width;
        center_left = item_width / 2;
        if (grid->area) {
            limit_top  = grid->area->top;
            limit_left = grid->area->left;
        } else {
            limit_top  = center_top;
            limit_left = center_left;
        }

        middle.left   = items[0].elm.css.left;
        middle.width  = items[0].elm.css.width;
        middle.top    = center_top;
        middle.height = item_height;

        dhi = dynamic->dcol_num * dynamic->min_row_index + dynamic->min_col_index;
        dhi_init = dhi;

        ui_core_get_element_abs_rect(&grid->elm, &grid_rect);

        moved = 0;
        x_total = 0;
        y_total = 0;

        if (direction == SCROLL_DIRECTION_LR) {
            int left0;
            int neg, abs_left;

            offset = dynamic->grid_xval * steps / grid_rect.width;
            left0 = items[0].elm.css.left;
            neg = -left0;
            abs_left = (neg > 0) ? neg : left0;
            x_total = ((left0 >= 0) ? abs_left : -abs_left) + offset;

            /* 已经整格滑出去了: 把窗口起点往前挪 */
            if (left0 + offset > 0 && x_total > grid->x_interval &&
                dynamic->min_col_index != 0) {
                for (j = 0; j != grid->avail_item_num; j++) {
                    css = &grid->item[j].elm.css;
                    css->left = css->left - left0;
                }
                {
                    int step_w = grid->x_interval + css->width;
                    int cols = x_total / step_w;

                    if (dynamic->min_col_index < cols) {
                        dynamic->min_col_index = 0;
                    } else {
                        dynamic->min_col_index = dynamic->min_col_index - cols;
                    }
                    x_total = x_total - step_w * cols;
                    dhi = dynamic->dcol_num * dynamic->min_row_index +
                          dynamic->min_col_index;
                    moved = 1;
                }
            }

            if (dynamic->min_col_index == 0) {
                css = &grid->item[0].elm.css;
                if (css->left + x_total > limit_left) {
                    x_total = limit_left - css->left;
                }
                if (!moved && x_total == 0) {
                    return 1;
                }
            }
        } else {
            int top0;
            int neg, abs_top;

            offset = dynamic->grid_yval * steps / grid_rect.height;
            top0 = items[0].elm.css.top;
            neg = -top0;
            abs_top = (neg > 0) ? neg : top0;
            y_total = ((top0 >= 0) ? abs_top : -abs_top) + offset;

            if (top0 + offset > 0 && y_total > grid->y_interval &&
                dynamic->min_row_index != 0) {
                for (j = 0; j != grid->avail_item_num; j++) {
                    css = &grid->item[j].elm.css;
                    css->top = css->top - top0;
                }
                {
                    int step_h = grid->y_interval + css->top;
                    int rows = y_total / step_h;

                    if (dynamic->min_row_index < rows) {
                        dynamic->min_row_index = 0;
                    } else {
                        dynamic->min_row_index = dynamic->min_row_index - rows;
                    }
                    y_total = y_total - (grid->y_interval + css->height) * rows;
                    dhi = dynamic->dcol_num * dynamic->min_row_index +
                          dynamic->min_col_index;
                    moved = 1;
                }
            }

            if (dynamic->min_row_index == 0) {
                css = &grid->item[0].elm.css;
                if (css->top + y_total > limit_top) {
                    y_total = limit_top - css->top;
                }
                if (!moved && y_total == 0) {
                    return 1;
                }
            }
        }

        row = (dhi / dynamic->dcol_num) % dynamic->drow_num;
        col = dhi % dynamic->dcol_num;

        moved2 = 0;
        if (direction == SCROLL_DIRECTION_LR) {
            if (col != 0 && moved && x_total != 0) {
                x_total = x_total - css->width - grid->x_interval;
                moved2 = 1;
            }
            if (x_total == 0) {
                goto finish;
            }
        } else {
            if (row != 0 && moved && y_total != 0) {
                y_total = y_total - css->height - grid->y_interval;
                moved2 = 1;
            }
            if (y_total == 0) {
                goto finish;
            }
        }

        for (i = 0; i < grid->avail_item_num; i++) {
            struct layout *cur = grid->item;

            css = &cur[i].elm.css;

            if (direction == SCROLL_DIRECTION_LR) {
                if (i == 0) {
                    if (cur[0].elm.css.left > center_left && col == 0) {
                        return 1;
                    }
                }
                cur[i].elm.css.left = cur[i].elm.css.left + x_total;
                if (moved2 && i == 0) {
                    if (cur[i].elm.css.width + cur[i].elm.css.left > 0) {
                        dhi = dhi - 1;
                    }
                }

                row = (i / grid->col_num) % grid->row_num;
                col = i % grid->col_num;
                if (row < dynamic->drow_num && col < dynamic->dcol_num) {
                    item_rect.left   = cur[i].elm.css.width + cur[i].elm.css.left;
                    item_rect.top    = cur[i].elm.css.top;
                    item_rect.width  = cur[i].elm.css.width;
                    item_rect.height = cur[i].elm.css.height;
                    middle.left = center_left;
                    if (get_rect_cover(&middle, &item_rect, &grid_rect)) {
                        if (grid_rect.left >= cur[i].elm.css.left * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    if (cur[i].elm.css.width + cur[i].elm.css.left < 1 ||
                        cur[i].elm.css.left >= dynamic->grid_xval) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                } else {
                    css->invisible = 1;
                }
            } else {
                if (i == 0) {
                    if (cur[0].elm.css.top > center_top && row == 0) {
                        return 1;
                    }
                }
                cur[i].elm.css.top = cur[i].elm.css.top + y_total;
                if (moved2 && i == 0) {
                    if (cur[i].elm.css.height + cur[i].elm.css.top > 0) {
                        dhi = dhi - dynamic->dcol_num;
                    }
                }

                row = (i / grid->col_num) % grid->row_num;
                col = i % grid->col_num;
                if (row < dynamic->drow_num && col < dynamic->dcol_num) {
                    item_rect.left   = cur[i].elm.css.left;
                    item_rect.top    = cur[i].elm.css.top;
                    item_rect.width  = cur[i].elm.css.width;
                    item_rect.height = cur[i].elm.css.height;
                    if (get_rect_cover(&middle, &item_rect, &grid_rect)) {
                        if (grid_rect.height >= cur[i].elm.css.height * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    if (cur[i].elm.css.height + cur[i].elm.css.top < 1 ||
                        cur[i].elm.css.top >= dynamic->grid_yval) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                } else {
                    css->invisible = 1;
                }
            }
        }
    } else {
        /* ================= 反向滑动 ================= */
        int last = total - 1;

        row = (last / dynamic->grid_col_num) % dynamic->grid_row_num;
        col = last % dynamic->grid_col_num;
        base_local = grid->col_num * row + col;

        items = grid->item;
        css = &items[base_local].elm.css;

        item_height = items[base_local].elm.css.height;
        center_top  = dynamic->grid_yval - (dynamic->grid_yval - item_height) / 2;
        limit_top   = grid->area ? grid->area->bottom : center_top;
        item_width  = items[base_local].elm.css.width;
        center_left = dynamic->grid_xval - (dynamic->grid_xval - item_width) / 2;
        limit_left  = grid->area ? grid->area->right : center_left;

        middle.left   = items[base_local].elm.css.left;
        middle.width  = item_width;
        middle.top    = center_top - item_height;
        middle.height = item_height;

        dhi = dynamic->dcol_num * dynamic->max_row_index + dynamic->max_col_index;
        dhi_init = dhi;

        ui_core_get_element_abs_rect(&grid->elm, &grid_rect);

        moved = 0;
        x_total = 0;
        y_total = 0;

        if (direction == SCROLL_DIRECTION_LR) {
            int right;

            offset = dynamic->grid_xval * steps / grid_rect.width;
            right = items[base_local].elm.css.width + items[base_local].elm.css.left;
            gap = dynamic->grid_xval - right;
            abs_offset = (offset > 0) ? offset : -offset;
            abs_gap = (gap > 0) ? gap : -gap;
            x_total = ((dynamic->grid_xval < right) ? -abs_gap : abs_gap) + abs_offset;

            if (right + offset < dynamic->grid_xval && x_total > grid->x_interval &&
                dynamic->max_col_index < dynamic->dcol_num - 1) {
                for (j = 0; j != grid->avail_item_num; j++) {
                    css = &grid->item[j].elm.css;
                    css->left = css->left + gap;
                }
                {
                    int step_w = grid->x_interval + css->width;
                    int cols = x_total / step_w;

                    if (dynamic->max_col_index + cols > dynamic->dcol_num - 1) {
                        dynamic->max_col_index = dynamic->dcol_num - 1;
                    } else {
                        dynamic->max_col_index = dynamic->max_col_index + cols;
                    }
                    x_total = x_total - step_w * cols;
                    dhi = dynamic->dcol_num * dynamic->max_row_index +
                          dynamic->max_col_index;
                    moved = 1;
                }
            }

            if (dynamic->max_col_index == dynamic->dcol_num - 1) {
                css = &grid->item[base_local].elm.css;
                if (css->width + css->left + x_total < limit_left) {
                    x_total = limit_left - (css->width + css->left);
                }
                if (!moved && x_total == 0) {
                    return 1;
                }
            }
        } else {
            int bottom;

            offset = dynamic->grid_yval * steps / grid_rect.height;
            bottom = items[base_local].elm.css.height + items[base_local].elm.css.top;
            gap = dynamic->grid_yval - bottom;
            abs_offset = (offset > 0) ? offset : -offset;
            abs_gap = (gap > 0) ? gap : -gap;
            y_total = ((dynamic->grid_yval < bottom) ? -abs_gap : abs_gap) + abs_offset;

            if (bottom + offset < dynamic->grid_yval && y_total > grid->y_interval &&
                dynamic->max_row_index < dynamic->drow_num - 1) {
                for (j = 0; j != grid->avail_item_num; j++) {
                    css = &grid->item[j].elm.css;
                    css->top = css->top + gap;
                }
                {
                    int step_h = grid->y_interval + css->top;
                    int rows = y_total / step_h;

                    if (dynamic->max_row_index + rows > dynamic->drow_num - 1) {
                        dynamic->max_row_index = dynamic->drow_num - 1;
                    } else {
                        dynamic->max_row_index = dynamic->max_row_index + rows;
                    }
                    y_total = y_total - step_h * rows;
                    dhi = dynamic->dcol_num * dynamic->max_row_index +
                          dynamic->max_col_index;
                    moved = 1;
                }
            }

            if (dynamic->max_row_index == dynamic->drow_num - 1) {
                css = &grid->item[base_local].elm.css;
                if (css->height + css->top + y_total < limit_top) {
                    y_total = limit_top - (css->height + css->top);
                }
                if (!moved && y_total == 0) {
                    return 1;
                }
            }
        }

        row = (dhi / dynamic->dcol_num) % dynamic->drow_num;
        col = dhi % dynamic->dcol_num;

        moved2 = 0;
        if (direction == SCROLL_DIRECTION_LR) {
            if (col < dynamic->dcol_num - 1 && moved && x_total != 0) {
                x_total = css->width - x_total + grid->x_interval;
                moved2 = 1;
            }
            if (x_total == 0) {
                goto finish;
            }
        } else {
            if (row < dynamic->drow_num - 1 && moved && y_total != 0) {
                y_total = css->height - y_total + grid->y_interval;
                moved2 = 1;
            }
            if (y_total == 0) {
                goto finish;
            }
        }

        i = grid->avail_item_num;
        while (--i >= 0) {
            struct layout *cur = grid->item;

            css = &cur[i].elm.css;

            if (direction == SCROLL_DIRECTION_LR) {
                if (i == base_local) {
                    if (cur[base_local].elm.css.width + cur[base_local].elm.css.left <
                        center_left &&
                        col == dynamic->dcol_num - 1) {
                        return 1;
                    }
                }
                cur[i].elm.css.left = cur[i].elm.css.left + x_total;
                if (moved2 && i == base_local) {
                    if (cur[i].elm.css.left < dynamic->grid_xval) {
                        dhi = dhi + 1;
                    }
                }

                row = (i / grid->col_num) % grid->row_num;
                col = i % grid->col_num;
                if (row < dynamic->drow_num && col < dynamic->dcol_num) {
                    item_rect.left   = cur[i].elm.css.left;
                    item_rect.top    = cur[i].elm.css.top;
                    item_rect.width  = cur[i].elm.css.width;
                    item_rect.height = cur[i].elm.css.height;
                    middle.left = center_left;
                    if (get_rect_cover(&middle, &item_rect, &grid_rect)) {
                        if (grid_rect.left >= cur[i].elm.css.left * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    if (cur[i].elm.css.width + cur[i].elm.css.left < 1 ||
                        cur[i].elm.css.left >= dynamic->grid_xval) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                } else {
                    css->invisible = 1;
                }
            } else {
                if (i == base_local) {
                    if (cur[base_local].elm.css.height + cur[base_local].elm.css.top <
                        center_top &&
                        row == dynamic->drow_num - 1) {
                        return 1;
                    }
                }
                cur[i].elm.css.top = cur[i].elm.css.top + y_total;
                if (moved2 && i == base_local) {
                    if (cur[i].elm.css.top < dynamic->grid_yval) {
                        dhi = dhi + dynamic->dcol_num;
                    }
                }

                row = (i / grid->col_num) % grid->row_num;
                col = i % grid->col_num;
                if (row < dynamic->drow_num && col < dynamic->dcol_num) {
                    item_rect.left   = cur[i].elm.css.left;
                    item_rect.top    = cur[i].elm.css.top;
                    item_rect.width  = cur[i].elm.css.width;
                    item_rect.height = cur[i].elm.css.height;
                    if (get_rect_cover(&middle, &item_rect, &grid_rect)) {
                        if (grid_rect.height >= cur[i].elm.css.height * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    if (cur[i].elm.css.height + cur[i].elm.css.top < 1 ||
                        cur[i].elm.css.top >= dynamic->grid_yval) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                } else {
                    css->invisible = 1;
                }
            }
        }
    }

finish:
    same = 1;
    if (steps > 0) {
        int r = (dhi / dynamic->dcol_num) % dynamic->drow_num;
        int c = dhi % dynamic->dcol_num;

        same = (dhi_init == dhi);
        dhi = (r - 1 + dynamic->grid_row_num) * dynamic->dcol_num +
              (c - 1 + dynamic->grid_col_num);
    }

    if (same && dhi_init == dhi) {
        /* 窗口没变: 只把 dhi_index 更新到 hi_index 对应的那一项 */
        int cur_dindex = dhi_init;

        for (i = total - 1; i >= 0; i--) {
            if (i != total - 1) {
                if (i % dynamic->grid_col_num == dynamic->grid_col_num - 1) {
                    cur_dindex = i % dynamic->grid_col_num + cur_dindex - dynamic->dcol_num;
                } else {
                    cur_dindex = cur_dindex - 1;
                }
            }
            if (i == grid->hi_index) {
                dynamic->dhi_index = cur_dindex;
                break;
            }
        }
    } else {
        /* 窗口变了: 重扫一遍, 补内容并重算 min/max 游标 */
        int cur_dindex = dhi;
        int min_left, max_left, min_top, max_top;
        int min_show_left, max_show_left, min_show_top, max_show_top;
        int left, top;

        item_width  = css->width;
        item_height = css->height;

        min_show_left = dynamic->grid_xval;
        min_show_top  = dynamic->grid_yval;
        max_show_left = -1;
        max_show_top  = -1;
        min_left = dynamic->grid_xval;
        min_top  = dynamic->grid_yval;
        max_left = -1;
        max_top  = -1;

        for (i = total - 1; i >= 0; i--) {
            int local_i;

            if (i != total - 1) {
                if (i % dynamic->grid_col_num == dynamic->grid_col_num - 1) {
                    cur_dindex = i % dynamic->grid_col_num + cur_dindex - dynamic->dcol_num;
                } else {
                    cur_dindex = cur_dindex - 1;
                }
            }
            if (i == grid->hi_index) {
                dynamic->dhi_index = cur_dindex;
            }

            local_i = (grid->col_num - dynamic->grid_col_num) *
                      ((i / dynamic->dcol_num) % dynamic->drow_num) + i;

            list_for_each_child_element(p, &grid->item[local_i].elm) {
                if (p->handler && p->handler->onchange) {
                    p->handler->onchange(p, ON_CHANGE_UPDATE_ITEM, (void *)cur_dindex);
                }
            }

            css = &grid->item[local_i].elm.css;
            left = css->left;
            if (min_left > left) {
                dynamic->min_col_index = cur_dindex % dynamic->dcol_num;
                min_left = left;
            }
            if (max_left < left) {
                dynamic->max_col_index = cur_dindex % dynamic->dcol_num;
                max_left = left;
            }
            top = css->top;
            if (min_top > top) {
                dynamic->min_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                min_top = top;
            }
            if (max_top < top) {
                dynamic->max_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                max_top = top;
            }

            if (top + item_height < 0 || top >> 2 > dynamic->grid_yval >> 2 ||
                left + item_width < 0 || left >> 2 > dynamic->grid_xval >> 2) {
                css->invisible = 1;
            } else {
                css->invisible = 0;
                if (min_show_left > left) {
                    dynamic->min_show_col_index = cur_dindex % dynamic->dcol_num;
                    min_show_left = left;
                }
                if (max_show_left < left) {
                    dynamic->max_show_col_index = cur_dindex % dynamic->dcol_num;
                    max_show_left = left;
                }
                if (min_show_top > top) {
                    dynamic->min_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                    min_show_top = top;
                }
                if (max_show_top < top) {
                    dynamic->max_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                    max_show_top = top;
                }
            }
        }
    }

    if (hi_index != grid->hi_index) {
        item_highlight(&grid->item[hi_index].elm, 0);
        item_highlight(&grid->item[grid->hi_index].elm, 1);
    }

    if (callback) {
        callback(grid);
    }

    return 1;
}

/* ============================================================
 *  ui_grid_slide_with_callback - line: 964
 *  普通网格(非动态)像素滚动, 结构与 dynamic 版类似
 * ============================================================ */
int ui_grid_slide_with_callback(struct ui_grid *grid, int direction,
                                int steps, void (*callback)(void *))
{
    struct rect grid_rect;
    struct rect middle;
    struct layout *items;
    struct element_css *css;
    int hi_index = grid->hi_index;
    int center_top, center_left;
    int limit_top, limit_left;
    int xoffset, yoffset;
    int i;

    if (steps == 0) {
        return 0;
    }

    items = grid->item;

    if (steps > 0) {
        /* ---- 正向(往 left/top 增大的方向)平移 ---- */
        center_top  = (10000 - items[0].elm.css.height) / 2;
        center_left = items[0].elm.css.width / 2;
        if (grid->area) {
            limit_top  = grid->area->top;
            limit_left = grid->area->left;
        } else {
            limit_top  = center_top;
            limit_left = center_left;
        }

        /* middle: 屏幕正中那一格, 谁盖住它最多谁就是高亮项 */
        middle.left   = items[0].elm.css.left;
        middle.width  = items[0].elm.css.width;
        middle.top    = center_top;
        middle.height = items[0].elm.css.height;

        for (i = 0; i < grid->avail_item_num; i++) {
            struct layout *cur = grid->item;

            css = &cur[i].elm.css;

            if (i == 0) {
                ui_core_get_element_abs_rect(&grid->elm, &grid_rect);
                if (direction == SCROLL_DIRECTION_LR) {
                    xoffset = steps * 10000 / grid_rect.width;
                    if (cur[0].elm.css.left + xoffset > limit_left) {
                        xoffset = limit_left - cur[0].elm.css.left;
                    }
                } else {
                    yoffset = steps * 10000 / grid_rect.height;
                    if (cur[0].elm.css.top + yoffset > limit_top) {
                        yoffset = limit_top - cur[0].elm.css.top;
                    }
                    if (yoffset == 0 || cur[0].elm.css.top > center_top) {
                        return 1;
                    }
                }
            }

            if (direction == SCROLL_DIRECTION_LR) {
                if (xoffset == 0) {
                    return 1;
                }
                css->left = css->left + xoffset;
                {
                    struct rect item_rect;
                    struct rect cover;
                    int right;

                    item_rect.left   = css->width + css->left;
                    item_rect.top    = css->top;
                    item_rect.width  = css->width;
                    item_rect.height = css->height;
                    middle.left = center_left;
                    if (get_rect_cover(&middle, &item_rect, &cover)) {
                        if (cover.left >= css->left * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    right = css->width + css->left;
                    if (css->left > 9999 || right < 1) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                }
            } else {
                struct rect item_rect;
                struct rect cover;
                int bottom;

                css->top = css->top + yoffset;

                item_rect.left   = css->left;
                item_rect.top    = css->top;
                item_rect.width  = css->width;
                item_rect.height = css->height;
                if (get_rect_cover(&middle, &item_rect, &cover)) {
                    if (cover.height >= css->height * 2 / 3) {
                        grid->hi_index = i;
                    }
                }
                bottom = css->height + css->top;
                if (css->top > 9999 || bottom < 1) {
                    css->invisible = 1;
                } else {
                    css->invisible = 0;
                }
            }
        }
    } else {
        /* ---- 反向平移: 从最后一个 item 往前走 ---- */
        int last = grid->avail_item_num - 1;
        int item_height = items[last].elm.css.height;

        center_top  = 10000 - (10000 - item_height) / 2;
        center_left = items[last].elm.css.width / 2;
        if (grid->area) {
            limit_top  = grid->area->bottom;
            limit_left = grid->area->right;
        } else {
            limit_top  = center_top;
            limit_left = center_left;
        }

        middle.left   = items[last].elm.css.left;
        middle.width  = items[last].elm.css.width;
        middle.top    = center_top - item_height;
        middle.height = item_height;

        i = grid->avail_item_num;
        while (--i >= 0) {
            struct layout *cur = grid->item;

            css = &cur[i].elm.css;

            if (i == grid->avail_item_num - 1) {
                ui_core_get_element_abs_rect(&grid->elm, &grid_rect);
                if (direction == SCROLL_DIRECTION_LR) {
                    xoffset = steps * 10000 / grid_rect.width;
                    if (i == grid->avail_item_num - 1) {
                        if (css->left + xoffset + css->width < limit_left) {
                            /* @note 原厂这里减的是 height, 纵横搞混了, 1:1 还原 */
                            xoffset = limit_left - css->left - css->height;
                        }
                    }
                } else {
                    yoffset = steps * 10000 / grid_rect.height;
                    if (css->height + css->top + yoffset < limit_top) {
                        yoffset = limit_top - (css->height + css->top);
                    }
                    if (yoffset == 0) {
                        return 1;
                    }
                    if (i == grid->avail_item_num - 1 &&
                        css->height + css->top < center_top) {
                        return 1;
                    }
                }
            }

            if (direction == SCROLL_DIRECTION_LR) {
                if (xoffset == 0) {
                    return 1;
                }
                css->left = css->left + xoffset;
                {
                    struct rect item_rect;
                    struct rect cover;
                    int right;

                    item_rect.left   = css->left;
                    item_rect.top    = css->top;
                    item_rect.width  = css->width;
                    item_rect.height = css->height;
                    middle.left = center_left;
                    if (get_rect_cover(&middle, &item_rect, &cover)) {
                        if (cover.left >= css->left * 2 / 3) {
                            grid->hi_index = i;
                        }
                    }
                    right = css->width + css->left;
                    if (css->left > 9999 || right < 1) {
                        css->invisible = 1;
                    } else {
                        css->invisible = 0;
                    }
                }
            } else {
                struct rect item_rect;
                struct rect cover;
                int bottom;

                css->top = css->top + yoffset;

                item_rect.left   = css->left;
                item_rect.top    = css->top;
                item_rect.width  = css->width;
                item_rect.height = css->height;
                if (get_rect_cover(&middle, &item_rect, &cover)) {
                    if (cover.height >= css->height * 2 / 3) {
                        grid->hi_index = i;
                    }
                }
                bottom = css->height + css->top;
                if (css->top > 9999 || bottom < 1) {
                    css->invisible = 1;
                } else {
                    css->invisible = 0;
                }
            }
        }
    }

    if (hi_index != grid->hi_index) {
        item_highlight(&grid->item[hi_index].elm, 0);
        item_highlight(&grid->item[grid->hi_index].elm, 1);
    }

    if (callback) {
        callback(grid);
    }

    return 1;
}

/* ============================================================
 *  ui_grid_slide - line: 1167
 * ============================================================ */
int ui_grid_slide(struct ui_grid *grid, int direction, int steps)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;

    if (dynamic) {
        if (direction == SCROLL_DIRECTION_LR) {
            if (dynamic->dcol_num <= grid->show_col) {
                return 0;
            }
        } else {
            if (dynamic->drow_num <= grid->show_row) {
                return 0;
            }
        }
        return ui_grid_slide_with_callback_dynamic(grid, direction, steps,
                (void (*)(void *))ui_core_redraw);
    }

    return ui_grid_slide_with_callback(grid, direction, steps,
                                       (void (*)(void *))ui_core_redraw);
}

/* ============================================================
 *  ui_grid_dynamic_create - line: 1200
 *  链表版动态列表: 与 grid->dynamic(struct ui_grid_dynamic) 那套互斥,
 *  只有 grid->dynamic == NULL 时这套才生效。
 * ============================================================ */
int ui_grid_dynamic_create(struct ui_grid *grid, int direction, int list_total,
                           int (*event_handler_cb)(void *, int, int, int))
{
    struct grid_dynamic *dynamic;

    if (grid->dynamic) {
        return -1;
    }

    dynamic = zalloc(sizeof(struct grid_dynamic));
    dynamic->list_total = list_total;
    dynamic->grid = grid;
    dynamic->event_handler_cb = event_handler_cb;
    list_add_tail(&dynamic->entry, &dynamic_head);

    return 0;
}

/* ============================================================
 *  ui_grid_dynamic_set_item_by_id - line: 1216
 * ============================================================ */
int ui_grid_dynamic_set_item_by_id(int id, int count)
{
    struct element *elm;
    struct ui_grid *grid;
    struct grid_dynamic *dynamic;

    elm = ui_core_get_element_by_id(id);
    grid = (struct ui_grid *)elm;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return -1;
    }

    list_for_each_entry(dynamic, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            dynamic->list_total = count;
        }
    }

    return 0;
}

/* ============================================================
 *  ui_grid_dynamic_reset - line: 1238
 *  注意: 形参 index 在原厂未被使用。
 * ============================================================ */
int ui_grid_dynamic_reset(struct ui_grid *grid, int index)
{
    struct grid_dynamic *dynamic;
    int item_step;
    int i;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return 0;
    }

    list_for_each_entry(dynamic, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            break;
        }
    }
    if (dynamic->grid != (void *)grid) {
        return 0;
    }

    dynamic->offset = 0;
    dynamic->base_index = 0;
    item_step = grid->item[0].elm.css.height + grid->y_interval;
    for (i = 0; i < grid->avail_item_num; i++) {
        grid->item[i].elm.css.top = i * item_step;
    }

    return 0;
}

/* ============================================================
 *  ui_grid_dynamic_release - line: 1272
 * ============================================================ */
int ui_grid_dynamic_release(struct ui_grid *grid)
{
    struct grid_dynamic *dynamic, *n;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return -1;
    }

    list_for_each_entry_safe(dynamic, n, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            list_del(&dynamic->entry);
            free(dynamic);
        }
    }

    return 0;
}

/* ============================================================
 *  ui_grid_dynamic_set_prepare - line: 1294
 * ============================================================ */
int ui_grid_dynamic_set_prepare(struct ui_grid *grid,
                                int (*prepare_cb)(void *, int, int, int))
{
    struct grid_dynamic *dynamic;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return -1;
    }

    list_for_each_entry(dynamic, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            dynamic->prepare_cb = prepare_cb;
            return 0;
        }
    }

    return -1;
}

/* ============================================================
 *  ui_grid_dynamic_cur_item - line: 1316
 * ============================================================ */
int ui_grid_dynamic_cur_item(struct ui_grid *grid)
{
    struct grid_dynamic *dynamic;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return -1;
    }

    list_for_each_entry(dynamic, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            if (grid->touch_index >= 0) {
                return dynamic->base_index + grid->touch_index;
            }
            return grid->hi_index + dynamic->base_index;
        }
    }

    return -1;
}

/* ============================================================
 *  ui_grid_dynamic_slide - line: 1342
 *  像素级滚动: 用 10000 为一屏的定点比例做 offset 累加,
 *  再按 item 步距把每个 item 的 css.top 重新排布。
 *  注意: 形参 direction 在原厂未被使用。
 * ============================================================ */
int ui_grid_dynamic_slide(struct ui_grid *grid, int direction, int steps)
{
    struct grid_dynamic *dynamic;
    struct element_css *css;
    struct element *p;
    bool cover;
    int item_step;
    int base;
    int top;
    u8 num;
    int cover_h;
    int max_h;
    char hi_index;
    int i;

    if (grid->dynamic || list_empty(&dynamic_head)) {
        return 0;
    }

    list_for_each_entry(dynamic, &dynamic_head, entry) {
        if (dynamic->grid == (void *)grid) {
            break;
        }
    }
    if (dynamic->grid != (void *)grid) {
        return 0;
    }

    {
    struct rect area = {0};
    struct rect rect = {0};
    struct rect item = {0};

    ui_core_get_element_abs_rect(&grid->elm, &rect);
    dynamic->offset -= steps * 10000 / rect.height;
    css = &grid->item[0].elm.css;

    item.width = 10000;
    item.height = 10000;
    item.top = dynamic->offset;

    area.width = 10000;
    item_step = grid->item[0].elm.css.height + grid->y_interval;
    area.height = item_step * dynamic->list_total;

    cover = get_rect_cover(&item, &area, &rect);

    if (dynamic->offset > 0) {
        base = dynamic->offset / item_step;
        cover_h = rect.height;
        num = cover_h / item_step + ((dynamic->offset % item_step) ? 1 : 0);
    } else {
        base = 0;
        cover_h = rect.height;
        num = (cover_h + item_step - 1) / item_step;
    }

    hi_index = grid->hi_index;

    if (!cover) {
        return cover;
    }

    if (rect.top == 0) {
        if (cover_h < 5000) {
            dynamic->offset = cover_h - 10000;
            return 0;
        }
    } else {
        if (cover_h < 5000) {
            dynamic->offset = area.height - 5000;
            return 0;
        }
    }

    if (dynamic->prepare_cb) {
        int cnt = dynamic->list_total - base;
        if (cnt >= grid->avail_item_num) {
            cnt = grid->avail_item_num;
        }
        dynamic->prepare_cb(grid, cnt, base, cnt + base);
    }

    max_h = (area.height < 10000) ? area.height : 10000;

    for (i = 0; i < grid->avail_item_num; i++) {
        struct element_css *css_cur = &grid->item[i].elm.css;

        if (i >= num) {
            css_cur->invisible = 1;
            continue;
        }

        if (i == 0) {
            if (rect.top == 0 && rect.height < 10000) {
                top = max_h - rect.height;
            } else {
                top = -(rect.top % item_step);
            }
            css = css_cur;
        } else {
            top = css->top + i * item_step;
        }

        grid->item[i].elm.css.top = top;
        css_cur->invisible = 0;

        if (top > (rect.height - item_step) / 2 &&
            top <= (rect.height + item_step) / 2) {
            grid->hi_index = i;
            dynamic->base_index = base;
        }

        list_for_each_child_element(p, &grid->item[i].elm) {
            if (!dynamic->event_handler_cb) {
                continue;
            }
            if (dynamic->event_handler_cb(p, p->id, (p->id >> 16) & 0x3f, i + base)) {
                break;
            }
        }
    }

    if (hi_index != grid->hi_index) {
        item_highlight(&grid->item[hi_index].elm, 0);
        item_highlight(&grid->item[grid->hi_index].elm, 1);
    }

    ui_core_redraw(grid);

    return cover;
    }
}


/* ============================================================
 *  ui_grid_add_dynamic - line: 1580
 * ============================================================ */
int ui_grid_add_dynamic(struct ui_grid *grid, int *row, int *col, int redraw)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    struct element *p;
    int new_row, new_col;
    int last_item, cur_dindex;
    int i;

    if (!row || !col || !dynamic) {
        return -1;
    }
    if (*row < 1 && *col < 1) {
        return -1;
    }

    new_row = *row + dynamic->drow_num;
    new_col = *col + dynamic->dcol_num;
    if (new_row == 0) {
        new_row = 1;
    }
    if (new_col == 0) {
        new_col = 1;
    }

    __grid_ajust(grid, new_row, new_col);

    dynamic->dhi_index = -1;
    dynamic->drow_num = new_row;
    dynamic->dcol_num = new_col;

    dynamic->min_row_index      = 0;
    dynamic->max_row_index      = dynamic->grid_row_num - 1;
    dynamic->min_col_index      = 0;
    dynamic->max_col_index      = dynamic->grid_col_num - 1;
    dynamic->min_show_row_index = 0;
    dynamic->max_show_row_index = dynamic->grid_show_row - 1;
    dynamic->min_show_col_index = 0;
    dynamic->max_show_col_index = dynamic->grid_show_col - 1;

    *row = new_row;
    *col = new_col;

    /*
     * 从最后一个屏上 item 往前走, 同时把"列表下标" cur_dindex 一起往前退,
     * 逐个给子控件发 ON_CHANGE_UPDATE_ITEM, 让应用层填内容。
     */
    last_item = dynamic->grid_col_num * dynamic->grid_row_num - 1;
    cur_dindex = dynamic->dcol_num *
                 ((last_item / dynamic->grid_col_num) % dynamic->grid_row_num) +
                 last_item % dynamic->grid_col_num;

    for (i = last_item; i >= 0; i--) {
        int index;

        if (i != last_item) {
            if (i % dynamic->grid_col_num == dynamic->grid_col_num - 1) {
                /* 退到上一行的最后一列 */
                cur_dindex = i % dynamic->grid_col_num + cur_dindex - dynamic->dcol_num;
            } else {
                cur_dindex = cur_dindex - 1;
            }
        }

        /* 屏上 item 是按 grid->col_num 排布的, 动态窗口只占前 grid_col_num 列 */
        index = ((i / dynamic->dcol_num) % dynamic->drow_num) *
                (grid->col_num - dynamic->grid_col_num) + i;

        list_for_each_child_element(p, &grid->item[index].elm) {
            if (p->handler && p->handler->onchange) {
                p->handler->onchange(p, ON_CHANGE_UPDATE_ITEM, (void *)cur_dindex);
            }
        }
    }

    if (redraw == 1) {
        ui_core_redraw(grid);
    }

    return 0;
}

/* ============================================================
 *  ui_grid_del_dynamic - line: 1650
 * ============================================================ */
int ui_grid_del_dynamic(struct ui_grid *grid, int *row, int *col, int redraw)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    struct element *p;
    int new_row, new_col;
    int last_item, cur_dindex;
    int i;

    if (!row || !col || !dynamic) {
        return -1;
    }
    if (*row < 1 && *col < 1) {
        return -1;
    }

    /* 要删的行/列数被夹到现有行列数以内 */
    new_row = dynamic->drow_num -
              ((*row < dynamic->drow_num) ? *row : dynamic->drow_num);
    new_col = dynamic->dcol_num -
              ((*col < dynamic->dcol_num) ? *col : dynamic->dcol_num);

    if (new_row < 1 || new_col < 1) {
        /* 删空了: 所有 item 隐藏, 动态状态整块清零 */
        for (i = 0; i < grid->avail_item_num; i++) {
            grid->item[i].elm.css.invisible = 1;
        }
        memset(dynamic, 0, sizeof(struct ui_grid_dynamic));
        if (redraw == 1) {
            ui_core_redraw(grid);
        }
        return 0;
    }

    __grid_ajust(grid, new_row, new_col);

    dynamic->dhi_index = -1;
    dynamic->drow_num = new_row;
    dynamic->dcol_num = new_col;

    dynamic->min_row_index      = 0;
    dynamic->max_row_index      = dynamic->grid_row_num - 1;
    dynamic->min_col_index      = 0;
    dynamic->max_col_index      = dynamic->grid_col_num - 1;
    dynamic->min_show_row_index = 0;
    dynamic->max_show_row_index = dynamic->grid_show_row - 1;
    dynamic->min_show_col_index = 0;
    dynamic->max_show_col_index = dynamic->grid_show_col - 1;

    *row = new_row;
    *col = new_col;

    last_item = dynamic->grid_col_num * dynamic->grid_row_num - 1;
    cur_dindex = dynamic->dcol_num *
                 ((last_item / dynamic->grid_col_num) % dynamic->grid_row_num) +
                 last_item % dynamic->grid_col_num;

    for (i = last_item; i >= 0; i--) {
        int index;

        if (i != last_item) {
            if (i % dynamic->grid_col_num == dynamic->grid_col_num - 1) {
                cur_dindex = i % dynamic->grid_col_num + cur_dindex - dynamic->dcol_num;
            } else {
                cur_dindex = cur_dindex - 1;
            }
        }

        index = ((i / dynamic->dcol_num) % dynamic->drow_num) *
                (grid->col_num - dynamic->grid_col_num) + i;

        list_for_each_child_element(p, &grid->item[index].elm) {
            if (p->handler && p->handler->onchange) {
                p->handler->onchange(p, ON_CHANGE_UPDATE_ITEM, (void *)cur_dindex);
            }
        }
    }

    if (redraw == 1) {
        ui_core_redraw(grid);
    }

    return 0;
}

/* ============================================================
 *  ui_grid_init_dynamic - line: 1734
 *  注意: 原库此处直接 ui_core_malloc(60), 然后
 *    若已有 grid->dynamic 或 row/col<1, 先把所有 item invisible 清零
 *    再挂 dynamic, 最后 ui_grid_add_dynamic(row, col, 0)
 * ============================================================ */
int ui_grid_init_dynamic(struct ui_grid *grid, int *row, int *col)
{
    struct ui_grid_dynamic *dynamic;
    int i;

    dynamic = ui_core_malloc(sizeof(struct ui_grid_dynamic));
    if (!dynamic) {
        *row = 0;
        *col = 0;
        return -1;
    }

    /* 行列非法或已经初始化过: 只把所有 item 隐藏起来, 不建动态窗口 */
    if (*row < 1 || *col < 1 || grid->dynamic) {
        *row = 0;
        *col = 0;
        grid->dynamic = dynamic;
        for (i = 0; i < grid->avail_item_num; i++) {
            grid->item[i].elm.css.invisible = 1;
        }
        grid->dynamic = dynamic;    /* 原厂 IR 里此处确实写了两次 */
        return 0;
    }

    grid->dynamic = dynamic;
    if (ui_grid_add_dynamic(grid, row, col, 0)) {
        ui_core_free(dynamic);
        grid->dynamic = NULL;
        *row = 0;
        *col = 0;
        return -1;
    }

    printf("min_show_col_index %d, max_show_col_index %d, min_show_row_index %d, max_show_row_index %d\n",
           dynamic->min_show_col_index, dynamic->max_show_col_index,
           dynamic->min_show_row_index, dynamic->max_show_row_index);
    printf("max_row_index %d, max_col_index %d, min_row_index %d, min_col_index %d\n",
           dynamic->max_row_index, dynamic->max_col_index,
           dynamic->min_row_index, dynamic->min_col_index);

    return 0;
}

/* ============================================================
 *  ui_grid_add_dynamic_by_id
 * ============================================================ */
int ui_grid_add_dynamic_by_id(int id, int *row, int *col, int redraw)
{
    struct element *elm = ui_core_get_element_by_id(id);

    return ui_grid_add_dynamic((struct ui_grid *)elm, row, col, redraw);
}

/* ============================================================
 *  ui_grid_del_dynamic_by_id
 * ============================================================ */
int ui_grid_del_dynamic_by_id(int id, int *row, int *col, int redraw)
{
    struct element *elm = ui_core_get_element_by_id(id);

    return ui_grid_del_dynamic((struct ui_grid *)elm, row, col, redraw);
}

/* ============================================================
 *  ui_grid_release - line: 2723
 * ============================================================ */
void ui_grid_release(struct ui_grid *grid)
{
    struct element *p;

    if (grid->info) {
        /* 资源还在: item[] 是 layout_new 出来的一整块, 交给 layout_delete */
        if (grid->item) {
            layout_delete(grid->item, grid->ctrl_num);
        }
    } else {
        /* 资源已被释放(动态列表场景): 逐个放掉子元素, 再放 item[] 那块内存 */
        list_for_each_child_element(p, &grid->elm) {
            ui_core_release_child(p);
        }
        ui_core_free(grid->item);
    }

    if (grid->dynamic) {
        ui_core_free(grid->dynamic);
        grid->dynamic = NULL;
    }

    ui_core_remove_element(grid);
    ui_core_free(grid);
}

/* ============================================================
 *  ui_grid_child_init - line: 2747 (internal fastcc)
 *  由 new_ui_grid 调用, 负责:
 *    1. layout_new 展开所有子项 item[]
 *    2. 遍历 item[0..avail-1] 的子元素, 算出:
 *       col_num, row_num, show_row, show_col
 *       min/max_show_left/top, min/max_left/top
 *       x_interval, y_interval
 *    3. page_mode==0 时根据是否跨出可视区设 invisible
 *    4. 若 show_row<row_num 或 show_col<col_num, 设 pix_scroll=1
 * ============================================================ */
static void ui_grid_child_init(struct ui_grid *grid, struct ui_grid_info *info)
{
    int last_top, last_left;
    struct element_css *css;
    struct element *p;
    u8 ctrl_num;

    if (grid->ctrl_num > info->head.ctrl_num) {
        ctrl_num = info->head.ctrl_num;
    } else {
        ctrl_num = grid->ctrl_num;
    }

    if (info && ctrl_num) {
        grid->item = layout_new(info->info, ctrl_num, &grid->elm);
        if (!grid->item) {
            return;
        }
        grid->avail_item_num = ctrl_num;
    }

    grid->col_num  = 1;
    grid->row_num  = 1;
    grid->show_col = 0;
    grid->show_row = 0;

    grid->min_show_top  = 10000;
    grid->min_show_left = 10000;
    grid->max_show_top  = -1;
    grid->max_show_left = -1;
    grid->min_top  = 10000;
    grid->min_left = 10000;
    grid->max_top  = -1;
    grid->max_left = -1;
    grid->x_interval = 0;
    grid->y_interval = 0;

    /*
     * 遍历 layout_new 建出来的 item, 从它们的 css 坐标反推网格形状:
     *   left 相同的算同一列 -> row_num++;  top 相同的算同一行 -> col_num++。
     * 同时统计 min/max_left|top(全部 item)与 min/max_show_left|top(可视 item)。
     */
    last_left = -1;
    last_top = -1;
    list_for_each_child_element(p, &grid->elm) {
        struct rect rect;
        int right, bottom;

        css = &p->css;
        ui_core_get_element_abs_rect(p, &rect);

        if (last_left == -1) {
            last_left = css->left;
        } else {
            if (css->left == last_left) {
                grid->row_num++;
            }
        }
        if (last_top == -1) {
            last_top = css->top;
        } else {
            if (css->top == last_top) {
                grid->col_num++;
            }
        }

        right  = css->left + css->width;
        bottom = css->top + css->height;

        if (grid->min_left > css->left) {
            grid->min_left = css->left;
        }
        if (grid->max_left < css->left) {
            grid->max_left = css->left;
        }
        if (grid->min_top > css->top) {
            grid->min_top = css->top;
        }
        if (grid->max_top < css->top) {
            grid->max_top = css->top;
        }

        if (css->left < 0 || right >> 2 > 2500 ||
            css->top < 0 || bottom >> 2 > 2500) {
            css->invisible = 1;
        } else {
            css->invisible = 0;
            if (grid->min_show_left > css->left) {
                grid->min_show_left = css->left;
            }
            if (grid->max_show_left < css->left) {
                grid->max_show_left = css->left;
            }
            if (grid->min_show_top > css->top) {
                grid->min_show_top = css->top;
            }
            if (grid->max_show_top < css->top) {
                grid->max_show_top = css->top;
            }
            if (css->left == last_left) {
                grid->show_row++;
            }
            if (css->top == last_top) {
                grid->show_col++;
            }
        }

        if (grid->page_mode == 0) {
            if (bottom < 1 || css->top > 9999) {
                css->invisible = 1;
            } else {
                css->invisible = 0;
            }
        }
    }

    /* x_interval = item 之间的平均横向空白(css 用的是 0..10000 定点比例) */
    grid->x_interval = (grid->max_left - grid->min_left) - css->width * (grid->col_num - 1);
    grid->y_interval = (grid->max_top - grid->min_top) - css->height * (grid->row_num - 1);
    if (grid->x_interval != 0 && grid->col_num > 1) {
        grid->x_interval = grid->x_interval / (grid->col_num - 1);
    }
    if (grid->y_interval != 0 && grid->row_num > 1) {
        grid->y_interval = grid->y_interval / (grid->row_num - 1);
    }

    if (grid->page_mode == 1) {
        return;
    }
    /* 有 item 显示不全 -> 打开像素级滚动 */
    if (grid->show_row < grid->row_num || grid->show_col < grid->col_num) {
        grid->pix_scroll = 1;
    }
}

/* ============================================================
 *  grid_scroll - 内部函数 (internal fastcc)
 *  普通网格高亮滚动: 根据 index 计算目标位置, 平移所有 item
 * ============================================================ */
static void grid_scroll(struct ui_grid *grid, int index, u8 key_direction, u8 init)
{
    struct layout *items;
    int item_width, item_height;
    int row_span;
    int left, top;
    int i, j;
    int hi_index;
    u8 dir;

    item_highlight(&grid->item[index].elm, 1);
    hi_index = grid->hi_index;
    if (hi_index == index) {
        return;
    }

    items = grid->item;

    {
        struct rect rect;

        if (grid->pix_scroll) {
            /* 像素滚动: 目标 item 只要有一边露出 dc 的绘制区就要滚 */
            ui_core_get_element_abs_rect(&items[index].elm, &rect);
            if (rect.left < grid->dc.draw.left ||
                rect.width + rect.left > grid->dc.draw.width + grid->dc.draw.left ||
                rect.top < grid->dc.draw.top ||
                rect.height + rect.top > grid->dc.draw.height + grid->dc.draw.top) {
                goto do_scroll;
            }
        } else {
            if (items[index].elm.css.invisible) {
                goto do_scroll;
            }
        }
    }

    {
        /* 目标已经完整可见: 只要重画旧、新两个 item */
        if (init == 0) {
            if (grid->elm.dc->buf_num == 2) {
                if (platform_api->get_draw_context) {
                    platform_api->get_draw_context(grid->elm.dc);
                }
            }
            ui_core_redraw(&grid->item[grid->hi_index].elm);
            ui_core_redraw(&grid->item[index].elm);
            if (grid->elm.dc->buf_num == 2) {
                if (platform_api->put_draw_context) {
                    platform_api->put_draw_context(grid->elm.dc);
                }
            }
        }
        grid->hi_index = index;
        return;
    }

do_scroll:
    item_width  = items[index].elm.css.width;
    item_height = items[index].elm.css.height;

    if (grid->page_mode == GRID_PAGE_MODE) {
        /* 翻页模式: 目标 item 的坐标直接由行列号算出 */
        items[index].elm.css.left = (grid->x_interval + item_width) *
                                    (index % grid->col_num % grid->show_col) +
                                    grid->min_show_left;
        items[index].elm.css.top  = (grid->y_interval + item_height) *
                                    ((index / grid->col_num) % grid->show_row) +
                                    grid->min_show_top;
    } else {
        dir = key_direction;
        if (dir == UI_KEY_DOWN) {
            dir = UI_KEY_RIGHT;
        }
        if (dir == UI_KEY_UP) {
            dir = UI_KEY_LEFT;
        }

        if (grid->row_num > 1) {
            /* 多行: 纵向滚动 */
            switch (dir) {
            case UI_KEY_RIGHT:
                if (grid->pix_scroll) {
                    if (index == 0) {
                        /* 回到第一项: 整体上移到 top = 0 */
                        top = items[index].elm.css.top;
                        for (j = 0; j < grid->avail_item_num; j++) {
                            struct layout *cur = grid->item;
                            int t = cur[j].elm.css.top - top;
                            int bottom;

                            cur[j].elm.css.top = t;
                            bottom = cur[j].elm.css.height + t;
                            if (t > 9999 || bottom < 1) {
                                cur[j].elm.css.invisible = 1;
                            } else {
                                cur[j].elm.css.invisible = 0;
                            }
                        }
                    } else {
                        top = items[index].elm.css.top;
                        if (top + item_height > 10000) {
                            int off = 10000 - (top + item_height);

                            for (j = 0; j < grid->avail_item_num; j++) {
                                struct layout *cur = grid->item;
                                int t = cur[j].elm.css.top + off;
                                int bottom;

                                cur[j].elm.css.top = t;
                                bottom = cur[j].elm.css.height + t;
                                if (t > 9999 || bottom < 1) {
                                    cur[j].elm.css.invisible = 1;
                                } else {
                                    cur[j].elm.css.invisible = 0;
                                }
                            }
                        }
                    }
                } else if (index == 0) {
                    items[index].elm.css.left = grid->min_show_left;
                    items[index].elm.css.top  = grid->min_show_top;
                } else {
                    if (index % grid->col_num == 0) {
                        items[index].elm.css.left = grid->min_show_left;
                        items[index].elm.css.top = items[hi_index].elm.css.top +
                                                   item_height + grid->y_interval;
                        if ((items[index].elm.css.top + item_height) >> 2 > 2500) {
                            items[index].elm.css.top = items[hi_index].elm.css.top;
                        }
                    } else {
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                        items[index].elm.css.top = items[hi_index].elm.css.top;
                    }
                }
                break;

            case UI_KEY_LEFT:
                if (grid->pix_scroll) {
                    if (grid->hi_index < index) {
                        /* 从头绕回尾: 整体下移让最后一项贴底 */
                        int off = 10000 - item_height - items[index].elm.css.top;

                        for (j = 0; j < grid->avail_item_num; j++) {
                            struct layout *cur = grid->item;
                            int t = off + cur[j].elm.css.top;
                            int bottom;

                            cur[j].elm.css.top = t;
                            bottom = cur[j].elm.css.height + t;
                            if (t > 9999 || bottom < 1) {
                                cur[j].elm.css.invisible = 1;
                            } else {
                                cur[j].elm.css.invisible = 0;
                            }
                        }
                    } else {
                        top = items[index].elm.css.top;
                        if (top < 0) {
                            for (j = 0; j < grid->avail_item_num; j++) {
                                struct layout *cur = grid->item;
                                int t = cur[j].elm.css.top - top;
                                int bottom;

                                cur[j].elm.css.top = t;
                                bottom = cur[j].elm.css.height + t;
                                if (t > 9999 || bottom < 1) {
                                    cur[j].elm.css.invisible = 1;
                                } else {
                                    cur[j].elm.css.invisible = 0;
                                }
                            }
                        }
                    }
                } else if (grid->hi_index < index) {
                    int col;

                    if (index % grid->col_num < grid->show_col) {
                        col = index % grid->show_col;
                    } else {
                        col = grid->show_col - 1;
                    }
                    items[index].elm.css.left = (grid->x_interval + item_width) * col +
                                                grid->min_show_left;
                    items[index].elm.css.top  = grid->max_show_top;
                } else {
                    if (index % grid->col_num == grid->col_num - 1) {
                        items[index].elm.css.left = grid->max_show_left;
                        items[index].elm.css.top = items[hi_index].elm.css.top -
                                                   item_height - grid->y_interval;
                        if (items[index].elm.css.top < 0) {
                            items[index].elm.css.top = items[hi_index].elm.css.top;
                        }
                    } else {
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                        items[index].elm.css.top = items[hi_index].elm.css.top;
                    }
                }
                break;

            default:
                break;
            }
        } else {
            /* 单行: 横向滚动 */
            switch (dir) {
            case UI_KEY_RIGHT:
                if (grid->pix_scroll) {
                    if (index == 0) {
                        left = items[index].elm.css.left;
                        for (j = 0; j < grid->avail_item_num; j++) {
                            struct layout *cur = grid->item;
                            int l = cur[j].elm.css.left - left;
                            int right;

                            cur[j].elm.css.left = l;
                            right = cur[j].elm.css.width + l;
                            if (l > 9999 || right < 1) {
                                cur[j].elm.css.invisible = 1;
                            } else {
                                cur[j].elm.css.invisible = 0;
                            }
                        }
                    } else {
                        left = items[index].elm.css.left;
                        if (left + item_width > 10000) {
                            int off = 10000 - (left + item_width);

                            for (j = 0; j < grid->avail_item_num; j++) {
                                struct layout *cur = grid->item;
                                int l = cur[j].elm.css.left + off;
                                int right;

                                cur[j].elm.css.left = l;
                                right = cur[j].elm.css.width + l;
                                if (l > 9999 || right < 1) {
                                    cur[j].elm.css.invisible = 1;
                                } else {
                                    cur[j].elm.css.invisible = 0;
                                }
                            }
                        }
                    }
                } else if (index == 0) {
                    /* @note 原厂这里给 left 赋的是 min_show_top, 1:1 还原 */
                    items[0].elm.css.left = grid->min_show_top;
                } else {
                    if (index % grid->col_num == 0) {
                        items[index].elm.css.left = grid->min_show_left;
                        items[index].elm.css.left = items[hi_index].elm.css.left +
                                                    item_width + grid->x_interval;
                        if ((items[index].elm.css.left + item_width) >> 2 > 2500) {
                            items[index].elm.css.left = items[hi_index].elm.css.left;
                        }
                    } else {
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                    }
                }
                break;

            case UI_KEY_LEFT:
                if (grid->pix_scroll) {
                    if (grid->hi_index < index) {
                        int off = 10000 - item_width - items[index].elm.css.left;

                        for (j = 0; j < grid->avail_item_num; j++) {
                            struct layout *cur = grid->item;
                            int l = off + cur[j].elm.css.left;
                            int right;

                            cur[j].elm.css.left = l;
                            right = cur[j].elm.css.width + l;
                            if (l > 9999 || right < 1) {
                                cur[j].elm.css.invisible = 1;
                            } else {
                                cur[j].elm.css.invisible = 0;
                            }
                        }
                    } else {
                        left = items[index].elm.css.left;
                        if (left < 0) {
                            for (j = 0; j < grid->avail_item_num; j++) {
                                struct layout *cur = grid->item;
                                int l = cur[j].elm.css.left - left;
                                int right;

                                cur[j].elm.css.left = l;
                                right = cur[j].elm.css.width + l;
                                if (l > 9999 || right < 1) {
                                    cur[j].elm.css.invisible = 1;
                                } else {
                                    cur[j].elm.css.invisible = 0;
                                }
                            }
                        }
                    }
                } else if (grid->hi_index < index) {
                    /* @note 原厂这里给 left 赋的是 max_show_top, 1:1 还原 */
                    items[index].elm.css.left = grid->max_show_top;
                } else {
                    if (index % grid->col_num == grid->col_num - 1) {
                        items[index].elm.css.left = grid->max_show_left;
                        /* @note 原厂这里减的是 item_height / y_interval, 1:1 还原 */
                        items[index].elm.css.left = items[hi_index].elm.css.left -
                                                    item_height - grid->y_interval;
                        if (items[index].elm.css.left < 0) {
                            items[index].elm.css.left = items[hi_index].elm.css.left;
                        }
                    } else {
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                        items[index].elm.css.left = items[hi_index].elm.css.left;
                    }
                }
                break;

            default:
                break;
            }
        }
    }

    items[index].elm.css.invisible = 0;

    /* 目标 item 定好位后, 前后两个方向把其余 item 依次排开 */
    row_span = (grid->col_num - 1) * (grid->x_interval + item_width);

    left = items[index].elm.css.left;
    top  = items[index].elm.css.top;
    for (i = index + 1; i < grid->avail_item_num; i++) {
        struct layout *cur;

        if (i % grid->col_num == 0) {
            left = left - row_span;
            if (left < 0 && left > -5) {
                left = 0;
            }
            top = top + item_height + grid->y_interval;
        } else {
            left = left + item_width + grid->x_interval;
        }
        cur = grid->item;
        cur[i].elm.css.left = left;
        cur[i].elm.css.top = top;
        if (top < 0 || (top + item_height) >> 2 > 2500 ||
            left < 0 || (left + item_width) >> 2 > 2500) {
            cur[i].elm.css.invisible = 1;
        } else {
            cur[i].elm.css.invisible = 0;
        }
    }

    left = items[index].elm.css.left;
    top  = items[index].elm.css.top;
    for (i = index - 1; i >= 0; i--) {
        struct layout *cur;

        if (i % grid->col_num == grid->col_num - 1) {
            left = left + row_span;
            top = top - item_height - grid->y_interval;
            if (top < 0 && top > -5) {
                top = 0;
            }
        } else {
            left = left - item_width - grid->x_interval;
            if (left < 0 && left > -5) {
                left = 0;
            }
        }
        cur = grid->item;
        cur[i].elm.css.left = left;
        cur[i].elm.css.top = top;
        if (top < 0 || (top + item_height) >> 2 > 2500 ||
            left < 0 || (left + item_width) >> 2 > 2500) {
            cur[i].elm.css.invisible = 1;
        } else {
            cur[i].elm.css.invisible = 0;
        }
    }

    if (grid->pix_scroll) {
        if (init == 0) {
            item_highlight(&grid->item[grid->hi_index].elm, 0);
            item_highlight(&grid->item[index].elm, 1);
            ui_core_redraw(grid);
        }
        grid->hi_index = index;
    } else {
        grid->hi_index = index;
        if (init == 0) {
            ui_core_redraw(grid);
        }
    }
}

/* ============================================================
 *  grid_scroll_dynamic - 内部函数 (internal fastcc)
 *  动态网格高亮滚动: 约 1000 行 IR, 结构与 grid_scroll 类似
 *  但要在 min/max_row/col 之间滑动窗口
 * ============================================================ */
static int grid_scroll_dynamic(struct ui_grid *grid, int index, u8 key_direction, u8 init)
{
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    struct element_css *css;
    struct element *p;
    struct layout *items;
    int total = dynamic->grid_col_num * dynamic->grid_row_num;
    int row, col;
    int dindex, local;
    int item_width, item_height;
    int row_span;
    int left, top;
    int base, cur_dindex, row_dindex;
    int min_left, max_left, min_top, max_top;
    int min_show_left, max_show_left, min_show_top, max_show_top;
    int xval, yval;
    int new_hi;
    int i, j;

    if (dynamic->drow_num == 0) {
        return 0;
    }

    row = (index / dynamic->dcol_num) % dynamic->drow_num;
    col = index % dynamic->dcol_num;

    if (grid->page_mode == GRID_PAGE_MODE) {
        row = (index / dynamic->dcol_num) % dynamic->grid_show_row;
        col = col % dynamic->grid_show_col;
    } else if ((u8)(key_direction - UI_KEY_RIGHT) < 2) {
        /* UI_KEY_RIGHT / UI_KEY_DOWN —— 原厂就是这种范围判定, 不是两次相等比较 */
        /* 往后翻: 目标停在动态窗口的末行/末列 */
        row = row - dynamic->min_row_index;
        if (row > dynamic->grid_row_num - 1) {
            row = dynamic->grid_row_num - 1;
        } else if (row < 0) {
            row = 0;
        }
        col = col - dynamic->min_col_index;
        if (col > dynamic->grid_col_num - 1) {
            col = dynamic->grid_col_num - 1;
        } else if (col < 0) {
            col = 0;
        }
    } else if ((u8)(key_direction - UI_KEY_LEFT) < 2) {
        /* UI_KEY_LEFT / UI_KEY_UP */
        /* 往前翻: 先把行列夹进当前窗口, 再换算成窗口内偏移 */
        if (row > dynamic->max_row_index) {
            row = dynamic->max_row_index;
        } else if (row < dynamic->min_row_index) {
            row = dynamic->min_row_index;
        }
        if (col > dynamic->max_col_index) {
            col = dynamic->max_col_index;
        } else if (col < dynamic->min_col_index) {
            col = dynamic->min_col_index;
        }
        row = row - dynamic->min_row_index;
        col = col - dynamic->min_col_index;
    }

    dindex = dynamic->grid_col_num * row + col;
    local  = (grid->col_num - dynamic->grid_col_num) * row + dindex;

    item_highlight(&grid->item[local].elm, 1);
    if (dynamic->dhi_index == index) {
        return 1;
    }

    items = grid->item;
    item_width  = items[local].elm.css.width;
    item_height = items[local].elm.css.height;

    if (grid->page_mode == GRID_PAGE_MODE) {
        left = (grid->x_interval + item_width) * col + grid->min_show_left;
        items[local].elm.css.left = left;
        top = (grid->y_interval + item_height) * row + grid->min_show_top;
        items[local].elm.css.top = top;
    } else {
        /* 把目标 item 拉回可视窗口, 整体平移所有 item */
        int off_top, off_left;

        top  = items[local].elm.css.top;
        off_top = (top < 0) ? -top : 0;
        left = items[local].elm.css.left;
        off_left = (left < 0) ? -left : 0;
        if (dynamic->grid_yval < top + item_height) {
            off_top = dynamic->grid_yval - (top + item_height);
        }
        if (dynamic->grid_xval < left + item_width) {
            off_left = dynamic->grid_xval - (left + item_width);
        }
        for (j = 0; j < grid->avail_item_num; j++) {
            struct layout *cur = grid->item;

            cur[j].elm.css.top = cur[j].elm.css.top + off_top;
            cur[j].elm.css.left = cur[j].elm.css.left + off_left;
            cur[j].elm.css.invisible = 1;
        }
        left = items[local].elm.css.left;
        top  = items[local].elm.css.top;
    }

    items[local].elm.css.invisible = 0;

    row_span = (grid->col_num - 1) * (grid->x_interval + item_width);

    /* 以目标 item 为基准, 先往前再往后把其余 item 排开(全部先置隐藏) */
    for (i = local; --i >= 0; ) {
        struct layout *cur;

        if (i % grid->col_num == grid->col_num - 1) {
            left = left + row_span;
            top = top - item_height - grid->y_interval;
            if (top < 0 && top > -5) {
                top = 0;
            }
        } else {
            left = left - item_width - grid->x_interval;
            if (left < 0 && left > -5) {
                left = 0;
            }
        }
        cur = grid->item;
        cur[i].elm.css.left = left;
        cur[i].elm.css.top = top;
        cur[i].elm.css.invisible = 1;
    }

    left = items[local].elm.css.left;
    top  = items[local].elm.css.top;
    for (i = local; ++i < grid->avail_item_num; ) {
        struct layout *cur;

        if (i % grid->col_num == 0) {
            left = left - row_span;
            if (left < 0 && left > -5) {
                left = 0;
            }
            top = top + item_height + grid->y_interval;
        } else {
            left = left + item_width + grid->x_interval;
        }
        cur = grid->item;
        cur[i].elm.css.left = left;
        cur[i].elm.css.top = top;
        cur[i].elm.css.invisible = 1;
    }

    /*
     * 排好版后从目标 item 向两侧扫一遍: 给每个 item 的子控件发
     * ON_CHANGE_UPDATE_ITEM 填内容, 同时重新统计动态窗口的
     * min/max_(show_)row/col_index。
     */
    xval = dynamic->grid_xval;
    yval = dynamic->grid_yval;
    base = dynamic->base_index_once + index;
    row_dindex = (dindex < dynamic->grid_col_num) ? base - dindex : -1;

    min_left = xval;
    min_top  = yval;
    max_left = -1;
    max_top  = -1;
    min_show_left = xval;
    min_show_top  = yval;
    max_show_left = -1;
    max_show_top  = -1;
    cur_dindex = base;

    for (i = dindex; i >= 0; i--) {
        int local_i;

        if (i != dindex) {
            if (i % dynamic->grid_col_num == dynamic->grid_col_num - 1) {
                if (row_dindex == -1) {
                    row_dindex = cur_dindex;
                }
                cur_dindex = i % dynamic->grid_col_num + cur_dindex - dynamic->dcol_num;
            } else {
                cur_dindex = cur_dindex - 1;
            }
        }

        local_i = (grid->col_num - dynamic->grid_col_num) *
                  ((i / dynamic->dcol_num) % dynamic->drow_num) + i;

        list_for_each_child_element(p, &grid->item[local_i].elm) {
            if (p->handler && p->handler->onchange) {
                p->handler->onchange(p, ON_CHANGE_UPDATE_ITEM, (void *)cur_dindex);
            }
        }

        css = &grid->item[local_i].elm.css;
        left = css->left;
        if (min_left > left) {
            dynamic->min_col_index = cur_dindex % dynamic->dcol_num;
            min_left = left;
        }
        if (max_left < left) {
            dynamic->max_col_index = cur_dindex % dynamic->dcol_num;
            max_left = left;
        }
        top = css->top;
        if (min_top > top) {
            dynamic->min_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
            min_top = top;
        }
        if (max_top < top) {
            dynamic->max_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
            max_top = top;
        }

        if (top + item_height < 0 || top >> 2 > dynamic->grid_yval >> 2 ||
            left + item_width < 0 || left >> 2 > dynamic->grid_xval >> 2) {
            css->invisible = 1;
        } else {
            css->invisible = 0;
            if (min_show_left > left) {
                dynamic->min_show_col_index = cur_dindex % dynamic->dcol_num;
                min_show_left = left;
            }
            if (max_show_left < left) {
                dynamic->max_show_col_index = cur_dindex % dynamic->dcol_num;
                max_show_left = left;
            }
            if (min_show_top > top) {
                dynamic->min_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                min_show_top = top;
            }
            if (max_show_top < top) {
                dynamic->max_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                max_show_top = top;
            }
        }
    }

    cur_dindex = base;
    i = dindex;
    while (++i < total) {
        int local_i;

        if (i % dynamic->grid_col_num == 0) {
            cur_dindex = dynamic->dcol_num + row_dindex;
            row_dindex = cur_dindex;
        } else {
            cur_dindex = cur_dindex + 1;
        }

        local_i = (grid->col_num - dynamic->grid_col_num) *
                  ((i / dynamic->dcol_num) % dynamic->drow_num) + i;

        list_for_each_child_element(p, &grid->item[local_i].elm) {
            if (p->handler && p->handler->onchange) {
                p->handler->onchange(p, ON_CHANGE_UPDATE_ITEM, (void *)cur_dindex);
            }
        }

        css = &grid->item[local_i].elm.css;
        left = css->left;
        if (min_left > left) {
            dynamic->min_col_index = cur_dindex % dynamic->dcol_num;
            min_left = left;
        }
        if (max_left < left) {
            dynamic->max_col_index = cur_dindex % dynamic->dcol_num;
            max_left = left;
        }
        top = css->top;
        if (min_top > top) {
            dynamic->min_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
            min_top = top;
        }
        if (max_top < top) {
            dynamic->max_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
            max_top = top;
        }

        if (top < 0 || (top + item_height) >> 2 > dynamic->grid_yval >> 2 ||
            left < 0 || (left + item_width) >> 2 > dynamic->grid_xval >> 2) {
            css->invisible = 1;
        } else {
            css->invisible = 0;
            if (min_show_left > left) {
                dynamic->min_show_col_index = cur_dindex % dynamic->dcol_num;
                min_show_left = left;
            }
            if (max_show_left < left) {
                dynamic->max_show_col_index = cur_dindex % dynamic->dcol_num;
                max_show_left = left;
            }
            if (min_show_top > top) {
                dynamic->min_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                min_show_top = top;
            }
            if (max_show_top < top) {
                dynamic->max_show_row_index = (cur_dindex / dynamic->dcol_num) % dynamic->drow_num;
                max_show_top = top;
            }
        }
    }

    dynamic->base_index_once = 0;

    row = (base / dynamic->dcol_num) % dynamic->drow_num - dynamic->min_row_index;
    col = base % dynamic->dcol_num - dynamic->min_col_index;
    new_hi = grid->col_num * row + col;

    if (grid->pix_scroll) {
        if (init == 0) {
            item_highlight(&grid->item[grid->hi_index].elm, 0);
            item_highlight(&grid->item[new_hi].elm, 1);
            ui_core_redraw(grid);
        }
        grid->hi_index = new_hi;
        dynamic->dhi_index = base;
    } else {
        grid->hi_index = new_hi;
        dynamic->dhi_index = base;
        if (init == 0) {
            ui_core_redraw(grid);
        }
    }

    return 1;
}

/* ============================================================
 *  ui_grid_highlight_child - line: 2878 (internal fastcc)
 *  被 ui_grid_highlight_item / ui_grid_state_reset 调用
 * ============================================================ */
static void ui_grid_highlight_child(struct ui_grid *grid, int item, int init)
{
    int offset;
    int i;

    if (item < 0) {
        return;
    }

    if (grid->page_mode == 0) {
        /* 目标 item 有一部分落在可视区(0..10000 定点比例)之外: 整体平移 y */
        if (grid->item[item].elm.css.top < 0 ||
            grid->item[item].elm.css.top + grid->item[item].elm.css.height > 10000) {
            offset = grid->item[item].elm.css.height / -2 + 5000;
            offset -= grid->item[item].elm.css.top;
            for (i = 0; i < grid->avail_item_num; i++) {
                int t = offset + grid->item[i].elm.css.top;
                int bottom;

                grid->item[i].elm.css.top = t;
                bottom = t + grid->item[i].elm.css.height;
                if (t > 9999 || bottom < 1) {
                    grid->item[i].elm.css.invisible = 1;
                } else {
                    grid->item[i].elm.css.invisible = 0;
                }
            }
        }
    }

    /* init 且 item==0: 直接高亮 item[0], 不走滚动逻辑 */
    if (item == 0 && init) {
        item_highlight(&grid->item[0].elm, 1);
        grid->hi_index = 0;
        if (grid->dynamic) {
            grid->dynamic->dhi_index = 0;
        }
        return;
    }

    if (grid->dynamic) {
        grid_scroll_dynamic(grid, item, 0, init);
    } else {
        grid_scroll(grid, item, 0, init);
    }
}

/* ============================================================
 *  ui_grid_on_focus - line: 3000
 * ============================================================ */
void ui_grid_on_focus(struct ui_grid *grid)
{
    if (grid->onfocus == 0) {
        grid->onfocus = 1;
        ui_core_element_on_focus(&grid->elm, 1);
    }
}

/* ============================================================
 *  ui_grid_lose_focus - line: 3008
 * ============================================================ */
void ui_grid_lose_focus(struct ui_grid *grid)
{
    if (grid->onfocus == 1) {
        grid->onfocus = 0;
        ui_core_element_on_focus(&grid->elm, 0);
    }
}

/* ============================================================
 *  ui_grid_state_reset - line: 3016
 * ============================================================ */
void ui_grid_state_reset(struct ui_grid *grid, int highlight_item)
{
    struct ui_grid_info *info;
    struct element *p;

    info = platform_api->load_widget_info((void *)grid->info, 0xff);

    if (grid->dynamic) {
        return;
    }

    if (info) {
        /* 资源还能加载: 直接整块删掉 item[], 下次 show 时重建 */
        if (grid->item) {
            layout_delete(grid->item, info->head.ctrl_num);
            ui_core_element_append_child(&grid->elm, NULL);
            grid->item = NULL;
        }
        grid->hi_index = highlight_item;
        return;
    }

    /* 资源已不可加载: 就地把每个 item 的子控件全部重建一遍 */
    grid->avail_item_num = 0;
    list_for_each_child_element(p, &grid->elm) {
        struct layout_info *item_info;
        struct ui_ctrl_info_head *head;
        int i;

        ui_core_release_child_probe(p);
        ui_core_release_child(p);
        p->css.invisible = 0;
        p->state = 0;

        item_info = grid->item_info;
        head = (struct ui_ctrl_info_head *)item_info->ctrl;
        for (i = 0; i < item_info->head.ctrl_num; i++) {
            const struct control_ops *ops;

            /* 原为遍历 .control_ops 段; 移植后改为查显式注册表, 见 control.h */
            ops = get_control_ops_by_type(head->type);
            if (!ops) {
                puts("!!!!!unknow:ctrl_type");
                break;
            }

            ops->new(&head->type, p);
            head = (struct ui_ctrl_info_head *)((u8 *)&head->type + head->len);
        }
        grid->avail_item_num++;
    }

    if (grid->hi_index >= 0) {
        item_highlight(&grid->item[grid->hi_index].elm, 0);
    }
    if (highlight_item >= 0) {
        if (grid->onfocus != -1) {
            item_highlight(&grid->item[highlight_item].elm, 1);
            grid->hi_index = highlight_item;
        }
    }
}

/* ============================================================
 *  ui_grid_highlight_item - line: 3069
 * ============================================================ */
int ui_grid_highlight_item(struct ui_grid *grid, int item, bool yes)
{
    struct ui_grid_dynamic *dynamic;
    int index;
    int row, col;

    if (!grid || item < 0) {
        return -EINVAL;
    }
    if (grid->avail_item_num < item) {
        return -EINVAL;
    }

    if (yes) {
        ui_grid_highlight_child(grid, item, 0);
        return 0;
    }

    /* yes == 0: 取消高亮 —— 需要把"列表下标"换算成"屏上 item 下标" */
    if (!grid->item) {
        return 0;
    }

    dynamic = grid->dynamic;
    if (!dynamic) {
        index = item;
    } else {
        row = (item / dynamic->dcol_num) % dynamic->drow_num;
        col = item % dynamic->dcol_num;

        if (row > dynamic->max_row_index) {
            row = dynamic->max_row_index;
        } else if (row < dynamic->min_row_index) {
            row = dynamic->min_row_index;
        }
        if (col > dynamic->max_col_index) {
            col = dynamic->max_col_index;
        } else if (col < dynamic->min_col_index) {
            col = dynamic->min_col_index;
        }

        index = (row - dynamic->min_row_index) * grid->col_num +
                (col - dynamic->min_col_index);
    }

    item_highlight(&grid->item[index].elm, 0);
    ui_core_redraw(&grid->item[index].elm);

    if (grid->hi_index == index) {
        grid->hi_index = -1;
        if (grid->dynamic) {
            grid->dynamic->dhi_index = -1;
        }
    }

    return 0;
}

/* ============================================================
 *  ui_grid_highlight_item_by_id - line: 3117
 * ============================================================ */
int ui_grid_highlight_item_by_id(int id, int item, bool yes)
{
    struct element *elm = ui_core_get_element_by_id(id);

    return ui_grid_highlight_item((struct ui_grid *)elm, item, yes);
}

/* ============================================================
 *  ui_grid_enable - line: 3211
 *  空函数, 原库 define void() 直接 ret
 * ============================================================ */
void ui_grid_enable(void)
{
}

/* ---------- 事件处理 ---------- */
static int grid_ontouch(void *_elm, struct element_touch_event *e)
{
    struct ui_grid *grid = _elm;
    char touch_index = grid->touch_index;
    int xoffset, yoffset;
    int ret;

    if (e->event == ELM_EVENT_TOUCH_DOWN) {
        int index = -1;
        {
        struct rect rect;
        int i;

        for (i = 0; i < grid->avail_item_num; i++) {
            /*
             * 像素级滚动且非动态列表时不看 invisible ——
             * 此时 item 是整体平移的, invisible 只用于裁剪显示。
             */
            if ((!grid->pix_scroll || grid->dynamic) &&
                grid->item[i].elm.css.invisible) {
                continue;
            }
            ui_core_get_element_abs_rect(&grid->item[i].elm, &rect);
            if (in_rect(&rect, &e->pos)) {
                index = i;
                break;
            }
        }
        }
        grid->touch_index = index;
    }

    if (grid->handler->ontouch) {
        if (grid->handler->ontouch(grid, e)) {
            return 1;
        }
    }

    switch (e->event) {
    case ELM_EVENT_TOUCH_DOWN:
        grid->pos.x = e->pos.x;
        grid->pos.y = e->pos.y;
        break;

    case ELM_EVENT_TOUCH_MOVE:
        if (touch_index >= 0) {
            grid->touch_index = -1;
        }
        xoffset = e->pos.x - grid->pos.x;
        yoffset = e->pos.y - grid->pos.y;
        if (!grid->pix_scroll) {
            break;
        }
        switch (grid->slide_direction) {
        case SCROLL_DIRECTION_LR:
            ret = ui_grid_slide(grid, SCROLL_DIRECTION_LR, xoffset);
            break;
        case SCROLL_DIRECTION_UD:
            ret = ui_grid_slide(grid, SCROLL_DIRECTION_UD, yoffset);
            break;
        default:
            /* 未指定方向: 谁的位移大就往哪边滚 */
            if ((yoffset > 0 ? yoffset : -yoffset) >
                (xoffset > 0 ? xoffset : -xoffset)) {
                ret = ui_grid_slide(grid, SCROLL_DIRECTION_UD, yoffset);
            } else {
                ret = ui_grid_slide(grid, SCROLL_DIRECTION_LR, xoffset);
            }
            break;
        }
        if (ret) {
            grid->pos.x = e->pos.x;
            grid->pos.y = e->pos.y;
        }
        break;

    case ELM_EVENT_TOUCH_UP:
        if (touch_index >= 0) {
            grid->touch_index = -1;
        }
        break;

    default:
        break;
    }

    return 1;
}

static int grid_onkey(void *_elm, struct element_key_event *e)
{
    struct ui_grid *grid = _elm;
    struct ui_grid_dynamic *dynamic = grid->dynamic;
    int index;

    if (grid->handler->onkey) {
        if (grid->handler->onkey(grid, e)) {
            return 1;
        }
    }

    /* hi_index < 0 表示当前没有高亮项, 方向键不处理 */
    if (grid->hi_index < 0) {
        return 0;
    }

    /*
     * 上/下键在 grid 里等价于左/右键 —— 原厂把 e->value 改写后【跳到】对应
     * case 上执行, 所以这里保留 goto: 用 fall through 会改变 case 的书写顺序。
     */
    switch (e->value) {
    case UI_KEY_DOWN:
        e->value = UI_KEY_RIGHT;
        goto key_right;
    case UI_KEY_UP:
        e->value = UI_KEY_LEFT;
        goto key_left;
    case UI_KEY_RIGHT:
key_right:
        if (dynamic) {
            index = dynamic->dhi_index + 1;
            if (index >= dynamic->drow_num * dynamic->dcol_num) {
                index -= dynamic->drow_num * dynamic->dcol_num;
            }
        } else {
            index = grid->hi_index + 1;
            if (index >= grid->avail_item_num) {
                index -= grid->avail_item_num;
            }
        }
        break;
    case UI_KEY_LEFT:
key_left:
        if (dynamic) {
            index = dynamic->dhi_index - 1;
            if (index < 0) {
                index += dynamic->drow_num * dynamic->dcol_num;
            }
        } else {
            index = grid->hi_index - 1;
            if (index < 0) {
                index += grid->avail_item_num;
            }
        }
        break;
    default:
        return 0;
    }

    item_highlight(&grid->item[grid->hi_index].elm, 0);

    if (dynamic) {
        return grid_scroll_dynamic(grid, index, e->value, 0);
    }

    grid_scroll(grid, index, e->value, 0);
    return 1;
}

static int grid_onchange(void *_elm, enum element_change_event event, void *arg)
{
    struct ui_grid *grid = _elm;
    struct ui_grid_info *info;
    struct element *p;

    info = platform_api->load_widget_info((void *)grid->info, 0xff);

    /*
     * @note 应用层 onchange 返回真时通常吃掉事件, 但 RELEASE_PROBE/RELEASE
     *       例外, 必须继续往下走: 前者要摘链, 后者要释放内存。
     */
    if (grid->handler->onchange) {
        if (grid->handler->onchange(grid, event, arg) &&
            event != ON_CHANGE_RELEASE_PROBE && event != ON_CHANGE_RELEASE) {
            return 1;
        }
    }

    switch (event) {
    case ON_CHANGE_HIDE:
        if (grid->onfocus == 1) {
            ui_core_element_on_focus(&grid->elm, 0);
            grid->onfocus = 0;
        }
        break;

    case ON_CHANGE_SHOW_PROBE:
        if (!grid->item) {
            /* 首次显示: 此时才真正展开 item[], 省 RAM */
            ui_grid_child_init(grid, info);
            ui_grid_highlight_child(grid, grid->hi_index, 1);
        }
        if (grid->hi_index >= 0) {
            if (grid->onfocus == 0) {
                grid->onfocus = 1;
                ui_core_element_on_focus(&grid->elm, 1);
            }
        }
        break;

    case ON_CHANGE_RELEASE_PROBE:
        if (grid->info) {
            if (grid->item) {
                layout_delete_probe(grid->item, grid->ctrl_num);
            }
        } else {
            list_for_each_child_element(p, &grid->elm) {
                ui_core_release_child_probe(p);
            }
        }
        if (grid->pix_scroll) {
            platform_api->close_draw_context(&grid->dc);
        }
        break;

    case ON_CHANGE_RELEASE:
        ui_core_element_on_focus(&grid->elm, 0);
        ui_grid_release(grid);
        break;

    default:
        break;
    }

    return 1;
}

/* ============================================================
 *  new_ui_grid - line: 2949
 *
 *  【README 5.3.2 强制规则】platform_api->load_widget_info 返回的是
 *  全局 static union ui_control_info 共享缓存, 每次调用整块覆盖.
 *  因此任何可能间接触发 load_widget_info 的调用(如 layout_new)
 *  之前, 必须先把所有要用的字段先缓存到局部变量/grid 字段.
 * ============================================================ */
static void *new_ui_grid(const void *_info, struct element *parent)
{
    struct ui_grid *grid;
    struct ui_grid_info *info;
    struct element_css1 *css;

    grid = ui_core_malloc(sizeof(struct ui_grid));
    if (!grid) {
        return NULL;
    }

    info = platform_api->load_widget_info((void *)_info, 0xff);

    grid->info           = _info;
    grid->ctrl_num       = info->head.ctrl_num;
    grid->onfocus        = 0;
    grid->pix_scroll     = 0;
    grid->page_mode      = info->page_mode;
    grid->hi_index       = info->highlight_index;
    grid->touch_index    = -1;
    if (info->highlight_index == -1) {
        grid->onfocus = -1;
    }

    css = platform_api->load_css(info->head.page, info->head.css);

    /* prj 打包在 css 指针的高 3 位里(原库如此, IR 为 lshr 29) */
    ui_core_element_init(&grid->elm, info->head.id, info->head.page,
                         (u8)((u32)info->head.css >> 29),
                         css, &grid_elm_handler, info->action);
    ui_core_element_append_child(parent, &grid->elm);

    grid->handler = element_event_handler_for_id(info->head.id);
    if (!grid->handler) {
        grid->handler = &dumy_handler;
    }
    if (grid->handler->onchange) {
        grid->handler->onchange(grid, ON_CHANGE_INIT_PROBE, NULL);
    }

    ui_grid_child_init(grid, info);

    if (grid->handler->onchange) {
        grid->handler->onchange(grid, ON_CHANGE_INIT, NULL);
    }

    ui_grid_highlight_child(grid, grid->hi_index, 1);

    return grid;
}
