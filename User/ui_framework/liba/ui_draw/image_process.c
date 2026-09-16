/*
 * image_process.c —— 位图解码与合成(RGB565 / ARGB8565 / AL88 / AL44 / L1)
 *
 * 图像解码路径上的任何偏差都会直接反映成整屏错位,
 * 所以这里不做"顺手清理"; 需要注意的地方都在函数内就地注明。
 * 各控件模块都依赖它的解码结果, 改动前先看文末的注意事项。
 * 单色屏走 MONO 分支, 彩屏走 OSD16 分支。
 *
 * 【本工程当前无调用者】这条解码路径还没在真机上跑过, 启用前先自己走一遍。
 * 【行号约定】image_decode_process 的 5 处 ASSERT(0) 落在行号
 *   595 / 603 / 638 / 667 / 737(ASSERT 宏内嵌 __LINE__)。本文件没有用
 *   #line 拨号, 所以增删行会让断言打印的行号偏掉, 改之前先想清楚。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".image_process.data")
#pragma data_seg(".image_process.data")
#pragma code_seg(".image_process.text")
#endif

#include "ui/ui.h"
#include "ui/ui_core.h"
#include "ui/ui_image.h"
#include "res/resfile.h"
#include "res/rle.h"
#include "jl_debug.h"    /* ASSERT: 显式包含, 保证本文件自包含 */

/* 这两个函数的声明没有出现在任何公共头里, 就地声明。
 * 签名要与 resfile.c 里的定义完全一致(含参数宽度), 否则 ABI 不符。 */
void select_resfile(u8 index);
int read_palette(int prj_id, RESFILE *specfile, struct image_file *f, u8 *data, int page);

/*
 * @brief 按 alpha 把前景色混进背景色(RGB565)
 * @param backcolor 背景色, 【字节交换过】的 RGB565
 * @param forecolor 前景色, 【字节交换过】的 RGB565
 * @param alpha     0 = 全背景, 255 = 全前景
 * @return 混合结果, 同样是【字节交换过】的 RGB565
 *
 * 【本文件自带一份 static 实现】彩屏通路里另有一份同名的非 static 函数,
 * 但它整个包在 #if (TCFG_SPI_LCD_ENABLE) 里; 本工程该宏为 0, 那份会被整个
 * 编译掉, 所以这里必须自带一份 —— 否则这条解码路径一旦启用就是链接失败。
 *
 * 约定(与彩屏那份一致, 不要改):
 *   · alpha==255 / alpha==0 两个早退分支都返回【字节交换后】的颜色;
 *   · 混合结果同样交换后返回。
 * 本文件第 833 行传进来的实参就是按这个约定预先交换过的, 两边必须配对,
 * 只改一侧就会整屏偏色。
 *
 * 【不能复用点阵屏那份】lcd_drive/middle/ui_synthesis_oled.c 里也有个同名
 * static 函数, 但它只处理 alpha==0、且返回【未交换】的 backcolor ——
 * 把它的 static 去掉来顶替会静默产生错误像素。
 *
 * 写成 static 而非全局, 是为了不跟彩屏配置下那份非 static 的定义打架:
 * 两者语义相同、各自文件内可见, 改回彩屏也不会重复定义。
 */
static u16 get_mixed_pixel(u16 backcolor, u16 forecolor, u8 alpha)
{
    u16 mixed_color;
    u8 r0, g0, b0;
    u8 r1, g1, b1;
    u8 r2, g2, b2;

    if (alpha == 255) {
        return (forecolor >> 8) | (forecolor & 0xff) << 8;
    } else if (alpha == 0) {
        return (backcolor >> 8) | (backcolor & 0xff) << 8;
    }

    r0 = ((backcolor >> 11) & 0x1f) << 3;
    g0 = ((backcolor >> 5) & 0x3f) << 2;
    b0 = ((backcolor >> 0) & 0x1f) << 3;

    r1 = ((forecolor >> 11) & 0x1f) << 3;
    g1 = ((forecolor >> 5) & 0x3f) << 2;
    b1 = ((forecolor >> 0) & 0x1f) << 3;

    r2 = (alpha * r1 + (255 - alpha) * r0) / 255;
    g2 = (alpha * g1 + (255 - alpha) * g0) / 255;
    b2 = (alpha * b1 + (255 - alpha) * b0) / 255;

    mixed_color = ((r2 >> 3) << 11) | ((g2 >> 2) << 5) | (b2 >> 3);

    return (mixed_color >> 8) | (mixed_color & 0xff) << 8;
}

