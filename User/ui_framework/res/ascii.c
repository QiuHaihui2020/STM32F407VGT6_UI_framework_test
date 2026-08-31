/*
 * ascii.c —— ASCII 字模的读取(点阵字库 .PIX/.TAB 里的 ASCII 部分)
 *
 * 【来源】从 cpu/br27/liba/res.a 的 ascii.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/ascii.ll
 *     原始路径: btsdk/lib/utils/ui/resource/ascii.c
 *
 * 【谁在用】驱动层 ui_synthesis_oled.c(取字模) 与 ui_resources_manager.c(初始化)。
 *
 * 【文件格式】字库文件开头 1 字节是 font_size(字高), 之后是一张按字符码索引的
 *   索引表, 每项 4 字节(struct ascii_head): width / size / addr。
 *   取某个字符: 定位到 code * 4 + 2 处读 4 字节表项, 表项里的 addr 是【大端】,
 *   换成小端后再定位过去读 size 字节点阵。
 *   注: +2 是跳过文件头那 1 字节 font_size 后再偏移 1, 与打包工具约定一致。
 *
 * 【行号锁定】ASSERT 宏内嵌 __LINE__, 必须落在原始行号 96。函数体由
 *   cpu/br27/tools/ui_reimpl/gen_ascii.py 按绝对行号拼出, 空行不要随意增删。
 *
 * 【段属性】代码在 .ascii.text; file 在 .ascii.data; font_size 在 .ascii.data.bss。
 *
 * 【两处不能"写干净"的地方, 改了就与原厂不等价】
 *   1. font_size 必须 aligned(4)。u8 默认 align 1, 而原厂 IR 是 align 4。
 *   2. 字节交换必须保留那两个冗余掩码(即 font_all.h 里 font_ntoh 宏的形态)。
 *      写成 (x >> 8) | (x << 8) 会被 InstCombine 折成 llvm.bswap.i16,
 *      原厂保留的是 lshr/shl/or/trunc 展开形式。掩码本身会被 demanded-bits
 *      消掉(IR 里看不见 and), 但它挡住了 bswap 的识别。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma data_seg(".ascii.data")
#pragma bss_seg(".ascii.data.bss")
#pragma code_seg(".ascii.text")
#endif

#include "jl_os_api.h"
#include "res/resfile.h"
#include "res/font_ascii.h"
#include "jl_debug.h"    /* ASSERT: 原厂靠别处间接带入, 这里补成自包含 */

struct ascii_head {
    u8 width;
    u8 size;
    u16 addr;
};

/* 带初值 -> 在声明处就地发射(位于 .ascii.data, 且排在所有字符串常量之前);
 * font_size 不带初值是 tentative definition, 由 clang 在【首次被引用】处
 * 建立(第 42 行), 所以它在 IR 里排在 "fail!!!" 那条字符串之后。顺序必须一致。 */
static RESFILE *file = NULL;
static u8 font_size __attribute__((aligned(4)));

#line 26
int font_ascii_init(const char *name)
{
    if (file) {

        res_fclose(file);
        file = NULL;
    }

    file = res_fopen(name, "r");

    if (!file) {
        puts("font_ascii_init fail!!!\n");
        return -2;
    }


    /* 加固: 原库丢弃返回值。读不到字高就把文件关掉并报错, 否则后面每次取字模
     * 都会拿一个未初始化(实为 0)的字高去画, 表现为整屏无字却无任何提示。 */
    if (res_fread(file, &font_size, 1) != 1) {
        puts("font_ascii_init: read font_size fail!!!\n");
        res_fclose(file);
        file = NULL;
        return -1;
    }

    return 0;
}

void font_ascii_get_width_and_height(char code, int *height, int *width)
{
    int err;
    int offset;
    struct ascii_head head;

    /* 加固: 原库出错时【直接 return 而不给 *height / *width 赋值】,
     * 调用方拿到的是未初始化的栈值。这里统一先清零。 */
    if (height == NULL || width == NULL) {
        return;
    }
    *height = 0;
    *width  = 0;

    if (!file) {
        puts("font_ascii_init fail_1!!!\n");
        return;
    }

    /* 加固: code 是 char(本目标有符号), 字符码 >= 0x80 时 code * 4 为负,
     * 会定位到文件头之前。按无符号取索引。 */
    offset = (u8)code * sizeof(struct ascii_head) + 2;

    /* @note res_fseek 的返回值【故意不判】: 它转调闭源的 resfile_seek,
     * 成功时返回 0 还是新偏移无从确认, 贸然判断可能把成功当失败。
     * 定位失败会由紧接着的 res_fread 读不满而暴露出来。 */
    res_fseek(file, offset, SEEK_SET);

    /* 加固: 原库丢弃返回值, 读失败就拿栈上未初始化的 head 往下算。 */
    if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
        return;
    }

    head.addr = ((head.addr >> 8) & 0x00ff) | ((head.addr << 8) & 0xff00);

    res_fseek(file, head.addr, SEEK_SET);

    *height = font_size;
    *width  = head.width;
}

