/*
 * font_sdfs.c —— 字库文件访问层(把 font 模块的 RESFILE* 接口转接到 resfile_*)
 *
 * 【来源】从 cpu/br27/liba/font.a 的 font_sdfs.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/font_sdfs.ll
 *     原始路径: btsdk/lib/utils/ui/font/font_sdfs.c
 *
 * 【还原依据】
 *   函数原始行号(DISubprogram): font_sd_fopen@11  font_sd_fread@23
 *                              font_sd_fseek@35  font_sd_fclose@47
 *   四个函数都是薄封装, 原型与 font/font_sdfs.h 逐字吻合。
 *   IR 里的 RESFILE* <-> RESFILE* 是 bitcast(两种句柄在这里被当同一个指针用),
 *   不是类型转换函数, 所以源码就是直接强转。
 *   本模块无 ASSERT、无全局变量、无字符串常量。
 *
 * 【段属性】原库代码在 .font_sdfs.text(见 ref IR 的 section 属性)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_sdfs.text")
#endif

#include "jl_typedef.h"
#include "jl_fs.h"
#include "font/font_sdfs.h"

RESFILE *font_sd_fopen(const char *filename, void *arg)
{
    return (RESFILE *)resfile_open(filename);
}

int font_sd_fread(RESFILE *fp, void *buf, u32 len)
{
    return resfile_read((RESFILE *)fp, buf, len);
}

/*
 * @note 实参顺序: resfile_seek(fp, offset, fromwhere) —— seek_mode 是第三个,
 *       而本函数的形参表里 seek_mode 在 offset 之前, 别照着形参顺序传。
 */
int font_sd_fseek(RESFILE *fp, u8 seek_mode, u32 offset)
{
    return resfile_seek((RESFILE *)fp, offset, seek_mode);
}

/*
 * @note 原库在这里对同一个指针先 resfile_close() 再 fclose(), 并返回 fclose
 *       的结果。原框架特意保留了那次多余的 fclose, 理由是"resfile_* 闭源,
 *       无从确认 resfile_close 是否已关掉底层句柄"。
 *
 *       【本移植已去掉那次 fclose】: resfile_* 现在由 port/ui_port_fs_fatfs.c
 *       实现, 不再是黑盒 —— resfile_close() 已经 f_close 并释放了句柄池槽位。
 *       再调一次 stdio 的 fclose() 会把"指向句柄池结构体的指针"当 FILE* 用,
 *       是明确的未定义行为(且返回值也不再有意义)。歧义消失, 保留反而是错的。
 */
int font_sd_fclose(RESFILE *fp)
{
    return resfile_close(fp);
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [保留] 1 —— font_sd_fclose 对同一句柄连着调 resfile_close() 和 fclose()。
 *                【这条特意不改】: resfile_* 是闭源的, 无从确认 resfile_close
 *                是否已经把底层 RESFILE 一并关掉。
 *                  · 若它已经关了     -> 那第二次 fclose 是 double free;
 *                  · 若它只管上面一层 -> 去掉 fclose 就会【泄漏句柄】。
 *                原库这么写且实机长期运行没暴露问题, 说明当前组合是可工作的。
 *                在拿到 resfile 层实现之前, 贸然去掉一次比留着更危险。
 *                (同类判断: res_fseek 的返回值也是故意不判, 见 res/ascii.c。)
 *   [保留] 2 —— font_sd_fopen 的形参 arg 未使用。它是为了匹配 font 模块的
 *                fopen 回调原型, 本来就该留着。
 *
 * 1) font_sd_fclose 对同一个句柄连续调用 resfile_close() 和 fclose():
 *      resfile_close((RESFILE *)fp);
 *      return fclose(fp);
 *    整个文件里 RESFILE* 与 RESFILE* 是同一个指针(IR 里只是 bitcast, 见上),
 *    而 font_sd_fopen 只经由 resfile_open() 拿到它 —— 从没经过 fopen()。
 *    所以这里是"用 resfile 层关掉之后, 再拿同一个(很可能已释放的)指针交给
 *    vfs 层的 fclose 关一次", 且**函数返回值取的是 fclose 的结果**,
 *    resfile_close 的失败被丢弃。
 *    参考 IR 确认原库就是这个顺序(两次 tail call, 返回 %call1 = fclose 的结果),
 *    不是还原笔误。
 *
 * 2) font_sd_fopen 的形参 arg 从未使用(IR 里带 nocapture readnone)。
 *    它存在只是为了匹配 font 模块的 fopen 回调原型, 无需改动。
 */
