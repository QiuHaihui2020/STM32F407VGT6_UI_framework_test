/*
 * font_sdfs.c —— 字库文件访问层(把 font 模块的 RESFILE* 接口转接到 resfile_*)
 *
 * 四个函数都是薄封装, 原型见 font/font_sdfs.h。font 模块自己的句柄类型与
 * resfile 层的 RESFILE 在本工程里就是同一个指针, 所以这里直接强转。
 * 本模块无 ASSERT、无全局变量、无字符串常量。
 *
 * 【段属性】代码放在 .font_sdfs.text。
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
 * @note 【只关一次】resfile_close() 已经把底层文件关掉并释放了句柄池槽位
 *       (实现见 liba/res/ui_res_core.c), 所以这里不要再补一次 stdio 的
 *       fclose() —— 那会把"指向句柄池结构体的指针"当 FILE* 用, 是明确的
 *       未定义行为, 返回值也没有意义。
 */
int font_sd_fclose(RESFILE *fp)
{
    return resfile_close(fp);
}

/*
 * 实现注意事项
 *
 *  1) 【句柄只关一次】font_sd_fclose 只调 resfile_close()。本模块拿到的句柄
 *     一律来自 resfile_open(), 从没经过 stdio 的 fopen(), 所以也不该用
 *     fclose() 去关它。
 *
 *  2) 【font_sd_fseek 的实参顺序与形参顺序不同】形参表里 seek_mode 在 offset
 *     之前, 而 resfile_seek 的第三个参数才是 fromwhere —— 别照着形参顺序传。
 *
 *  3) 【font_sd_fopen 的 arg 未使用】它只是为了匹配 font 模块的 fopen 回调
 *     原型, 保留即可。
 */
