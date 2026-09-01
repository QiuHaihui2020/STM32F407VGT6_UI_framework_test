/*
 * ui_rotate.c —— 定点数图像旋转(双线性插值 + Alpha 混合)
 *
 * 【来源】从 cpu/br27/liba/ui_dot.a 的 ui_rotate.c.o 还原。
 *   参考 IR: cpu/br27/tools/ui_reimpl/ref_ir/ui_rotate.ll
 *   原始路径: btsdk/lib/utils/ui/ui_framework/ui_rotate.c
 *
 * 【函数原始行号(DISubprogram)】
 *   jlve_sin@48  jlve_cos@70  rotate_0@83  image_rle_buffer@224
 *   rotate_map@321  rotate_1@341
 *
 * 【本工程状态】死代码: rotate_0/1/map 无任何调用者(仅 ui_rotate.h 里有声明),
 *   但符号必须存在, 否则链接报 undefined reference。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_rotate.data.bss")
#pragma data_seg(".ui_rotate.data")
#pragma const_seg(".ui_rotate.text.const")
#pragma code_seg(".ui_rotate.text")
#endif

#include "ui/ui_rotate.h"
#include "res/rle.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef long long int int64_t;

static const int sin_tbl[91] = {
    0, 18, 36, 54, 71, 89, 107, 125, 143, 160,
    178, 195, 213, 230, 248, 265, 282, 299, 316, 333,
    350, 367, 384, 400, 416, 433, 449, 465, 481, 496,
    512, 527, 543, 558, 573, 587, 602, 616, 630, 644,
    658, 672, 685, 698, 711, 724, 737, 749, 761, 773,
    784, 796, 807, 818, 828, 839, 849, 859, 868, 878,
    887, 896, 904, 912, 920, 928, 935, 943, 949, 956,
    962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005,
    1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024, 1024
};

static inline int jlve_sin(int angle)
{
    int idx = 0;
    int sign = 1;

    if (angle < 91) {
        idx = angle;
    } else if (angle < 181) {
        idx = 180 - angle;
    } else if (angle < 271) {
        idx = angle - 180;
        sign = -1;
    } else if (angle < 361) {
        idx = 360 - angle;
        sign = -1;
    }

    return sin_tbl[idx] * sign;
}

static inline int jlve_cos(int angle)
{
    int a = angle + 90;
    a = (a > 359) ? (angle - 270) : a;
    return jlve_sin(a);
}

void rotate_0(unsigned char *src, unsigned char *src1, int sw, int sh,
              int cx, int cy, unsigned char *dst, int dw, int dh,
              int dx, int dy, struct rect *rect, int angle)
{
    int i, j;
    int w1 = sw, h1 = sh, w2 = dw, h2 = dh;
    int offset = 0;
    int64_t A, B, C, D, gray;
    int x, y, index;
    int Aa, Ba, Ca, Da;
    int alpha, alpha1;
    int x_diff, y_diff;
    uint8_t *srcp = src;
    uint8_t *srcp1 = src1;
    uint8_t *dstp = dst;
    int cos_angle = jlve_cos(angle);
    int sin_angle = jlve_sin(angle);
    uint16_t *rgb565 = (uint16_t *)src;

    for (j = rect->top; j < rect->top + rect->height; j++) {
        int dy_diff = j - dy;
        int base_x = (cx << 10) + 32 + dy_diff * sin_angle;
        int base_y = (cy << 10) + 32 + dy_diff * cos_angle;

        for (i = rect->left; i < rect->left + rect->width; i++) {
            int dx_diff = i - dx;

            x = (base_x + dx_diff * cos_angle) >> 6;
            y = (base_y - dx_diff * sin_angle) >> 6;

            if ((x | y) < 0 || x > (sw << 4) - 16 || y > (sh << 4) - 16) {
                offset += 2;
                continue;
            }

            if (x == (sw << 4) - 16) {
                x--;
            }
            if (y == (sh << 4) - 16) {
                y--;
            }

            x_diff = x & 15;
            y_diff = y & 15;
            x >>= 4;
            y >>= 4;

            index = y * sw + x;

            Aa = src1[index];
            Ba = src1[index + 1];
            Ca = src1[index + sw];
            Da = src1[index + sw + 1];

            alpha1 = ((Aa * (16 - x_diff) + Ba * x_diff) * (16 - y_diff)
                      + (16 - x_diff) * y_diff * Ca
                      + Da * x_diff * y_diff + 128) >> 8;

            if (alpha1 > 0) {
                int offset_plus_1;
                uint8_t dst_lo, dst_hi, dst_r, dst_g, dst_b;
                int dst_pixel;
                uint16_t At, Bt, Ct, Dt;
                int c;

                if (alpha1 > 255) {
                    alpha1 = 255;
                }

                At = rgb565[index];
                Bt = rgb565[index + 1];
                Ct = rgb565[index + sw];
                Dt = rgb565[index + sw + 1];

                offset_plus_1 = offset + 1;
                dst_lo = dst[offset_plus_1];
                dst_hi = dst[offset];
                dst_pixel = (dst_hi << 8) | dst_lo;

                dst_r = dst_hi & 0xF8;
                dst_g = (dst_pixel >> 3) & 0xFC;
                dst_b = dst_lo << 3;

                for (c = 0; c < 3; c++) {
                    if (c == 0) {
                        A = (int64_t)(((At >> 8) << 3) & 0xF8) * Aa;
                        B = (int64_t)(((Bt >> 8) << 3) & 0xF8) * Ba;
                        C = (int64_t)(((Ct >> 8) << 3) & 0xF8) * Ca;
                        D = (int64_t)(((Dt >> 8) << 3) & 0xF8) * Da;
                    } else {
                        switch (c) {
                        case 1:
                            A = (int64_t)((((At >> 8) | (At << 8)) >> 3) & 0xFC) * Aa;
                            B = (int64_t)((((Bt >> 8) | (Bt << 8)) >> 3) & 0xFC) * Ba;
                            C = (int64_t)((((Ct >> 8) | (Ct << 8)) >> 3) & 0xFC) * Ca;
                            D = (int64_t)((((Dt >> 8) | (Dt << 8)) >> 3) & 0xFC) * Da;
                            break;
                        case 2:
                            A = (int64_t)(At & 0xF8) * Aa;
                            B = (int64_t)(Bt & 0xF8) * Ba;
                            C = (int64_t)(Ct & 0xF8) * Ca;
                            D = (int64_t)(Dt & 0xF8) * Da;
                            break;
                        default:
                            break;
                        }
                    }

                    gray = ((A * (16 - x_diff) + B * x_diff) * (16 - y_diff)
                            + (int64_t)(16 - x_diff) * y_diff * C
                            + D * x_diff * y_diff + 128) >> 8;
                    gray = gray / alpha1;
                    if (gray > 255) {
                        gray = 255;
                    }

                    if (c == 0) {
                        dst_b = (uint8_t)(((int64_t)dst_b * 255 + 128 + (gray - dst_b) * alpha1) >> 8);
                    } else {
                        switch (c) {
                        case 1:
                            dst_g = (uint8_t)(((int64_t)dst_g * 255 + 128 + (gray - dst_g) * alpha1) >> 8);
                            break;
                        case 2:
                            dst_r = (uint8_t)(((int64_t)dst_r * 255 + 128 + (gray - dst_r) * alpha1) >> 8);
                            break;
                        default:
                            break;
                        }
                    }
                }

                {
                    uint16_t dst_rgb565;
                    dst_rgb565 = ((uint16_t)(dst_r << 8) & 0xF800) | ((uint16_t)dst_g << 3);
                    dst[offset] = dst_rgb565 >> 8;
                    dst[offset + 1] = ((dst_g << 3) & 0xE0) | (dst_b >> 3);
                }
            }

            offset += 2;
        }
    }
}

AT_UI_RAM
void image_rle_buffer(u8 *tmp, u8 *src, u8 *pixel, u8 *alpha,
                      int x, int y, int width, int height, int len)
{
    u32 alpha_addr = *(u32 *)src;
    struct rle_header head;
    u32 begin_addr;
    u32 pitch;
    u8 *pixel_tmp;
    u8 *alpha_tmp;
    u16 tmp_len;
    u16 lines;
    u16 ys;
    u16 ye;
    int i;

    if (pixel) {
        if (tmp) {
            tmp_len = ((tmp[0] << 8) | tmp[1]) - 4;
            lines = (tmp_len / width) / 3;
            pixel_tmp = tmp + 10;
            ys = (tmp[2] << 8) | tmp[3];
            ye = (tmp[4] << 8) | tmp[5];
            if (ys > y || ye < y) {
                int new_ys;
                lines = lines;
                new_ys = y - (lines >> 1);
                if (new_ys > 0) {
                    ys = new_ys;
                } else {
                    ys = 0;
                }
                ye = ys + lines;
                if (ye > height) {
                    ye = height;
                }
                tmp[2] = ys >> 8;
                tmp[3] = ys;
                tmp[4] = (ye - 1) >> 8;
                tmp[5] = (ye - 1);
                head = *(struct rle_header *)(src + ys * 4 + 4);
                pitch = head.len;
                for (i = 1; i < (int)ye - ys; i++) {
                    head = *(struct rle_header *)(src + (ys + i) * 4 + 4);
                    pitch += head.len;
                }
                begin_addr = head.addr;
                Rle_Decode(src + begin_addr + 4, pitch, pixel_tmp,
                           width * 2 * (ye - ys), 0, width * 2 * (ye - ys), 2);
                memcpy(pixel, pixel_tmp + ((y - ys) * width + x) * 2, len);
            } else {
                memcpy(pixel, pixel_tmp + ((y - ys) * width + x) * 2, len);
            }
        } else {
            head = *(struct rle_header *)(src + y * 4 + 4);
            begin_addr = head.addr;
            pitch = head.len;
            Rle_Decode(src + begin_addr + 4, pitch, pixel,
                       width * 2, x * 2, len, 2);
        }
    }

    if (alpha_addr && alpha) {
        if (tmp) {
            tmp_len = ((tmp[0] << 8) | tmp[1]) - 4;
            lines = (tmp_len / width) / 3;
            alpha_tmp = tmp + width * 2 * lines + 10;
            ys = (tmp[6] << 8) | tmp[7];
            ye = (tmp[8] << 8) | tmp[9];
            if (ys > y || ye < y) {
                int new_ys;
                new_ys = y - (lines >> 1);
                if (new_ys > 0) {
                    ys = new_ys;
                } else {
                    ys = 0;
                }
                ye = ys + lines;
                if (ye > height) {
                    ye = height;
                }
                tmp[6] = ys >> 8;
                tmp[7] = ys;
                tmp[8] = (ye - 1) >> 8;
                tmp[9] = (ye - 1);
                head = *(struct rle_header *)(src + ys * 4 + alpha_addr);
                pitch = head.len;
                for (i = 1; i < (int)ye - ys; i++) {
                    head = *(struct rle_header *)(src + (ys + i) * 4 + alpha_addr);
                    pitch += head.len;
                }
                begin_addr = head.addr;
                Rle_Decode(src + begin_addr + alpha_addr, pitch, alpha_tmp,
                           width * (ye - ys), 0, width * (ye - ys), 1);
                memcpy(alpha, alpha_tmp + (y - ys) * width + x, len);
            } else {
                memcpy(alpha, alpha_tmp + (y - ys) * width + x, len);
            }
        } else {
            head = *(struct rle_header *)(src + alpha_addr + y * 4);
            begin_addr = head.addr;
            pitch = head.len;
            Rle_Decode(src + begin_addr + alpha_addr, pitch, alpha,
                       width, x, len, 1);
        }
    }
}

void rotate_map(int sx, int sy, int scx, int scy, int *dx, int *dy,
                int dcx, int dcy, int angle)
{
    int cos_angle = jlve_cos(angle);
    int sin_angle = jlve_sin(angle);
    int x, y;

    x = (dcx << 10) + 32 + cos_angle * (sx - scx) - sin_angle * (sy - scy);
    y = (dcy << 10) + 32 + cos_angle * (sy - scy) + sin_angle * (sx - scx);

    *dx = x >> 10;
    *dy = y >> 10;
}

void rotate_1(unsigned char *tmp, unsigned char *src, int sw, int sh,
              int scx, int scy, unsigned char *dst, int dw, int dh,
              int dcx, int dcy, struct rect *rect, int angle)
{
    int i, j;
    int w1 = sw, h1 = sh, w2 = dw, h2 = dh;
    int offset = 0;
    int64_t A, B, C, D, gray;
    int x, y, index;
    int Aa, Ba, Ca, Da;
    int alpha, alpha1;
    int x_diff, y_diff;
    uint8_t *srcp = src;
    uint8_t *dstp = dst;
    int cos_angle = jlve_cos(angle);
    int sin_angle = jlve_sin(angle);

    if (tmp) {
        int tmp_len = (tmp[0] << 8) | tmp[1];
        memset(tmp + 2, 0, tmp_len);
    }

    for (j = rect->top; j < rect->top + rect->height; j++) {
        int dy_diff = j - dcy;
        int base_x = (scx << 10) + 32 + dy_diff * sin_angle;
        int base_y = (scy << 10) + 32 + dy_diff * cos_angle;

        for (i = rect->left; i < rect->left + rect->width; i++) {
            int dx_diff = i - dcx;

            x = (base_x + dx_diff * cos_angle) >> 6;
            y = (base_y - dx_diff * sin_angle) >> 6;

            if ((x | y) < 0 || x > (sw << 4) - 16 || y > (sh << 4) - 16) {
                offset += 2;
                continue;
            }

            if (x == (sw << 4) - 16) {
                x--;
            }
            if (y == (sh << 4) - 16) {
                y--;
            }

            x_diff = x & 15;
            y_diff = y & 15;
            x >>= 4;
            y >>= 4;

            {
                uint8_t AaBa[2];
                int add96 = y + 1;

                image_rle_buffer(tmp, src, NULL, AaBa, x, y, sw, sh, 2);
                Aa = AaBa[0];
                Ba = AaBa[1];
                image_rle_buffer(tmp, src, NULL, AaBa, x, add96, sw, sh, 2);
                Ca = AaBa[0];
                Da = AaBa[1];

                alpha1 = ((Aa * (16 - x_diff) + Ba * x_diff) * (16 - y_diff)
                          + (16 - x_diff) * y_diff * Ca
                          + Da * x_diff * y_diff + 128) >> 8;

                if (alpha1 > 0) {
                    int offset_plus_1;
                    uint8_t dst_lo, dst_hi, dst_r, dst_g, dst_b;
                    int dst_pixel;
                    uint16_t At, Bt, Ct, Dt;
                    int c;

                    if (alpha1 > 255) {
                        alpha1 = 255;
                    }

                    {
                        uint16_t AtBt[2];
                        image_rle_buffer(tmp, src, (u8 *)AtBt, NULL, x, y, sw, sh, 4);
                        At = AtBt[0];
                        Bt = AtBt[1];
                        image_rle_buffer(tmp, src, (u8 *)AtBt, NULL, x, add96, sw, sh, 4);
                        Ct = AtBt[0];
                        Dt = AtBt[1];
                    }

                    offset_plus_1 = offset + 1;
                    dst_lo = dst[offset_plus_1];
                    dst_hi = dst[offset];
                    dst_pixel = (dst_hi << 8) | dst_lo;

                    dst_r = dst_hi & 0xF8;
                    dst_g = (dst_pixel >> 3) & 0xFC;
                    dst_b = dst_lo << 3;

                    for (c = 0; c < 3; c++) {
                        if (c == 0) {
                            A = (int64_t)(((At >> 8) << 3) & 0xF8) * Aa;
                            B = (int64_t)(((Bt >> 8) << 3) & 0xF8) * Ba;
                            C = (int64_t)(((Ct >> 8) << 3) & 0xF8) * Ca;
                            D = (int64_t)(((Dt >> 8) << 3) & 0xF8) * Da;
                        } else {
                            switch (c) {
                            case 1:
                                A = (int64_t)((((At >> 8) | (At << 8)) >> 3) & 0xFC) * Aa;
                                B = (int64_t)((((Bt >> 8) | (Bt << 8)) >> 3) & 0xFC) * Ba;
                                C = (int64_t)((((Ct >> 8) | (Ct << 8)) >> 3) & 0xFC) * Ca;
                                D = (int64_t)((((Dt >> 8) | (Dt << 8)) >> 3) & 0xFC) * Da;
                                break;
                            case 2:
                                A = (int64_t)(At & 0xF8) * Aa;
                                B = (int64_t)(Bt & 0xF8) * Ba;
                                C = (int64_t)(Ct & 0xF8) * Ca;
                                D = (int64_t)(Dt & 0xF8) * Da;
                                break;
                            default:
                                break;
                            }
                        }

                        gray = ((A * (16 - x_diff) + B * x_diff) * (16 - y_diff)
                                + (int64_t)(16 - x_diff) * y_diff * C
                                + D * x_diff * y_diff + 128) >> 8;
                        gray = gray / alpha1;
                        if (gray > 255) {
                            gray = 255;
                        }

                        if (c == 0) {
                            dst_b = (uint8_t)(((int64_t)dst_b * 255 + 128 + (gray - dst_b) * alpha1) >> 8);
                        } else {
                            switch (c) {
                            case 1:
                                dst_g = (uint8_t)(((int64_t)dst_g * 255 + 128 + (gray - dst_g) * alpha1) >> 8);
                                break;
                            case 2:
                                dst_r = (uint8_t)(((int64_t)dst_r * 255 + 128 + (gray - dst_r) * alpha1) >> 8);
                                break;
                            default:
                                break;
                            }
                        }
                    }

                    {
                        uint16_t dst_rgb565;
                        dst_rgb565 = ((uint16_t)(dst_r << 8) & 0xF800) | ((uint16_t)dst_g << 3);
                        dst[offset] = dst_rgb565 >> 8;
                        dst[offset + 1] = ((dst_g << 3) & 0xE0) | (dst_b >> 3);
                    }
                }
            }

            offset += 2;
        }
    }
}