int font_ascii_get_pix(char code, u8 *pixbuf, int buflen, int *height, int *width)
{
    int err;
    int offset;
    struct ascii_head head;

    /* 加固: 原库不判 pixbuf(其余三个函数对入参指针也一概不判)。
     * 这是要往里写点阵的目标缓冲, 为空就没什么可做的了。 */
    if (pixbuf == NULL || height == NULL || width == NULL) {
        return -1;
    }

    if (!file) {
        puts("font_ascii_init fail_1!!!\n");
        return -1;
    }

    /* 加固: 同 font_ascii_get_width_and_height, code 按无符号取索引。 */
    offset = (u8)code * sizeof(struct ascii_head) + 2;

    /* @note res_fseek 返回值故意不判, 理由见 font_ascii_get_width_and_height。 */
    res_fseek(file, offset, SEEK_SET);

    /* 加固: 原库丢弃返回值。索引表读不全就往下走, head.size / head.addr
     * 全是栈垃圾, 会直接喂给下面那次 res_fread。 */
    if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
        return -1;
    }

    head.addr = ((head.addr >> 8) & 0x00ff) | ((head.addr << 8) & 0xff00);

    res_fseek(file, head.addr, SEEK_SET);

    *height = font_size;
    *width  = head.width;

    /*
     * 加固【本文件最严重的一处】: 原库只有 ASSERT, 而 ASSERT 在 config_asser
     * 为假时只调 cpu_assert【不停机】, 之后照样执行 res_fread(pixbuf, head.size)
     * —— 断言失败紧接着就是一次缓冲区溢出写。ASSERT 保留(开发期仍要停机),
     * 后面补一道真正拦得住的检查。
     */
    ASSERT(head.size <= buflen);
    if (buflen <= 0 || head.size > buflen) {
        return -1;
    }

    return res_fread(file, pixbuf, head.size);
}



int font_ascii_width_check(const char *str)
{

    int err;
    int offset;
    int width_sum = 0;
    struct ascii_head head;
    if (str == NULL) {
        return 0;
    }

    if (!file) {
        puts("font_ascii_init fail_2!!!\n");
        return -1;
    }

    while (*str != 0) {
        /* 加固: 同前, *str 是 char, 按无符号取索引。 */
        offset = (u8)(*str) * sizeof(struct ascii_head) + 2;

        /* @note res_fseek 返回值故意不判, 理由见 font_ascii_get_width_and_height。 */
        res_fseek(file, offset, SEEK_SET);

        /* 加固: 原库丢弃返回值, 读失败会把栈垃圾累加进总宽度。 */
        if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
            return -1;
        }
        width_sum += head.width;
        str++;
    }



    return width_sum;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍然照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在
 * cpu/br27/tools/ui_reimpl/accept/ascii.txt 并锁定指纹)。
 *
 *   [已修] 1 —— 三处 res_fread 的返回值都补上了判断。但 res_fseek 的返回值
 *                【故意仍不判】: 它转调闭源的 resfile_seek, 成功时返回 0 还是
 *                新偏移无从确认, 贸然判断可能把成功当失败; 定位失败会由紧接着
 *                的 res_fread 读不满而暴露。
 *   [已修] 2 —— file 为 NULL 时先把 *height / *width 清零再 return。
 *   [已修] 3 —— ASSERT 之后补了一道真正拦得住的 if, 断言不停机也不会溢出写。
 *   [已修] 4 —— 四个函数对入参指针一概不判。-> font_ascii_get_pix 补上了
 *                pixbuf / height / width 三个判空,
 *                font_ascii_get_width_and_height 补了 height / width。
 *   [已修] 5 —— code / *str 一律按 (u8) 取索引, 不再出现负偏移。
 *
 * 【注意】文件头"行号锁定 ASSERT 必须落在 96"那条【已随加固失效】:
 * 源码已改, ASSERT 就该打印真实行号, 不再用 #line 拨回。
 *
 *
 * 1) 四个函数里的 err / offset 有一半是死变量: 三处 res_fseek / res_fread 的
 *    返回值【全部被丢弃】, err 从头到尾没被赋过值。也就是说字库文件读失败
 *    (定位越界、文件损坏)时, 上层拿到的是一帧未初始化的点阵数据, 无从察觉。
 *
 * 2) font_ascii_get_width_and_height 在 file 为 NULL 时【直接 return】, 却不给
 *    *height / *width 赋值 —— 调用方拿到的是未初始化的栈值。
 *
 * 3) font_ascii_get_pix 的 ASSERT(head.size <= buflen) 在 config_asser 为假时
 *    只调 cpu_assert 不停机, 之后仍会 res_fread(file, pixbuf, head.size) ——
 *    即断言失败后紧接着就是一次【缓冲区溢出写】。
 *
 * 4) font_ascii_width_check 对 str 判了 NULL, 另外三个函数对 pixbuf /
 *    height / width 一概不判。
 *
 * 5) code 是 char(本目标有符号)。字符码 >= 0x80 时 code * 4 为负,
 *    定位到文件头之前 —— 扩展 ASCII 会读到越界偏移。
 */
