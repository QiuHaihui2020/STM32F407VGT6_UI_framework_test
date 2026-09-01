/*
 * font_ascii.c —— ASCII 字模文件(0x00~0x7F)的索引与取模
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_ascii.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_ascii.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_ascii.c
 *
 * 【还原依据】
 *   函数原始行号(DISubprogram): InitFont_ASCII@13  GetASCIICharacterData@34
 *                              GetASCIICharacterWidth@73
 *   局部变量名取自 DWARF: ascinfo / addr / nbytes。
 *   ASCSTRUCT = {u8 width; u8 size; u16 addr}, sizeof=4, 与 font/font_all.h
 *   里的定义逐字段吻合(IR 里读的就是 4 字节)。
 *   字模文件布局(由本文件的寻址方式反推):
 *     偏移 0      : 1 字节, 字高(点数) —— InitFont_ASCII 读走
 *     偏移 2+4*n  : 第 n 个 ASCII 字符的 ASCSTRUCT(width/size/addr)
 *     addr        : 该字符点阵数据在文件里的偏移, 【大端】存放, 用 font_ntoh 转
 *   本模块无 ASSERT。
 *
 * 【段属性】原库代码在 .font_ascii.text(见 ref IR 的 section 属性)。
 *   三个字符串常量在原厂 IR 里【没有 section 属性】, 所以不能开 const_seg。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_ascii.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "jl_debug.h"    /* printf / puts: 原厂靠别处间接带入 */

u8 InitFont_ASCII(struct font_info *info);
u8 GetASCIICharacterData(struct font_info *info, u16 asc);
u8 GetASCIICharacterWidth(struct font_info *info, u16 asc);

/*
 * @brief 打开 ASCII 字模文件, 读出字高并算出单字符点阵字节数
 * @return 1 = 成功; 0 = 打开失败
 */
u8 InitFont_ASCII(struct font_info *info)
{
    info->ascpixel.file.fd = font_sd_fopen(info->ascpixel.file.name, "r");
    if (info->ascpixel.file.fd == NULL) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, 0);

    /* 加固: 原库丢弃返回值。读不到字高时 info->ascpixel.size 保持旧值
     * (首次调用就是未初始化内存), 而函数仍返回 1 表示成功 —— 此后整套
     * nbytes 计算全建立在垃圾值上。现在读失败就把文件关掉并如实报错。 */
    if (font_sd_fread(info->ascpixel.file.fd, &info->ascpixel.size, 1) != 1) {
        font_sd_fclose(info->ascpixel.file.fd);
        info->ascpixel.file.fd = NULL;
        return 0;
    }

    info->ascpixel.nbytes = ((info->ascpixel.size + 7) / 8) * info->ascpixel.size;

    return 1;
}

/*
 * @brief 取一个 ASCII 字符的点阵数据到 info->ascpixel.pixelbuf
 * @param asc 字符码, 必须 < 128
 * @return 该字符的宽度(点数); 0 = 非 ASCII 或缓冲不够
 *
 * @note nbytes 是按【本字符的 width】算的, 而 info->ascpixel.nbytes 是
 *       InitFont_ASCII 里按【字高】算的 —— 两者用同一个字段做上限比较,
 *       所以宽字符会走进重分配分支。
 */
