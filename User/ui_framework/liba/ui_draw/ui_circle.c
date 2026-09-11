/*
 * ui_circle.c —— 圆弧 / 环形进度条的位图绘制(1bpp 与 16bpp 两种位深)
 *
 * 【本工程当前无调用者】环形进度条控件没有启用, 所以这份实现没有经过真机
 *   验证 —— 启用前先自己走一遍。
 *
 * 【行号约定】bitmap_set 的 3 处 ASSERT 内嵌 __LINE__, 预期落在 31 / 38 / 42。
 *   本文件的类型定义是就地重述的(ui_draw/ui_circle.h 不在本工程的 -I 路径里),
 *   序言比较长, 所以用下面的 #line 把行号拨回, 让断言打印的行号与函数自身的
 *   逻辑行号对应。#line 之后的部分不要随意增删行。
 *
 * 【段属性】代码在 .ui_circle.text; get_rect_cover 由 jl_rect.h 的 AT_UI_RAM
 *   落在 .ui_ram。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_circle.data")
#pragma data_seg(".ui_circle.data")
#pragma code_seg(".ui_circle.text")
#endif

#include "jl_typedef.h"
#include "jl_rect.h"
#include "ui_math.h"
#include "jl_debug.h"    /* ASSERT / log_*: 显式包含, 保证本文件自包含 */

/* 以下三个类型在 ui_draw/ui_circle.h 里也有一份, 字段顺序必须与那边一致 */
typedef struct {
    uint16_t color;
    uint16_t width;
} style_t;

typedef struct {
    s16 x;
    s16 y;
    s16 disp_x;
    s16 disp_y;
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint16_t len;
    uint8_t *buf;
    int color;
    int bitdepth;
    struct rect *rect;
    style_t *style;
} area_t;

struct circle_info {
    int x, y, disp_x, disp_y;
    int center_x, center_y;
    int radius_big, radius_small;
    int angle_begin, angle_end, angle_curr;
    int color;
    int left, top, width, height;
    int bitmap_width, bitmap_height, bitmap_pitch, bitmap_depth;
    u8 *bitmap;
    int bitmap_size;
};

#define COORD_MIN     (-16384)
#line 11
static inline void bitmap_set(const area_t *area, int x, int y)
{
    u8 *bitmap = area->buf;
    u16 pitch = area->pitch;
    u16 width = area->width;
    u16 height = area->height;
    u16 offset;

    if (x < 0 || y < 0) {
        return;
    }
    if ((x >= width) || (y >= height)) {
        return;
    }

    switch (area->bitdepth) {
    case 1:
        offset = y * pitch + x / 8;
        if (offset < area->len) {
            /* 越界只靠下面的 ASSERT 报, 不在这里打印: 每个像素都会走到 */
            ASSERT((y * pitch + x / 8) < area->len);
            bitmap[offset] |= BIT(x % 8);
        } break;
    case 16: offset = y * pitch + x * 2;
        if (offset < area->len) {
            /* 同上 */

            ASSERT((offset + 1) < area->len);
            bitmap[offset] = area->style->color >> 8;
            bitmap[offset + 1] = area->style->color;
        } break;
    default: ASSERT(0, "not support!");
    }
}


static inline void draw_vert_line(int16_t x, int16_t y, int16_t len, const area_t *area)
{
    int h;
    struct rect draw, r;
    draw.left = x;
    draw.top = y;
    draw.width = 1;
    draw.height = len + 1;

    if (get_rect_cover(&draw, area->rect, &r)) {
        for (h = 0; h < r.height; h++) {
            int x1 = area->x + r.left - area->disp_x;
            int y1 = area->y + r.top + h - area->disp_y;
            bitmap_set(area, x1, y1);
        }
    }
}


static inline void draw_hort_line(int16_t x, int16_t y, int16_t len, const area_t *area)
{
    int w;
    struct rect draw, r;
    draw.left = x;
    draw.top = y;
    draw.width = len + 1;
    draw.height = 1;

    if (get_rect_cover(&draw, area->rect, &r)) {
        for (w = 0; w < r.width; w++) {
            int x1 = area->x + r.left + w - area->disp_x;
            int y1 = area->y + r.top - area->disp_y;
            bitmap_set(area, x1, y1);
        }
    }
}