struct image_priv priv = {0};

static int image_data_read(int prj_id, RESFILE *specfile, struct image_file *f,
                           u8 *data, int len, int offset)
{
    select_resfile(prj_id);
    return br23_read_image_data(specfile, f, data, len, offset);
}
/*
 * ⚠ 下面几处写法看着别扭, 但都是刻意的, 【不要"顺手改干净"】:
 *   1. 偏移运算一律重新读【源字段】(dc->fbuf / var->temp_pixelbuf), 不读刚
 *      赋过值的目标字段 —— 这样每条赋值的来源都只有一处, 读起来和生成的
 *      代码都更简单。
 *   2. rle_line 头清零用 memset(..., 4) 而不是 *(u32 *)p = 0 —— 后者带类型,
 *      编译器会当它与随后那次 var->ptemp 的读不相干, 把那次读提到清零之前。
 *   3. 循环里先判断再累加(break 之前不更新 total_len), 免得多记一轮。
 *   4. image_decode_read 的 L1 分支里 remain_bytes 写成减 (offset -
 *      rle_offset), 是为了复用 offset 里那个乘法, 少算一次。
 *   5. 透明色分支里像素直接取自 color >> 8 / color, 不重读 lut —— 中间隔着
 *      一次 alphabuf 写, 重读多一次访存, 也容易读串。
 */
static int line_update(u8 *mask, u16 mask_len, u16 y, u16 width)
{
    int i;
    if (!mask) {
        return 1;
    }
    for (i = 0; i < (width + 7) / 8; i++) {
        if (mask[y * ((width + 7) / 8) + i]) {
            return 1;
        }
    }
    return 0;
}

void image_l1_transparent_color_set(u8 mode, u32 val)
{
    int i;
    priv.transparent_mode = mode;
    if (mode == L1_TRANS_COLOR) {
        u32 *p = (u32 *)val;
        priv.transparent_color_num = p[0];
        for (i = 0; i < priv.transparent_color_num; i++) {
            priv.transparent_color[i] = p[i + 1];
        }
    } else if (mode == L1_TRANS_COLOR_INDEX) {
        priv.transparent_color_index = val;
    }
}

void image_l1_set_lut_color_callback(void (*l1_lut_cb)(u16, u16, u16, u16 *))
{
    priv.l1_lut_cb = l1_lut_cb;
}













