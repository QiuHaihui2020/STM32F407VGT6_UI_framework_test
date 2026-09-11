/*
 * quicklz.c —— QuickLZ 解压(仅解压侧, 压缩侧不在库里)
 *
 * 【这是第三方库】QuickLZ 1.5.0 的解压部分(QLZ_COMPRESSION_LEVEL 1、
 *   QLZ_STREAMING_BUFFER 0)。这里只需要解压所需的五个函数, 压缩侧不收录。
 *   各分支的位域布局按 QuickLZ 的流格式实现。
 *
 * 【本工程当前编不到 —— 已用最终固件符号表复核】
 *   调用链是 image_decode()(resfile.c, case 2) -> quicklz_decode() -> qlz_decompress()。
 *   quicklz_decode 【是】有调用者的, 但整条链在最终固件里符号数全为 0:
 *   image_decode / quicklz_decode / qlz_decompress / qlz_decompress_core
 *   一个都没链进去, 被优化整体丢掉了。
 *
 *   判死活别只看"有没有调用点" —— 还得看调用者自己是否活着。可靠的做法是
 *   在最终固件的符号表里数该函数的出现次数, 并【同时数一组已知活着的函数
 *   做对照】(如 Rle_Decode 37、open_resfile 12), 免得全 0 其实是匹配方式
 *   写错了。
 *
 *   收录它是为了让资源层的解压分支完整。也正因为当前是死代码, 文末注意
 *   事项里那两条边界问题暂不处理。
 *
 * 【本文件不需要 #line】本模块【没有任何 ASSERT】, 也没有字符串常量 ——
 *   全局只有 qlz_decompress_core 里那张 static const bitlut 表。
 *   所以不存在 __FILE__/__LINE__ 依赖, 不必像 rle.c / ascii.c 那样拨行号。
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
 * @note bytes 取 1~4 之外的值时返回 0, 不报错 —— 沿用 QuickLZ 的行为。
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

    /* 这两个形参在 level-1 + 无流式缓冲的配置下用不到, 只是挂着占位。 */
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
             * offset 与 matchlen 直接来自压缩流, 【必须校验后才能用】——
             * QuickLZ 的流格式假定输入可信, 不校验时一段被篡改或损坏的数据
             * 就能造成任意越界读写:
             *
             *   · offset 大于已输出长度 -> 读到 destination 之前;
             *   · dst + matchlen 越过 last_destination_byte -> 写出界。
             *
             * 两者都只能靠数据自证, 查出来就只能判定流已损坏、返回 0 让调用方
             * 知道解压失败(正常返回的是 size, 不会是 0)。
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

    /* 三个指针都要判空: source 为 NULL 时下面 qlz_size_decompressed 头一件事
     * 就是解引用它。
     * 注意【destination 缓冲区够不够大仍然无法校验】—— 接口没把它的长度传
     * 进来, 长度全靠压缩流头部自述。调用方必须先调一次 qlz_size_decompressed
     * 并按它分配。 */
    if (source == NULL || destination == NULL || state == NULL) {
        return 0;
    }

    dsiz = qlz_size_decompressed(source);

    if ((*source & 1) == 1) {
        /* core 的返回值要承接: 直接 return dsiz 的话, 解压失败与成功对调用方
         * 完全不可区分。core 判定流已损坏时返回 0。 */
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
 * 实现注意事项与已知限制
 *
 *  1) 【解压侧校验了边界】qlz_decompress_core 对从压缩流读出的 offset 与
 *     matchlen 都做了范围检查, 判定流已损坏时返回 0(正常返回 size)。
 *     QuickLZ 的流格式本身假定输入可信, 没有这两处检查就能被一段损坏数据
 *     造成任意越界读写。
 *
 *  2) 【destination 缓冲区大小无法校验】接口没把目标长度传进来, 长度全靠
 *     压缩流头部自述。调用方必须先用 qlz_size_decompressed 算出长度再分配 ——
 *     这是接口形态决定的, 不改签名修不了。
 *
 *  3) 【fast_read 的非法 bytes 静默返回 0】调用方无法区分"读到的就是 0"与
 *     "参数非法"。它是文件内的 static helper, 四个调用点传的都是字面量 4
 *     或 CWORD_LEN, 取不到非法值。
 *
 *  4) 【本模块当前是死代码】整条 image_decode -> quicklz_decode ->
 *     qlz_decompress 链在最终固件里符号数全为 0(见文件开头)。上面那些检查是
 *     为改回彩屏配置、它重新变活时准备的。
 */
