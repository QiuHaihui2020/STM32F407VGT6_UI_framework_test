/*
 * quicklz.c —— QuickLZ 解压(仅解压侧, 压缩侧不在库里)
 *
 * 【来源】从 cpu/br27/liba/res.a 的 quicklz.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/quicklz.ll
 *     原始路径: btsdk/lib/utils/ui/resource/quicklz.c
 *
 * 【这是第三方库】QuickLZ 1.5.0 的解压部分(QLZ_COMPRESSION_LEVEL 1、
 *   QLZ_STREAMING_BUFFER 0)。原厂只保留了解压所需的五个函数, 压缩侧整块删掉了。
 *   还原时逐条比对 IR 确认了各分支的位域布局与原版一致。
 *
 * 【本工程完全用不到 —— 已用最终固件符号表复核】
 *   调用链是 image_decode()(resfile.c, case 2) -> quicklz_decode() -> qlz_decompress()。
 *   注意 quicklz_decode 【是】有调用者的(早先这里写"无人调用"不准确), 但整条链
 *   在最终固件里全部为 0: image_decode / quicklz_decode / qlz_decompress /
 *   qlz_decompress_core 一个都没链进去, 被 LTO 整体丢掉了。
 *
 *   判死活的方法(见 README 3.3): 光看"有没有调用点"不够 —— 得看调用者自己
 *   是否活着。硬证据是在最终固件符号表里数该函数的出现次数, 并【同时跑一组
 *   已知活着的函数做对照】(如 Rle_Decode 37、open_resfile 12), 免得全 0 其实是
 *   匹配方式写错了。
 *
 *   还原它只是为了把 res.a 清空。也正因为是死代码, 文末 TODO 那两条边界问题
 *   不在加固范围内(见 README 9.3)。
 *
 * 【无需行号锁定】本模块【没有任何 ASSERT】, 也没有字符串常量 ——
 *   全局只有 qlz_decompress_core 里那张 static const bitlut 表。
 *   因此不存在 __FILE__/__LINE__ 依赖, 不必像 rle.c / ascii.c 那样用 #line 拨行号。
 *
 * 【段属性】代码在 .quicklz.text; bitlut 在 .quicklz.text.const。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma const_seg(".quicklz.text.const")
#pragma code_seg(".quicklz.text")
#endif

#include "jl_os_api.h"

typedef unsigned int ui32;

/* 压缩流的控制字长度: 每 32 个"项"共用一个 4 字节控制字, 每位标记该项是
 * 字面量(0)还是匹配(1)。 */
#define CWORD_LEN               4

/* 解压时留在末尾不做快速通道的字节数, 以及无条件匹配的最短长度 ——
 * 两者合起来决定 last_matchstart, 见 qlz_decompress_core。 */
#define UNCOMPRESSED_END        4
#define UNCONDITIONAL_MATCHLEN  6

typedef struct {
    ui32 stream_counter;
} qlz_state_decompress;

size_t qlz_size_decompressed(const char *source);
size_t qlz_size_compressed(const char *source);
size_t qlz_size_header(const char *source);
size_t qlz_decompress(const char *source, void *destination, qlz_state_decompress *state);

/*
 * @brief 从 src 处按小端读出 bytes(1~4) 个字节拼成一个 32 位值
 * @note bytes 取其它值时返回 0 —— 原版就是这样, 不报错。
 */
static inline ui32 fast_read(const void *src, ui32 bytes)
{
    const unsigned char *p = (const unsigned char *)src;

    switch (bytes) {
    case 4:
        return (*p | *(p + 1) << 8 | *(p + 2) << 16 | *(p + 3) << 24);
    case 3:
        return (*p | *(p + 1) << 8 | *(p + 2) << 16);
    case 2:
        return (*p | *(p + 1) << 8);
    case 1:
        return (*p);
    }

    return 0;
}

/*
 * @brief 逐字节向前拷贝, 【允许 src 与 dst 重叠且 src 在 dst 之前】
 * @note 这正是 LZ 回溯匹配需要的语义: 先写出的字节可以作为后续读取的源,
 *       从而用一个短匹配展开出更长的重复串。不能换成 memcpy。
 */