void image_decode_init(struct image_file *file, struct image_decode_var *var)
{
    u8 version[3];
    version[0] = file->version >> 8;
    version[1] = file->version;
    version[2] = 0;
    if (!strcmp((char *)version, "21")) {
        u32 alpha_addr = 0;
        image_data_read(var->dc->prj, var->fp, file, (u8 *)&alpha_addr, 4, 0);
        if (alpha_addr) {
            file->format = PIXEL_FMT_ARGB8565;
        } else {
            file->format = PIXEL_FMT_RGB565;
        }
    }

    switch (file->format) {
    case PIXEL_FMT_RGB565: { int buf_offset;
        struct draw_context *dc = var->dc;

        var->alpha_addr = 0;
        var->rle_offset = 4;

        var->temp_pixelbuf_len = dc->fbuf_len - dc->width * 2;

        var->alphabuf = NULL;
        var->temp_alphabuf = NULL;
        var->temp_alphabuf_len = 0;

        /* pixelbuf 直接指向帧缓冲首地址 */

        var->pixelbuf = dc->fbuf;

        /* temp_pixelbuf 放在一整行 RGB565 像素之后, 4 字节对齐 */

        buf_offset = (dc->width * 2 + 3) / 4 * 4;
        var->temp_pixelbuf = &dc->fbuf[buf_offset];
    } break;
    case PIXEL_FMT_ARGB8565: { int buf_offset;
        struct draw_context *dc = var->dc;

        image_data_read(dc->prj, var->fp, file, (u8 *)&var->alpha_addr, 4, 0);
        var->rle_offset = 4;

        var->temp_pixelbuf_len = dc->width * 2 * dc->lines;
        var->temp_alphabuf_len = dc->width * dc->lines;

        /* 帧缓冲布局: pixelbuf | alphabuf | temp_pixelbuf | temp_alphabuf */

        var->pixelbuf = dc->fbuf;

        /* alphabuf 紧跟一行 RGB565 像素 */

        buf_offset = (dc->width * 2 + 3) / 4 * 4;
        var->alphabuf = &dc->fbuf[buf_offset];

        buf_offset += dc->width;
        buf_offset = (buf_offset + 3) / 4 * 4;
        var->temp_pixelbuf = &dc->fbuf[buf_offset];

        buf_offset += var->temp_pixelbuf_len;
        buf_offset = (buf_offset + 3) / 4 * 4;
        var->temp_alphabuf = &dc->fbuf[buf_offset];
    } break;
    case PIXEL_FMT_AL44: { int buf_offset;
        struct draw_context *dc = var->dc;

        var->alpha_addr = 0;
        var->rle_offset = 32;

        var->temp_pixelbuf_len = dc->fbuf_len - dc->width * 3 - dc->width - 32;

        var->alphabuf = NULL;
        var->temp_alphabuf = NULL;
        var->temp_alphabuf_len = 0;

        /* 帧缓冲布局: pixelbuf | alphabuf | unzip | lut | temp_pixelbuf */

        var->pixelbuf = dc->fbuf;
        buf_offset = dc->width * 2;

        /* alphabuf */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->alphabuf = &dc->fbuf[buf_offset];
        buf_offset += dc->width;

        /* unzip: AL44 一字节一像素 */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->unzip = &dc->fbuf[buf_offset];
        buf_offset += dc->width;

        /* lut: 16 项 x 2 字节 */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->lut = &dc->fbuf[buf_offset];
        buf_offset += 32;

        /* temp_pixelbuf */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->temp_pixelbuf = &dc->fbuf[buf_offset];

        /* 调色板一次性读进 lut */

        image_data_read(dc->prj, var->fp, file, var->lut, 32, 0);
    } break;
    case PIXEL_FMT_AL88: { int buf_offset;
        struct draw_context *dc = var->dc;

        var->alpha_addr = 0;
        var->rle_offset = 0;

        var->temp_pixelbuf_len = dc->fbuf_len - dc->width * 3 - dc->width * 2 - 512;

        var->alphabuf = NULL;
        var->temp_alphabuf = NULL;
        var->temp_alphabuf_len = 0;

        /* 帧缓冲布局: pixelbuf | alphabuf | unzip | lut | temp_pixelbuf */

        var->pixelbuf = dc->fbuf;

        /* alphabuf */

        buf_offset = (dc->width * 2 + 3) / 4 * 4;
        var->alphabuf = &dc->fbuf[buf_offset];
        buf_offset += dc->width;

        /* unzip: AL88 两字节一像素 */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->unzip = &dc->fbuf[buf_offset];
        buf_offset += dc->width * 2;

        /* lut: 256 项 x 2 字节 */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->lut = &dc->fbuf[buf_offset];
        buf_offset += 512;

        /* temp_pixelbuf */

        buf_offset = (buf_offset + 3) / 4 * 4;
        var->temp_pixelbuf = &dc->fbuf[buf_offset];

        /* 调色板由 read_palette 按页读取 */

        read_palette(dc->prj, var->fp, file, var->lut, var->page);
    } break;
    case PIXEL_FMT_L1:
        if (file->compress == 0) { int buf_offset;
            struct draw_context *dc = var->dc;
            var->alpha_addr = 0;
            var->rle_offset = 8;

            var->temp_pixelbuf_len = dc->fbuf_len - dc->width * 3 - 8;

            var->alphabuf = NULL;
            var->temp_alphabuf = NULL;
            var->temp_alphabuf_len = 0;

            /* 帧缓冲布局: pixelbuf | alphabuf | lut | temp_pixelbuf */

            var->pixelbuf = dc->fbuf;
            buf_offset = dc->width * 2;

            /* alphabuf */

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->alphabuf = &dc->fbuf[buf_offset];
            buf_offset += dc->width;

            /* lut: 2 项 x 2 字节; 未压缩时不需要 unzip 缓冲 */

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->lut = &dc->fbuf[buf_offset];
            buf_offset += 8;

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->temp_pixelbuf = &dc->fbuf[buf_offset];
        } else { int buf_offset;
            struct draw_context *dc = var->dc;

            var->alpha_addr = 0;
            var->rle_offset = 8;

            var->temp_pixelbuf_len = dc->fbuf_len - dc->width * 3 - (dc->width + 7) / 8 - 8;

            var->alphabuf = NULL;
            var->temp_alphabuf = NULL;
            var->temp_alphabuf_len = 0;

            /* 帧缓冲布局: pixelbuf | alphabuf | unzip | lut | temp_pixelbuf */

            var->pixelbuf = dc->fbuf;
            buf_offset = dc->width * 2;

            /* alphabuf */

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->alphabuf = &dc->fbuf[buf_offset];
            buf_offset += dc->width;

            /* unzip: L1 一行 (width + 7) / 8 字节 */

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->unzip = &dc->fbuf[buf_offset];
            buf_offset += (dc->width + 7) / 8;

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->lut = &dc->fbuf[buf_offset];
            buf_offset += 8;

            buf_offset = (buf_offset + 3) / 4 * 4;
            var->temp_pixelbuf = &dc->fbuf[buf_offset];
        }
        {
            u16 *rgb565 = (u16 *)var->lut;
            if (priv.prj != var->dc->prj || priv.page != var->dc->page || priv.id != file->id) {
                u8 *argb = var->lut;
                int i;
                image_data_read(var->dc->prj, var->fp, file, var->lut, 8, 0);
                for (i = 0; i < 2; i++) {
                    rgb565[i] = RGB565(argb[i * 4 + 1], argb[i * 4 + 2], argb[i * 4 + 3]);
                }
                if (priv.l1_lut_cb) {
                    priv.l1_lut_cb(var->dc->prj, var->dc->page, file->id, rgb565);
                }
                priv.prj = var->dc->prj;
                priv.page = var->dc->page;
                priv.id = file->id;
                memcpy(priv.lut, rgb565, 4);
            } else {
                memcpy(rgb565, priv.lut, 4);
            }
        }
        break;
    }
}