static inline u8 deg_test_norm(uint16_t deg, uint16_t start, uint16_t end)
{
    if (deg >= start && deg <= end) {
        return true;
    }

    return false;
}


static inline u8 deg_test_inv(uint16_t deg, uint16_t start, uint16_t end)
{
    if (deg >= start || deg <= end) {
        return true;
    }

    return false;
}


static inline unsigned int fast_atan2(int x, int y)
{
    unsigned char negflag;
    unsigned char tempdegree;
    unsigned char comp;
    unsigned int degree;
    unsigned int ux;
    unsigned int uy;

    negflag = 0;
    if (x < 0) {
        negflag += 0x01;
        x = (0 - x);
    }
    ux = x;
    if (y < 0) {
        negflag += 0x02;
        y = (0 - y);
    }
    uy = y;

    if (uy < ux) {
        degree = (uy * 45) / ux;
        negflag += 0x10;
    } else {
        degree = (ux * 45) / uy;
    }

    comp = 0;
    tempdegree = degree;
    if (tempdegree > 22) {
        if (tempdegree < 45) {
            comp++;
        }
        if (tempdegree < 42) {
            comp++;
        }
        if (tempdegree < 38) {
            comp++;
        }
        if (tempdegree < 33) {
            comp++;
        }
    } else {
        if (tempdegree > 1) {
            comp++;
        }
        if (tempdegree > 5) {
            comp++;
        }
        if (tempdegree > 9) {
            comp++;
        }
        if (tempdegree > 14) {
            comp++;
        }
    }
    degree += comp;

    if (negflag & 0x10) {
        degree = (90 - degree);
    }

    if (negflag & 0x02) {
        if (negflag & 0x01) {
            degree = (180 + degree);
        } else {
            degree = (180 - degree);
        }
    } else {
        if (negflag & 0x01) {
            degree = (360 - degree);
        }
    }
    return degree;
}