static inline void memcpy_up(unsigned char *dst, const unsigned char *src, ui32 n)
{
    unsigned char *end = dst + n;

    while (dst < end) {
        *dst = *src;
        dst++;
        src++;
    }
}

size_t qlz_size_decompressed(const char *source)
{
    ui32 n, r;

    n = (((*source) & 2) == 2) ? 4 : 1;
    r = fast_read(source + 1 + n, n);
    r = r & (0xffffffff >> ((4 - n) * 8));

    return r;
}

size_t qlz_size_compressed(const char *source)
{
    ui32 n, r;

    n = (((*source) & 2) == 2) ? 4 : 1;
    r = fast_read(source + 1, n);
    r = r & (0xffffffff >> ((4 - n) * 8));

    return r;
}

size_t qlz_size_header(const char *source)
{
    size_t n = 2 * ((((*source) & 2) == 2) ? 4 : 1) + 1;

    return n;
}

/*
 * @brief QuickLZ level-1 解压主循环
 *
 * 控制字 cword_val 的最低位标记当前项的类型, 用完 32 位后重新取 4 字节。
 * 匹配项按 fetch 的低位分成五种编码(偏移与长度的位宽不同), 见下面各分支。
 */
static size_t qlz_decompress_core(const unsigned char *source, unsigned char *destination,
                                  size_t size, qlz_state_decompress *state,
                                  unsigned char *history)
{
    const unsigned char *src = source + qlz_size_header((const char *)source);
    unsigned char *dst = destination;
    const unsigned char *last_destination_byte = destination + size - 1;
    ui32 cword_val = 1;
    const unsigned char *last_matchstart = last_destination_byte - UNCONDITIONAL_MATCHLEN - UNCOMPRESSED_END;
    ui32 offset;

    /* 这两个形参在 level-1 + 无流式缓冲的配置下用不到, 原版也只是挂着。 */
    (void)state;
    (void)history;

    for (;;) {
        ui32 fetch;

        if (cword_val == 1) {
            cword_val = fast_read(src, 4);
            src += CWORD_LEN;
        }

        fetch = fast_read(src, 4);

        if ((cword_val & 1) == 1) {
            ui32 matchlen;
            const unsigned char *offset2;

            cword_val = cword_val >> 1;

            if ((fetch & 3) == 0) {
                offset = (fetch & 0xff) >> 2;
                matchlen = 3;
                src += 1;
            } else if ((fetch & 2) == 0) {
                offset = (fetch & 0xffff) >> 2;
                matchlen = 3;
                src += 2;
            } else if ((fetch & 1) == 0) {
                offset = (fetch & 0xffff) >> 6;
                matchlen = ((fetch >> 2) & 15) + 3;
                src += 2;
            } else if ((fetch & 127) != 3) {
                offset = (fetch >> 7) & 0x1ffff;
                matchlen = ((fetch >> 2) & 31) + 2;
                src += 3;
            } else {
                offset = (fetch >> 15);
                matchlen = ((fetch >> 7) & 255) + 3;
                src += 4;
            }

            /*
             * 加固: 原库【完全不校验边界】, 从压缩流里读出的 offset 与 matchlen
             * 直接拿去 memcpy_up —— 一段被篡改或损坏的压缩数据可以造成任意
             * 越界读写。这是 QuickLZ 原版就有的性质(它假定输入可信)。
             *
             *   · offset 大于已输出长度 -> 读到 destination 之前;
             *   · dst + matchlen 越过 last_destination_byte -> 写出界。
             *
             * 两者都是【只能靠数据自证】的量, 查出来就只能判定流已损坏、
             * 返回 0 让调用方知道解压失败(正常返回的是 size, 不会是 0)。
             */
            if (offset > (ui32)(dst - destination)) {
                return 0;
            }
            if ((size_t)(dst - destination) + matchlen > size) {
                return 0;
            }

            offset2 = (const unsigned char *)(dst - offset);

            memcpy_up(dst, offset2, matchlen);
            dst += matchlen;
        } else {
            if (dst < last_matchstart) {
                /* 快速通道: 一次搬 4 字节, 再按 bitlut 查出这 4 位里
                 * 连续字面量的个数 n, 一次推进 n。 */
                static const ui32 bitlut[16] = {
                    4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
                };
                ui32 n = bitlut[cword_val & 0xf];

                memcpy_up(dst, src, 4);
                cword_val = cword_val >> n;
                dst += n;
                src += n;
            } else {
                /* 收尾: 逼近输出末尾时不能再多搬, 逐字节抄完就结束。 */
                while (dst <= last_destination_byte) {
                    if (cword_val == 1) {
                        src += CWORD_LEN;
                        cword_val = 1U << 31;
                    }

                    *dst = *src;
                    dst++;
                    src++;
                    cword_val = cword_val >> 1;
                }

                return size;
            }
        }
    }
}