int image_decode_read(struct image_file *file, struct image_decode_var *var)
{

    switch (file->format) {
    case PIXEL_FMT_RGB565: case PIXEL_FMT_ARGB8565: {
        int headlen = sizeof(struct rle_header) + (var->remain * 2 + 3) / 4 * 4;
        var->line = (struct rle_line *)var->temp_pixelbuf;
        var->ptemp = &var->temp_pixelbuf[headlen];
        memset(var->line, 0, 4);

        image_data_read(var->dc->prj, var->fp, file, var->ptemp, var->remain << 2, var->rle_offset + var->vh * 4);
        {
        int i;
        struct rle_line *line = var->line;
        struct rle_header *rle = (struct rle_header *)var->ptemp;
        int total_len = 0;
        for (i = 0; i < var->remain; i++) {
            if (i == 0) {
                line->addr = rle[i].addr;
                line->len[i] = rle[i].len;
            } else {
                line->len[i] = rle[i].len;
            }
            if (total_len + rle[i].len > var->temp_pixelbuf_len - headlen) {
                break;
            }
            total_len += rle[i].len;
                line->num++;
        }
        image_data_read(var->dc->prj, var->fp, file, var->ptemp, total_len, var->rle_offset + line->addr);

        if (var->alpha_addr) {
            headlen = sizeof(struct rle_header) + (line->num * 2 + 3) / 4 * 4;

            var->alpha_line = (struct rle_line *)var->temp_alphabuf;
            var->alpha_ptemp = &var->temp_alphabuf[headlen];
            memset(var->alpha_line, 0, 4);

            image_data_read(var->dc->prj, var->fp, file, var->alpha_ptemp, line->num * 4, var->alpha_addr + var->vh * 4);
            {
            struct rle_line *alpha_line = var->alpha_line;
            struct rle_header *rle = (struct rle_header *)var->alpha_ptemp;
            int total_len = 0;
            for (i = 0; i < line->num; i++) {
                if (i == 0) {
                    alpha_line->addr = rle[i].addr;
                    alpha_line->len[i] = rle[i].len;
                } else {
                    alpha_line->len[i] = rle[i].len;
                }
                if (total_len + rle[i].len > var->temp_alphabuf_len - headlen) {
                    break;
                }
                total_len += rle[i].len;
                    alpha_line->num++;
            }
            image_data_read(var->dc->prj, var->fp, file, var->alpha_ptemp, total_len, var->alpha_addr + alpha_line->addr);
            }
        }
        }
    } break;
    case PIXEL_FMT_AL88: {
        int headlen = sizeof(struct rle_header) + (var->remain * 2 + 3) / 4 * 4;
        var->line = (struct rle_line *)var->temp_pixelbuf;
        var->ptemp = &var->temp_pixelbuf[headlen];
        memset(var->line, 0, 4);

        image_data_read(var->dc->prj, var->fp, file, var->ptemp, var->remain << 2, var->rle_offset + var->vh * 4);

        /* 先把一行 RLE 头表读进 line, 再逐行累加长度直到缓冲放不下 */




        {
        int i;
        struct rle_line *line = var->line;
        struct rle_header *rle = (struct rle_header *)var->ptemp;
        int total_len = 0;
        for (i = 0; i < var->remain; i++) {

            if (i == 0) {
                line->addr = rle[i].addr;
                line->len[i] = rle[i].len;
            } else {
                line->len[i] = rle[i].len;
            }
            if (total_len + rle[i].len > var->temp_pixelbuf_len - headlen) {
                break;
            }
            total_len += rle[i].len;
                line->num++;
        }



        image_data_read(var->dc->prj, var->fp, file, var->ptemp, total_len, var->rle_offset + line->addr);
        }
    } break;
    case PIXEL_FMT_AL44: {
        int i;
        int headlen = sizeof(struct rle_header) + (var->remain * 2 + 3) / 4 * 4;
        var->line = (struct rle_line *)var->temp_pixelbuf;
        var->ptemp = &var->temp_pixelbuf[headlen];
        memset(var->line, 0, 4);

        image_data_read(var->dc->prj, var->fp, file, var->ptemp, var->remain << 2, var->rle_offset + var->vh * 4);
        {
        struct rle_line *line = var->line;
        struct rle_header *rle = (struct rle_header *)var->ptemp;
        int total_len = 0;
        for (i = 0; i < var->remain; i++) {
            if (i == 0) {
                line->addr = rle[i].addr;
                line->len[i] = rle[i].len;
            } else {
                line->len[i] = rle[i].len;
            }
            if (total_len + rle[i].len > var->temp_pixelbuf_len - headlen) {
                break;
            }
            total_len += rle[i].len;
                line->num++;
        }

        image_data_read(var->dc->prj, var->fp, file, var->ptemp, total_len, var->rle_offset + line->addr);
        }
    } break;
    case PIXEL_FMT_L1:
        if (file->compress == 0) {
            int bytes_per_line = (file->width + 7) / 8;
            int offset = var->rle_offset + var->vh * bytes_per_line;

            var->lines = var->temp_pixelbuf_len / bytes_per_line;
            var->lines = var->lines > var->remain ? var->remain : var->lines;
            {
            int remain_bytes = file->height * bytes_per_line - (offset - var->rle_offset);
            int read_bytes = remain_bytes > var->lines * bytes_per_line ? var->lines * bytes_per_line : remain_bytes;
            if (image_data_read(var->dc->prj, var->fp, file, var->temp_pixelbuf, read_bytes, offset) != read_bytes) {
                return -EFAULT;
            }
            }
        } else {
            int i;
            int headlen = sizeof(struct rle_header) + (var->remain * 2 + 3) / 4 * 4;
            var->line = (struct rle_line *)var->temp_pixelbuf;
            var->ptemp = &var->temp_pixelbuf[headlen];
            memset(var->line, 0, 4);

            image_data_read(var->dc->prj, var->fp, file, var->ptemp, var->remain << 2, var->rle_offset + var->vh * 4);
            {
            struct rle_line *line = var->line;
            struct rle_header *rle = (struct rle_header *)var->ptemp;
            int total_len = 0;
            for (i = 0; i < var->remain; i++) {
                if (i == 0) {
                    line->addr = rle[i].addr;
                    line->len[i] = rle[i].len;
                } else {
                    line->len[i] = rle[i].len;
                }
                if (total_len + rle[i].len > var->temp_pixelbuf_len - headlen) {
                    break;
                }
                total_len += rle[i].len;
                    line->num++;
            }

            image_data_read(var->dc->prj, var->fp, file, var->ptemp, total_len, var->rle_offset + line->addr);
            }
        }
        break;
    }
    return 0;
}