u8 GetASCIICharacterData(struct font_info *info, u16 asc)
{
    ASCSTRUCT ascinfo;
    u16 nbytes;

    if (asc > 127) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, asc * 4 + 2);

    /* 加固: 原库丢弃返回值。索引表项读不全时 ascinfo 是栈垃圾, 后面拿它的
     * width 算 nbytes、拿它的 addr 去 fseek, 一路错到底。返回 0 表示取不到。 */
    if (font_sd_fread(info->ascpixel.file.fd, &ascinfo, sizeof(ASCSTRUCT)) != sizeof(ASCSTRUCT)) {
        return 0;
    }

    nbytes = ((info->ascpixel.size + 7) / 8) * ascinfo.width;

    if (info->ascpixel.pixelbuf == NULL) {
        return ascinfo.width;
    }

    if (nbytes > info->ascpixel.nbytes) {
        /* 加固: puts 本身会补换行, 原库这里多写了一个 —— 该错误会多空一行。 */
        puts("error:pixelbuf overlay!");
        printf("ascinfo.width = %d, info->ascpixel.size = %d, nbytes = %d, info->ascpixel.nbytes = %d\n",
               ascinfo.width, info->ascpixel.size, nbytes, info->ascpixel.nbytes);
        /*
         * 加固: 原库【不检查 malloc 返回值】就把结果写回 pixelbuf, 还把 nbytes
         * 一并更新。堆耗尽时 pixelbuf 成了 NULL 而 nbytes 是新值, 此后每次调用
         * 都走上面那个 "pixelbuf == NULL" 分支直接返回宽度 —— 不会立刻崩,
         * 但这个字库【从此再也取不出点阵】, 而且没有任何提示。
         * 现在分配失败就保持 nbytes 不变并报一声, 下次还会再试一次。
         */
        free(info->ascpixel.pixelbuf);
        info->ascpixel.pixelbuf = malloc(nbytes);
        if (info->ascpixel.pixelbuf == NULL) {
            puts("error:pixelbuf realloc fail!");
            return 0;
        }
        info->ascpixel.nbytes = nbytes;
        return 0;
    }

    ascinfo.addr = font_ntoh(ascinfo.addr);

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, ascinfo.addr);

    /* 加固: 原库丢弃返回值。点阵读失败时 pixelbuf 里还是【上一个字】的点阵,
     * 却照常返回宽度 —— 表现为"显示上一个字", 排查起来很费劲。 */
    if (font_sd_fread(info->ascpixel.file.fd, info->ascpixel.pixelbuf, nbytes) != nbytes) {
        return 0;
    }

    return ascinfo.width;
}

/*
 * @brief 只取一个 ASCII 字符的宽度(不读点阵)
 * @return 宽度(点数); 0 = 非 ASCII
 */
u8 GetASCIICharacterWidth(struct font_info *info, u16 asc)
{
    ASCSTRUCT ascinfo;

    if (asc > 127) {
        return 0;
    }

    font_sd_fseek(info->ascpixel.file.fd, SD_SEEK_SET, asc * 4 + 2);

    /* 加固: 原库丢弃返回值, 读失败会把栈垃圾当宽度返回。 */
    if (font_sd_fread(info->ascpixel.file.fd, &ascinfo, sizeof(ASCSTRUCT)) != sizeof(ASCSTRUCT)) {
        return 0;
    }

    return ascinfo.width;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— 重分配分支不检查 malloc 返回值 -> 已补: 失败时保持 nbytes 不变
 *                并报一声, 下次还会再试。原库会把 nbytes 更新成新值, 于是此后
 *                每次都走 pixelbuf==NULL 分支, 这个字库【从此再也取不出点阵】。
 *   [已修] 2 —— free 与 malloc 之间没置 NULL -> 现在失败即返回、不再更新 nbytes,
 *                旧指针已 free 且不会再被使用。
 *   [已修] 3 —— InitFont_ASCII 不检查 font_sd_fread -> 已补(读不到字高就关掉
 *                文件并返回 0)。另外两个函数的三处 fread 也一并补了。
 *   [已修] 4 —— puts 里多余的换行 -> 已去掉。
 *
 * 1) GetASCIICharacterData 的重分配分支【不检查 malloc 返回值】就把结果写回
 *    info->ascpixel.pixelbuf, 并且把 nbytes 也一并更新。堆耗尽时 pixelbuf 变
 *    NULL、nbytes 却是新值, 下一次调用会走到 `pixelbuf == NULL` 分支直接返回
 *    宽度(不至于立刻崩), 但从此这个字库再也取不出点阵, 且失败无任何提示。
 *
 * 2) 同一分支里 free 之后 malloc, 中间没有把 pixelbuf 先置 NULL。若 malloc
 *    失败(返回 NULL)倒是安全, 但若这段代码将来被加上"失败则保留旧值"的补丁,
 *    旧指针已经被 free 掉了 —— 加固时要注意。
 *
 * 3) InitFont_ASCII 不检查 font_sd_fread 的返回值, 读失败时 info->ascpixel.size
 *    保持旧值(首次调用是未初始化内存), 而函数仍返回 1 表示成功。
 *
 * 4) puts("error:pixelbuf overlay!\n") 里的 '\n' 是多余的 —— puts 本身会补
 *    换行, 于是这条错误会空一行。属原厂写法, 参考 IR 确认该字符串常量本身就
 *    带 \n 且直接传给 puts(不是 printf 被折成 puts 的产物, 那种情况新建的
 *    全局不带 align 且会去掉 \n)。
 */