void draw_arc(int16_t center_x, int16_t center_y, uint16_t radius, uint16_t start_angle,
              uint16_t end_angle, const area_t *area)
{
    int16_t thickness = area->style->width;
    if (thickness > radius) {
        thickness = radius;
    }

    int16_t r_out = radius;
    int16_t r_in = r_out - thickness;
    int16_t deg_base;
    int16_t deg;
    int16_t x_start[4];
    int16_t x_end[4];

    u8 (*deg_test)(uint16_t, uint16_t, uint16_t);
    if (start_angle > end_angle) {
        deg_test = deg_test_inv;
    } else {
        deg_test = deg_test_norm;
    }

    if (deg_test(270, start_angle, end_angle)) {
        draw_hort_line(center_x + radius - 1 - (thickness - 1), center_y, thickness - 1, area);
    }
    if (deg_test(90, start_angle, end_angle)) {
        draw_hort_line(center_x - r_in - (thickness - 1), center_y, thickness - 1, area);
    }
    if (deg_test(180, start_angle, end_angle)) {
        draw_vert_line(center_x, center_y - radius + 1, thickness - 1, area);
    }
    if (deg_test(0, start_angle, end_angle)) {
        draw_vert_line(center_x, center_y + r_in, thickness - 1, area);
    }

    uint32_t r_out_sqr = r_out * r_out;
    uint32_t r_in_sqr = r_in * r_in;
    int16_t xi;
    int16_t yi;


    int top, bottom;
    int direction;
    struct rect *draw = area->rect;


    if (draw->top < center_y) {
        top = (draw->top - center_y) < -radius ? -radius : (draw->top - center_y);
        bottom = (top + draw->height);
        direction = 1;
    } else {
        top = (draw->top - center_y) > radius ? radius : (draw->top - center_y);
        bottom = (top + draw->height) > radius ? radius : (top + draw->height);
        direction = 0;
    }

    for (yi = direction ? top : -top; direction ? yi < bottom : yi > -bottom; direction ? yi++ : yi--) {
        x_start[0] = x_start[1] = x_start[2] = x_start[3] = COORD_MIN;
        x_end[0] = x_end[1] = x_end[2] = x_end[3] = COORD_MIN;





        for (xi = -radius; xi < 0; xi++) {

            uint32_t r_act_sqr = xi * xi + yi * yi;
            if (r_act_sqr > r_out_sqr) {
                continue;
            }

            deg_base = fast_atan2(xi, yi);
            deg = deg_base - 180;

            if (deg_test(deg_base, start_angle, end_angle)) {
                if (x_start[0] == COORD_MIN) {
                    x_start[0] = xi;
                }
            } else if (x_start[0] != COORD_MIN && x_end[0] == COORD_MIN) {
                x_end[0] = xi - 1;
            }

            deg = 540 - deg_base;
            if (deg_test(deg, start_angle, end_angle)) {
                if (x_start[1] == COORD_MIN) {
                    x_start[1] = xi;
                }
            } else if (x_start[1] != COORD_MIN && x_end[1] == COORD_MIN) {
                x_end[1] = xi - 1;
            }

            deg = 360 - deg_base;
            if (deg_test(deg, start_angle, end_angle)) {
                if (x_start[2] == COORD_MIN) {
                    x_start[2] = xi;
                }
            } else if (x_start[2] != COORD_MIN && x_end[2] == COORD_MIN) {
                x_end[2] = xi - 1;
            }

            deg = deg_base - 180;
            if (deg_test(deg, start_angle, end_angle)) {
                if (x_start[3] == COORD_MIN) {
                    x_start[3] = xi;
                }
            } else if (x_start[3] != COORD_MIN && x_end[3] == COORD_MIN) {
                x_end[3] = xi - 1;
            }

            if (r_act_sqr < r_in_sqr) {
                break;
            }
        }


        if (x_start[0] != COORD_MIN) {
            if (x_end[0] == COORD_MIN) {
                x_end[0] = xi - 1;
            }
            draw_hort_line(center_x - x_end[0], center_y + yi, x_end[0] - x_start[0], area);
        }

        if (x_start[1] != COORD_MIN) {
            if (x_end[1] == COORD_MIN) {
                x_end[1] = xi - 1;
            }
            draw_hort_line(center_x - x_end[1], center_y - yi, x_end[1] - x_start[1], area);
        }

        if (x_start[2] != COORD_MIN) {
            if (x_end[2] == COORD_MIN) {
                x_end[2] = xi - 1;
            }
            draw_hort_line(center_x + x_end[2] - MATH_ABS(x_end[2] - x_start[2]), center_y + yi, MATH_ABS(x_end[2] - x_start[2]), area);
        }

        if (x_start[3] != COORD_MIN) {
            if (x_end[3] == COORD_MIN) {
                x_end[3] = xi - 1;
            }
            draw_hort_line(center_x + x_end[3] - MATH_ABS(x_end[3] - x_start[3]), center_y - yi, MATH_ABS(x_end[3] - x_start[3]), area);
        }
    }
}



void draw_circle_by_percent(struct circle_info *info, u8 percent)
{
    style_t style;
    area_t area;
    struct rect rect;
    area.bitdepth = info->bitmap_depth;
    area.width = info->bitmap_width;
    area.height = info->bitmap_height;
    area.pitch = info->bitmap_pitch;
    area.buf = info->bitmap;
    area.len = info->bitmap_size;
    area.x = info->x;
    area.y = info->y;
    area.disp_x = info->disp_x;
    area.disp_y = info->disp_y;

    rect.left = info->left;
    rect.top = info->top;
    rect.width = info->width;
    rect.height = info->height;

    style.color = info->color;
    style.width = info->radius_big - info->radius_small;
    area.style = &style;
    area.rect = &rect;

    info->angle_curr = (info->angle_end - info->angle_begin) * percent / 100 + info->angle_begin;
    int angle_begin = info->angle_begin - 180;
    int angle_curr = info->angle_curr - 180;
    if (angle_begin < 0) {
        angle_begin += 360;
    }
    if (angle_curr < 0) {
        angle_curr += 360;
    }
    if (angle_begin > 360) {
        angle_begin -= 360;
    }
    if (angle_curr > 360) {
        angle_curr -= 360;
    }

    if (info->angle_begin != info->angle_curr) {
        if (angle_begin == angle_curr) {
            draw_arc(info->center_x, info->center_y, info->radius_big, angle_begin, 360, &area);





            draw_arc(info->center_x, info->center_y, info->radius_big, 0, angle_begin, &area);





        } else {
            draw_arc(info->center_x, info->center_y, info->radius_big, angle_begin, angle_curr, &area);







        }
    }
}