int image_decode_process(struct image_file *file, struct image_decode_var *var)
{
    struct draw_context *dc = var->dc;
    struct rect *r = &var->r;
    struct rle_line *line = var->line;
    struct rle_line *alpha_line = var->alpha_line;
    int hh = var->hh;
    int vw = var->vw;


    switch (file->format) {
    case PIXEL_FMT_RGB565: case PIXEL_FMT_ARGB8565: if (file->compress == 1) {
            if (line_update(dc->mask, dc->mask_len, r->top + var->h - dc->disp.top, dc->disp.width)) {
                int ret = Rle_Decode(var->p0, line->len[hh], var->pixelbuf, file->width * 2, vw * 2, r->width * 2, 2);
                if (ret == -1) {
                    int addr = 0;
                    /* addr 是排查时给打印用的, 平时不输出: 每一行都会走到 */

                    ASSERT(0);
                }
                if (var->alpha_addr) {
                    int ret = Rle_Decode(var->p1, alpha_line->len[hh], var->alphabuf, file->width, vw, r->width, 1);
                    if (ret == -1) {
                        int addr = 0;
                        /* 同上 */

                        ASSERT(0);
                    }
                }
                var->p0 += line->len[hh];
                if (var->alpha_addr) {
                    var->p1 += alpha_line->len[hh];
                }
                return 0;
            } else {
                var->p0 += line->len[hh];
                if (var->alpha_addr) {
                    var->p1 += alpha_line->len[hh];
                }
                return -1;
            }
        } else {
            var->pixelbuf += file->width * 2;
            if (var->alpha_addr) {
                var->alphabuf += file->width;
            }
        }
        break;

    case PIXEL_FMT_AL88: if (file->compress == 1) {
            if (line_update(dc->mask, dc->mask_len, r->top + var->h - dc->disp.top, dc->disp.width)) {
                /* AL88: 一字节 alpha + 一字节调色板下标, 解压后拆成两路 */


                u8 *al88buf = var->unzip;

                int ret = Rle_Decode(var->p0, line->len[hh], al88buf, file->width * 2, vw * 2, r->width * 2, 2);
                if (ret == -1) {
                    int addr = 0;
                    /* 同上 */

                    ASSERT(0);
                }

                int i;
                for (i = 0; i < r->width; i++) {
                    u8 index = al88buf[i * 2 + 1];
                    var->alphabuf[i] = al88buf[i * 2];
                    var->pixelbuf[i * 2] = var->lut[index * 2 + 1];
                    var->pixelbuf[i * 2 + 1] = var->lut[index * 2];
                }
                var->p0 += line->len[hh];
                return 0;
            } else {
                var->p0 += line->len[hh];
                return -1;
            }
        }
        break;
    case PIXEL_FMT_AL44: if (file->compress == 1) {
            if (line_update(dc->mask, dc->mask_len, r->top + var->h - dc->disp.top, dc->disp.width)) {
                /* AL44: 高 4 位 alpha, 低 4 位调色板下标 */

                u8 *al44buf = var->unzip;

                int ret = Rle_Decode(var->p0, line->len[hh], al44buf, file->width, vw, r->width, 1);
                if (ret == -1) {
                    int addr = 0;
                    /* 同上 */

                    ASSERT(0);
                }

                int i;
                for (i = 0; i < r->width; i++) {
                    u8 index = al44buf[i];
                    var->alphabuf[i] = (index >> 4) << 4;
                    var->pixelbuf[i * 2] = var->lut[(index & 0x0f) * 2 + 1];
                    var->pixelbuf[i * 2 + 1] = var->lut[(index & 0x0f) * 2];
                }
                var->p0 += line->len[hh];
                return 0;
            } else {
                var->p0 += line->len[hh];
                return -1;
            }
        }
        break;
    case PIXEL_FMT_L1: if (file->compress == 0) {
            int bytes_per_line = (file->width + 7) / 8;
            if (line_update(dc->mask, dc->mask_len, r->top + var->h - dc->disp.top, dc->disp.width)) {
                int i;
                u8 *l1buf = var->p0;
                memset(var->alphabuf, 0, r->width);
                for (i = 0; i < r->width; i++) {
                    u8 index = (l1buf[(i + vw) / 8] & (1 << (7 - (i + vw) % 8))) ? 1 : 0;
                    if (priv.transparent_mode == L1_TRANS_COLOR) {
                        u16 color = var->lut[index * 2 + 1] << 8 | var->lut[index * 2];
                        int c;
                        for (c = 0; c < priv.transparent_color_num; c++) {
                            if (priv.transparent_color[c] == color) {
                                break;
                            }
                        }
                        if (c == priv.transparent_color_num) {
                            var->alphabuf[i] = 0xff;
                            var->pixelbuf[i * 2] = color >> 8;
                            var->pixelbuf[i * 2 + 1] = color;
                        }
                    } else if (priv.transparent_mode == L1_TRANS_COLOR_INDEX) {
                        if (priv.transparent_color_index != index) {
                            var->alphabuf[i] = 0xff;
                            var->pixelbuf[i * 2] = var->lut[index * 2 + 1];
                            var->pixelbuf[i * 2 + 1] = var->lut[index * 2];
                        }
                    } else {
                        var->alphabuf[i] = 0xff;
                        var->pixelbuf[i * 2] = var->lut[index * 2 + 1];
                        var->pixelbuf[i * 2 + 1] = var->lut[index * 2];
                    }
                }
                var->p0 += bytes_per_line;
                return 0;
            } else {
                var->p0 += bytes_per_line;
                return -1;
            }
        } else {
            int bytes_per_line = (file->width + 7) / 8;

            if (line_update(dc->mask, dc->mask_len, r->top + var->h - dc->disp.top, dc->disp.width)) {
                /* L1 压缩: 一位一像素, 先整行解压到 unzip */

                u8 *l1buf = var->unzip;

                int ret = Rle_Decode(var->p0, line->len[hh], l1buf, bytes_per_line, vw / 8, (r->width + 7) / 8, 1);
                if (ret == -1) {
                    int addr = 0;
                    /* 同上 */

                    ASSERT(0);
                }

                int i;
                memset(var->alphabuf, 0, r->width);
                for (i = 0; i < r->width; i++) {
                    u8 index = (l1buf[i / 8] & (1 << (7 - i % 8))) ? 1 : 0;
                    if (priv.transparent_mode == L1_TRANS_COLOR) {
                        u16 color = var->lut[index * 2 + 1] << 8 | var->lut[index * 2];
                        int c;
                        for (c = 0; c < priv.transparent_color_num; c++) {
                            if (priv.transparent_color[c] == color) {
                                break;
                            }
                        }
                        if (c == priv.transparent_color_num) {
                            var->alphabuf[i] = 0xff;
                            var->pixelbuf[i * 2] = color >> 8;
                            var->pixelbuf[i * 2 + 1] = color;
                        }
                    } else if (priv.transparent_mode == L1_TRANS_COLOR_INDEX) {
                        if (priv.transparent_color_index != index) {
                            var->alphabuf[i] = 0xff;
                            var->pixelbuf[i * 2] = var->lut[index * 2 + 1];
                            var->pixelbuf[i * 2 + 1] = var->lut[index * 2];
                        }
                    } else {
                        var->alphabuf[i] = 0xff;
                        var->pixelbuf[i * 2] = var->lut[index * 2 + 1];
                        var->pixelbuf[i * 2 + 1] = var->lut[index * 2];
                    }
                }

                var->p0 += line->len[hh];
                return 0;
            } else {
                var->p0 += line->len[hh];
                return -1;
            }
        }
    }
    return 0;
}

