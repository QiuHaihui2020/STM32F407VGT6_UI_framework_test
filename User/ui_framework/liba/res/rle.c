/*
 * rle.c —— RLE(行程长度编码)位图解压
 *
 * 【谁在用】UI 侧用得最广的资源接口之一:
 *     驱动层 ui_synthesis_oled.c;
 *     框架侧 liba/ui_dot/ui_rotate.c 与 liba/ui_draw/image_process.c。
 *
 * 【行号约定】两处 ASSERT(0) 打印时会带上 __LINE__。文件头这段说明以后还会
 *   增删, 不想让它一改、断言行号就跟着漂, 所以下面用 #line 12 把行号拨回
 *   函数自身的布局 —— 那两处 switch default 固定落在 65 / 94。
 *   改函数体时留意: 增删空行会挪动这两个行号。
 *
 * 【段属性】Rle_Decode 在 .ui_ram(要在 RAM 里执行), 用 rect.h 的 AT_UI_RAM 标注。
 *
 * 【编码格式】输入是一串 (sign, data) 块:
 *     sign 的低 7 位 = 像素个数 count, 最高位 = 是否为"重复块";
 *     重复块  : 后跟 1 个像素(pixel_size 字节), 重复 count 次;
 *     字面块  : 后跟 count 个像素, 原样拷贝。
 *   offset / len 用来只解出中间某一段(按解码后的字节偏移裁剪)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".ui_ram")
#endif

#include "jl_os_api.h"
#include "jl_rect.h"
#include "res/rle.h"
#include "jl_debug.h"    /* ASSERT / log_*: 显式包含, 保证本文件自包含 */

AT_UI_RAM
#line 12
int Rle_Decode(u8 *inbuf, int inSize, u8 *outbuf, int onuBufSize, int offset, int len, int pixel_size)
{
    /*
     * 入参在入口一次挡住: inbuf / outbuf 为 NULL 会直接崩; pixel_size 为 0 时
     * count * pixel_size 恒为 0, while 会在不搬任何数据的情况下把整个输入当
     * sign 逐字节吃掉。本函数只支持 1 和 2 两种像素宽度(下面 switch 的
     * default 就是 ASSERT(0))。
     */
    if (inbuf == NULL || outbuf == NULL) {
        return -1;
    }
    if (pixel_size != 1 && pixel_size != 2) {
        return -1;
    }

    u8 *src = inbuf;
    int i = 0;
    int decSize = 0;
    int count = 0;
    int pos = 0;
    int copylen = 0;
    int done = 0;        /* 标记"解满 len 提前 break 出去", 见循环后 */

    while (src < inbuf + inSize) {
        u8 sign = *src++;
        /* 这里复用函数作用域的 count, 【不要】用 int 再声明一个同名局部 ——
         * 那样会把外层的 count 遮蔽掉, 而 break 出循环之后还要拿当前块的
         * count 去补算 decSize(见循环末尾)。 */
        count = sign & 0x7F;
        if ((decSize + count * pixel_size) > onuBufSize) {
            return -1;
        }

        if ((sign & 0x80) == 0x80) {
            if (decSize < offset) {
                if ((decSize + count * pixel_size) >= offset) {
                    copylen = decSize + count * pixel_size - offset > (len - pos) ? (len - pos) : decSize + count * pixel_size - offset;
                    switch (pixel_size) {
                    case 1: memset(&outbuf[pos], src[0], copylen);
                        break;
                    case 2: if (src[0] == src[1]) {
                            memset(&outbuf[pos], src[0], copylen);
                        } else {
                            int t;
                            for (t = 0; t < copylen / 2; t++) {
                                memcpy(&outbuf[pos + t * pixel_size], src, pixel_size);
                            }
                        }
                        break;
                    default:
                        /* ASSERT 在 config_asser 为假时【不停机】, 所以这里还要
                         * return —— 否则会跑完整个循环, 静默产生一帧错图。
                         * 入口已挡过 pixel_size, 这里是双保险。 */
                        ASSERT(0);
                        return -1;
                    }
                    pos += copylen;
                    if (pos == len) {
                        done = 1;
                        break;
                    }
                }
            } else {

                copylen = count * pixel_size > (len - pos) ? (len - pos) : count * pixel_size;
                if (copylen) {
                    switch (pixel_size) {
                    case 1: memset(&outbuf[pos], src[0], copylen);
                        break;
                    case 2: if (src[0] == src[1]) {
                            memset(&outbuf[pos], src[0], copylen);
                        } else {
                            int t;
                            for (t = 0; t < copylen / 2; t++) {
                                memcpy(&outbuf[pos + t * pixel_size], src, pixel_size);
                            }
                        }
                        break;
                    default:
                        /* 同上一处: ASSERT 在 config_asser 为假时【不停机】,
                         * 不跟一句 return 就会跑完整个循环, 静默产生一帧错图。
                         * 入口已挡住非法 pixel_size, 这里是双保险。 */
                        ASSERT(0);
                        return -1;
                    }
                    pos += copylen;
                    if (pos == len) {
                        done = 1;
                        break;
                    }
                }
            }
            src += pixel_size;
        } else {

            if (decSize < offset) {
                if ((decSize + count * pixel_size) >= offset) {
                    copylen = decSize + count * pixel_size - offset > (len - pos) ? (len - pos) : decSize + count * pixel_size - offset;
                    memcpy(&outbuf[pos], src + (offset - decSize), copylen);
                    pos += copylen;
                    if (pos == len) {
                        done = 1;
                        break;
                    }
                }
            } else {


                copylen = count * pixel_size > (len - pos) ? (len - pos) : count * pixel_size;
                if (copylen) {
                    memcpy(&outbuf[pos], src, copylen);
                    pos += copylen;
                    if (pos == len) {
                        done = 1;
                        break;
                    }
                }
            }
            src += count * pixel_size;
        }

        decSize += count * pixel_size;
    }

    /*
     * decSize 在循环体【末尾】才累加, 而解满 len 时是从循环体中间 break 出去
     * 的 —— 跳过了那次累加, 所以这里把当前这一块补上, 返回值才是真正已解码
     * 的字节数。正常走完 while(源数据耗尽)不会置 done, 不会重复加。
     */
    if (done) {
        decSize += count * pixel_size;
    }

    if (pos == len) {
        return decSize;
    }

    return -1;
}

/*
 * 实现注意事项
 *
 *  1) 【返回值含义】成功时返回已解码的字节数。decSize 在循环体末尾累加, 而
 *     解满 len 时是从循环体中间 break 出去的, 所以循环后用 done 标志把当前
 *     这一块补算进去 —— 少了这一步, "成功"返回的长度会偏小一个块, 调用方
 *     若拿它当"已消费的源数据长度"就会算错。
 *
 *  2) 【入参检查都在入口】inbuf / outbuf 判空; pixel_size 只支持 1 和 2 ——
 *     为 0 时 count * pixel_size 恒为 0, 循环会在不搬任何数据的情况下把整个
 *     输入当 sign 逐字节吃掉。
 *
 *  3) 【ASSERT 不能当拦截用】ASSERT 在 config_asser 为假时只记录、不停机,
 *     所以两处 switch default 在 ASSERT 之后还要 return -1, 否则会继续跑完
 *     循环、静默产生一帧错图。
 *
 *  4) 【i 是未使用的局部】保留它不影响生成代码, 也便于与解码流程的描述对齐;
 *     真要清理时注意别连带改到 count 的作用域。
 */