size_t qlz_decompress(const char *source, void *destination, qlz_state_decompress *state)
{
    size_t dsiz;

    /* 加固: 原库不检查这三个指针。source 为 NULL 时下面 qlz_size_decompressed
     * 头一件事就是解引用它。
     * 注意【仍然无法校验 destination 缓冲区够不够大】—— 接口没把它的长度传进来,
     * 长度全靠压缩流头部自述。调用方必须先自己调一次 qlz_size_decompressed
     * 并按它分配, 这一点没变。 */
    if (source == NULL || destination == NULL || state == NULL) {
        return 0;
    }

    dsiz = qlz_size_decompressed(source);

    if ((*source & 1) == 1) {
        /* 加固: 原库【丢弃 core 的返回值】直接 return dsiz, 于是解压失败与
         * 成功对调用方完全不可区分。现在承接它 —— core 判定流已损坏时返回 0。 */
        if (qlz_decompress_core((const unsigned char *)source, (unsigned char *)destination,
                                dsiz, state, (unsigned char *)destination) == 0) {
            state->stream_counter = 0;
            return 0;
        }
    } else {
        memcpy(destination, source + qlz_size_header(source), dsiz);
    }

    state->stream_counter = 0;

    return dsiz;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在
 * cpu/br27/tools/ui_reimpl/accept/quicklz.txt 并锁定指纹)。
 *
 *   [已修] 1 —— 【解压侧完全不校验边界】qlz_decompress_core 从压缩流里读出的
 *                offset 与 matchlen 不做任何检查就直接
 *                memcpy_up(dst, dst - offset, matchlen):
 *                  · offset 大于已输出长度时会【读到 destination 缓冲区之前】;
 *                  · dst + matchlen 可能【越过 last_destination_byte】写出界。
 *                一段被篡改或损坏的压缩数据可以造成任意越界读写。这是 QuickLZ
 *                原版就有的性质(它假定输入可信), 不是移植引入的。
 *                -> 两处都补了检查, 判定流已损坏时 return 0(正常返回 size,
 *                   不会是 0), 并由 qlz_decompress 承接这个失败。
 *
 *   [部分] 2 —— 三个指针的判空已补上。但【destination 缓冲区够不够大仍然无法
 *                校验】: 接口根本没把它的长度传进来, 长度全靠压缩流头部自述。
 *                调用方必须先自己调一次 qlz_size_decompressed 并按它分配 ——
 *                这一点是接口形态决定的, 不改签名就修不了。
 *
 *   [保留] 3 —— fast_read 对 bytes 取 1~4 之外的值静默返回 0, 调用方无法区分
 *                "读到的就是 0" 与 "参数非法"。它是文件内的 static helper,
 *                四个调用点传的都是字面量 4 或 CWORD_LEN, 取不到非法值。
 *
 * 【注意】本模块是死代码(整条 image_decode -> quicklz_decode -> qlz_decompress
 * 链在最终固件里符号数全为 0, 见文件开头)。加固是为了改回彩屏配置时它变活。
 */