void draw_image(struct image_file *file, struct image_decode_var *var)
{

    struct draw_context *dc = var->dc;
    u8 quadrant = var->quadrant;
    RESFILE *fp = var->fp;
    int page = var->page;
    struct rect *r = &var->r;
    struct rect *disp = &var->disp;
    int w;

    image_decode_init(file, var);

    for (var->h = 0; var->h < r->height;) {
        int rh = r->top + var->h - disp->top;
        int rw = r->left - disp->left;
        var->vh = rh;
        var->vw = rw;

        switch (quadrant) {
        case 0: break;
        case 1:
            var->vh = file->height - r->top + disp->top - r->height + var->h;
            break;
        case 2: var->vh = file->height - r->top + disp->top - r->height + var->h;
            /* 上下镜像后继续做左右镜像 */
        default:
            var->vw = file->width - (r->left - disp->left) - r->width;
            break;
        }
        var->remain = (r->height - var->h) > (file->height - var->vh) ? (file->height - var->vh) : (r->height - var->h);

        image_decode_read(file, var);

        if (file->compress == 0) {
            var->p0 = var->temp_pixelbuf;
        } else {
            var->p0 = var->ptemp;
            var->p1 = var->alpha_ptemp;
        }
        {
        u8 *p0 = var->p0;
        u8 *p1 = var->p1;
        int line_num;
        if (file->compress == 0) {
            line_num = var->lines;
        } else {
            if (var->alpha_addr) {
                line_num = var->line->num > var->alpha_line->num ? var->alpha_line->num : var->line->num;
            } else {
                line_num = var->line->num;
            }
        }
        {
        int hs;
        int he;
        int hstep;

        he = var->h + line_num;
        if (quadrant == 1 || quadrant == 2) {
            hs = r->height - var->h - 1;
            hstep = -1;
        } else {
            hs = var->h;
            hstep = 1;
        }

        for (var->hh = 0, var->h = hs; var->hh < line_num; var->hh++, var->h += hstep) {
            if (image_decode_process(file, var) == -1) {
                continue;
            }
            {
            u16 *pdisp = (u16 *)dc->buf;
            u16 *pixelbuf16 = (u16 *)var->pixelbuf;

            if (var->alphabuf == NULL) {
                u16 x0 = r->left;
                u16 y0 = r->top + var->h;
                int offset = (y0 - dc->disp.top) * dc->disp.width + (x0 - dc->disp.left);
                if (quadrant == 2 || quadrant == 3) {
                    for (w = 0; w < r->width; w++) {
                        int vww = r->width - w - 1;
                        pdisp[offset + w] = pixelbuf16[vww];
                    }
                } else {
                    memcpy(&pdisp[offset], var->pixelbuf, r->width * 2);
                }
                if (file->compress == 0) {
                    var->pixelbuf += file->width * 2;
                }
            } else {
                for (w = 0; w < r->width; w++) {
                    u16 color, pixel;
                    u8 alpha = var->alphabuf ? var->alphabuf[w] : 0xff;

                    pixel = pixelbuf16[w];
                    if (alpha == 0) { continue; }
                    int vww;
                    vww = (quadrant == 2 || quadrant == 3) ?
                          (r->width - w - 1) : w;

                    u16 x0 = r->left + vww;
                    u16 y0 = r->top + var->h;

                    if (alpha != 0xff) {
                        u16 backcolor = platform_api->read_point(dc, x0, y0);
                        pixel = get_mixed_pixel((backcolor >> 8) | (backcolor << 8), (pixel >> 8) | (pixel << 8), alpha);
                    }

                    if (dc->mask) {
                        int yy = y0 - dc->disp.top;
                        int xx = x0 - dc->disp.left;
                        if (yy >= dc->disp.height) {
                            continue;
                        }
                        if (xx >= dc->disp.width) {
                            continue;
                        }
                        if (dc->mask[(dc->disp.width + 7) / 8 * yy + xx / 8] & (1 << (xx & 0x07))) {
                            int offset = dc->disp.width * yy + xx;
                            if (offset * 2 + 1 < dc->len) {
                                pdisp[offset] = pixel;
                            }
                        }
                    } else {
                        int offset = dc->disp.width * (y0 - dc->disp.top) + (x0 - dc->disp.left);
                        if (offset * 2 + 1 < dc->len) {
                            pdisp[offset] = pixel;
                        }
                    }
                }
            }
            }
        }

        var->h = he;
        }
        }
    }
}

