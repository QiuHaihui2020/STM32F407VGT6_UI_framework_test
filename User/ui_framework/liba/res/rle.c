/*
 * rle.c —— RLE(行程长度编码)位图解压
 *
 * 【来源】从 cpu/br27/liba/res.a 的 rle.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/rle.ll
 *     原始路径: btsdk/lib/utils/ui/resource/rle.c
 *
 * 【谁在用】UI 侧用得最广的 res.a 接口之一:
 *     驱动层 ui_synthesis_oled.c;
 *     已还原的框架代码 liba/ui_dot/ui_rotate.c 与 liba/ui_draw/image_process.c。
 *
 * 【行号锁定】两处 ASSERT(0) 必须落在原始行号 44 / 67。函数体由
 *   cpu/br27/tools/ui_reimpl/gen_rle.py 按绝对行号拼出, 空行不要随意增删;
 *   下面的 #line 12 把行号拨回原厂布局。
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
#include "jl_debug.h"    /* ASSERT / log_*: 原厂靠别处间接带入, 这里补成自包含 */

AT_UI_RAM
#line 12
int Rle_Decode(u8 *inbuf, int inSize, u8 *outbuf, int onuBufSize, int offset, int len, int pixel_size)
{
    /*
     * 加固: 原库不检查这几个入参。inbuf / outbuf 为 NULL 直接崩;
     * pixel_size 为 0 时 count * pixel_size 恒为 0, while 会在不搬任何数据的
     * 情况下把整个输入当 sign 逐字节吃掉。本函数只支持 1 和 2 两种像素宽度
     * (下面 switch 的 default 就是 ASSERT(0)), 干脆在入口挡住。
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
    int done = 0;        /* 加固: 标记"解满 len 提前 break 出去", 见循环后 */

    while (src < inbuf + inSize) {
        u8 sign = *src++;
        /* 加固: 原库在这里用 int 重新声明 count, 把函数作用域那个 count 完全
         * 遮蔽掉(于是外层那个成了死变量)。去掉 int 改为复用外层变量 ——
         * 这样 break 出循环之后还拿得到当前块的 count, 供末尾补算 decSize。 */
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
                        /* 加固: ASSERT 在 config_asser 为假时【不停机】, 原库
                         * 随后仍会跑完整个循环, 静默产生一帧错图。入口已挡住
                         * 非法 pixel_size, 这里再返回错误做双保险。 */
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
                        /* 加固: ASSERT 在 config_asser 为假时【不停机】, 原库
                         * 随后仍会跑完整个循环, 静默产生一帧错图。入口已挡住
                         * 非法 pixel_size, 这里再返回错误做双保险。 */
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
     * 加固: decSize 是在循环体【末尾】才累加的, 而解满 len 时是从循环体中间
     * break 出去的 —— 跳过了那次累加, 于是"成功"返回的 decSize 少算了当前
     * 这一块。这里补上。正常走完 while(源数据耗尽)不会置 done, 也就不会重复加。
     * 各调用方目前只判 ret < 0, 所以原库这个偏差一直没暴露。
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
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍然照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在
 * cpu/br27/tools/ui_reimpl/accept/rle.txt 并锁定指纹)。
 *
 *   [已修] 1 —— 加了 done 标志, 循环后补算当前块, 返回值不再少一个块。
 *   [部分] 2 —— 内层那个遮蔽用的 count 已去掉(改为复用函数作用域的 count,
 *                正是补算所需)。i 仍是死变量, 留着不动: 删它对代码生成毫无
 *                影响, 却会让与原库的逐条对照多出一处无谓差异。
 *   [已修] 3 —— inbuf / outbuf / pixel_size 原先一概不判。-> 入口全判上了:
 *                inbuf / outbuf 为 NULL 直接崩; pixel_size 为 0 时
 *                count * pixel_size 恒为 0, while 会在不搬任何数据的情况下
 *                把整个输入当 sign 逐字节吃掉。本函数只支持 1 / 2 两种宽度。
 *   [已修] 4 —— ASSERT(0) 在 config_asser 为假时不停机、继续跑出一帧错图。
 *                -> 两处 default 分支在 ASSERT 之后补 return -1。入口已挡住
 *                   非法 pixel_size, 这里是双保险。
 *
 *
 * 1) 提前解满(pos == len)而 break 出去时, 返回的 decSize 【不含当前这一块】——
 *    因为 decSize 是在循环体末尾才累加的, break 跳过了那次累加。
 *    也就是说"解码成功"时返回的已解码字节数会偏小一个块。调用方若拿它当
 *    "已消费的源数据长度"会出错; 目前各调用方只判 >= 0, 所以没暴露。
 *
 * 2) i 与 copylen 之外, 函数作用域那个 count(第 17 行)被循环体内同名的
 *    局部变量完全遮蔽, 从头到尾没被使用。i 也是。都是原厂遗留的死变量。
 *
 * 3) 不检查 inbuf / outbuf 是否为 NULL, 也不检查 pixel_size 是否为 0
 *    (为 0 时 count * pixel_size 恒为 0, while 会在 src 不前进的情况下空转 ——
 *     实际上 src 每轮仍 +1, 所以能退出, 但会把整个输入当作 sign 逐字节吃掉)。
 *
 * 4) pixel_size 只支持 1 和 2, 其余走 ASSERT(0)。而 ASSERT 在 config_asser
 *    为假时只调 cpu_assert 不停机, 之后仍会继续执行(copylen 已算出但没搬数据),
 *    等于静默产生一帧错误图像。
 */
