/*
 * ascii.c —— ASCII 字模的读取(点阵字库 .PIX/.TAB 里的 ASCII 部分)
 *
 * 【谁在用】驱动层 ui_synthesis_oled.c(取字模) 与 ui_resources_manager.c(初始化)。
 *
 * 【文件格式】字库文件开头 1 字节是 font_size(字高), 之后是一张按字符码索引的
 *   索引表, 每项 4 字节(struct ascii_head): width / size / addr。
 *   取某个字符: 定位到 code * 4 + 2 处读 4 字节表项, 表项里的 addr 是【大端】,
 *   换成小端后再定位过去读 size 字节点阵。
 *   注: +2 是跳过文件头那 1 字节 font_size 后再偏移 1, 与打包工具约定一致。
 *
 * 【行号约定】ASSERT 宏内嵌 __LINE__。下面的 #line 26 把行号拨回函数自身的
 *   布局, 让断言打印的行号不受文件头这段说明增删的影响。
 *
 * 【段属性】代码在 .ascii.text; file 在 .ascii.data; font_size 在 .ascii.data.bss。
 *
 * 【两处不能"写干净"的地方】
 *   1. font_size 必须 aligned(4)。u8 默认 align 1, 这里要求 4 字节对齐 ——
 *      它会被按字访问。
 *   2. 字节交换必须保留那两个冗余掩码(即 font_all.h 里 font_ntoh 宏的形态)。
 *      写成 (x >> 8) | (x << 8) 会被优化器折成一条字节交换指令; 必须保留
 *      移位加或的展开形式。掩码本身会被优化掉, 但它挡住了优化器对
 *      "这是一次字节交换"的识别。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma data_seg(".ascii.data")
#pragma bss_seg(".ascii.data.bss")
#pragma code_seg(".ascii.text")
#endif

#include "jl_os_api.h"
#include "res/resfile.h"
#include "res/font_ascii.h"
#include "jl_debug.h"    /* ASSERT: 显式包含, 保证本文件自包含 */

struct ascii_head {
    u8 width;
    u8 size;
    u16 addr;
};

/* 带初值 -> 在声明处就地发射(位于 .ascii.data, 且排在所有字符串常量之前);
 * font_size 不带初值是 tentative definition, 由 clang 在【首次被引用】处
 * 建立(第 42 行), 所以它排在 "fail!!!" 那条字符串常量之后。顺序必须保持。 */
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


    /* 读不到字高就把文件关掉并报错: 否则后面每次取字模都会拿一个未初始化
     * (实为 0)的字高去画, 表现为整屏无字却没有任何提示。 */
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

    /* 出参统一先清零: 任何一条失败路径都会直接 return, 不清零的话调用方
     * 拿到的是未初始化的栈值。 */
    if (height == NULL || width == NULL) {
        return;
    }
    *height = 0;
    *width  = 0;

    if (!file) {
        puts("font_ascii_init fail_1!!!\n");
        return;
    }

    /* code 是 char(本目标有符号), 字符码 >= 0x80 时 code * 4 会是负数、
     * 定位到文件头之前, 所以按无符号取索引。 */
    offset = (u8)code * sizeof(struct ascii_head) + 2;

    /* @note res_fseek 的返回值【故意不判】: 它转调的 resfile_seek 在各后端
     * 下"成功时返回 0 还是返回新偏移"并不统一, 贸然判断可能把成功当失败。
     * 定位失败会由紧接着的 res_fread 读不满而暴露出来。 */
    res_fseek(file, offset, SEEK_SET);

    /* 读失败就不能往下算 —— head 是栈上变量, 未读满时里面是垃圾。 */
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

    /* pixbuf 是要往里写点阵的目标缓冲, 为空就没什么可做的了;
     * 三个出参在这里一并判空。 */
    if (pixbuf == NULL || height == NULL || width == NULL) {
        return -1;
    }

    if (!file) {
        puts("font_ascii_init fail_1!!!\n");
        return -1;
    }

    /* 同 font_ascii_get_width_and_height, code 按无符号取索引。 */
    offset = (u8)code * sizeof(struct ascii_head) + 2;

    /* @note res_fseek 返回值故意不判, 理由见 font_ascii_get_width_and_height。 */
    res_fseek(file, offset, SEEK_SET);

    /* 索引表读不全就得返回: head.size / head.addr 会是栈垃圾, 直接喂给
     * 下面那次 res_fread 就是按垃圾长度读盘。 */
    if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
        return -1;
    }

    head.addr = ((head.addr >> 8) & 0x00ff) | ((head.addr << 8) & 0xff00);

    res_fseek(file, head.addr, SEEK_SET);

    *height = font_size;
    *width  = head.width;

    /*
     * 【本文件最要紧的一处】ASSERT 不能当拦截用: 它在 config_asser 为假时
     * 只记录、不停机, 之后会照样执行 res_fread(pixbuf, head.size) ——
     * 那就是一次缓冲区溢出写。所以 ASSERT 留着(开发期停机便于定位),
     * 后面再补一道真正拦得住的 if。
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
        /* 同前, *str 是 char, 按无符号取索引。 */
        offset = (u8)(*str) * sizeof(struct ascii_head) + 2;

        /* @note res_fseek 返回值故意不判, 理由见 font_ascii_get_width_and_height。 */
        res_fseek(file, offset, SEEK_SET);

        /* 读失败必须返回, 否则会把栈垃圾累加进总宽度。 */
        if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
            return -1;
        }
        width_sum += head.width;
        str++;
    }



    return width_sum;
}

/*
 * 实现注意事项
 *
 *  1) 【读盘返回值都要判】四个函数里三处 res_fread 都判了实际读到的长度 ——
 *     字库文件损坏或定位越界时, 栈上的 head 里是垃圾, 拿它算偏移会 seek 到
 *     任意位置, 上层则拿到一帧看不出问题的错误点阵。
 *     res_fseek 的返回值【故意不判】: resfile_seek 在各后端下"成功返回 0 还是
 *     返回新偏移"并不统一; 定位失败会由紧接着的 res_fread 读不满暴露出来。
 *
 *  2) 【出参先清零】font_ascii_get_width_and_height 的每条失败路径都直接
 *     return, 所以入口统一把 *height / *width 清零, 调用方不会拿到栈值。
 *
 *  3) 【ASSERT 之后还要拦一道】font_ascii_get_pix 的 ASSERT(head.size <= buflen)
 *     在 config_asser 为假时不停机, 所以后面补了 if —— 否则断言失败紧接着就是
 *     一次缓冲区溢出写。
 *
 *  4) 【字符码按无符号取索引】char 在本目标上有符号, 扩展 ASCII(>= 0x80)
 *     直接乘会得到负偏移、定位到文件头之前, 所以四处都强转 (u8)。
 *
 *  5) 【行号】断言打印的行号以文件顶部那条 #line 26 为基准, 增删它之后的
 *     行会让行号整体平移 —— 上面这些检查占的行已经算在内。
 */