/*
 * 实现注意事项
 *
 *  1) 【get_mixed_pixel 在本文件内自带一份 static 实现】彩屏通路那份非 static
 *     的同名函数包在 #if (TCFG_SPI_LCD_ENABLE) 里, 本工程该宏为 0, 所以这条
 *     解码路径一旦启用就需要本文件里这一份。
 *     它按【彩屏语义】写的: alpha==255 / alpha==0 两个早退分支返回字节交换后
 *     的颜色, 混合结果同样交换后返回 —— 调用处传进来的实参就是按这个约定
 *     预先交换过的。
 *     lcd_drive/middle/ui_synthesis_oled.c 里那个同名 static 函数【语义不同】
 *     (只处理 alpha==0, 且返回未交换的 backcolor), 不能拿来顶替。
 *
 *  2) 【另外两个就地声明的符号】select_resfile / read_palette 都在
 *     liba/res/resfile.c 里有定义(read_palette 在本配置下是死代码), 不是缺口。
 *
 *  3) 【行号约定】image_decode_process 的 5 处 ASSERT(0) 内嵌 __LINE__,
 *     预期落在 595 / 603 / 638 / 667 / 737。本文件按固定行号布局组织,
 *     空行不要随意增删, 否则断言打印的行号会偏。
 *
 *  4) 【当前无调用者】这条解码路径还没在真机上验证过, 启用前先自己走一遍。
 */
