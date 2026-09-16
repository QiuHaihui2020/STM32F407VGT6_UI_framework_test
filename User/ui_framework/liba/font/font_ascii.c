/*
 * font_ascii.c —— ASCII 字模文件(0x00~0x7F)的索引与取模
 *
 * 【字模文件布局】
 *     偏移 0      : 1 字节, 字高(点数) —— InitFont_ASCII 读走
 *     偏移 2+4*n  : 第 n 个 ASCII 字符的索引项 ASCSTRUCT(width/size/addr),
 *                   sizeof=4, 定义见 font/font_all.h
 *     addr        : 该字符点阵数据在文件里的偏移, 【大端】存放, 用 font_ntoh 转
 *   本模块无 ASSERT。
 *
 * 【段属性】代码放在 .font_ascii.text。三个字符串常量【不单独设段】,
 *   所以不开 const_seg。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".font_ascii.text")
#endif

#include "jl_typedef.h"
#include "font/font_all.h"
#include "jl_debug.h"    /* printf / puts: 显式包含, 保证本文件自包含 */

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

    /* 读不到字高就把文件关掉并如实报错: 若放过这次失败, info->ascpixel.size
     * 会保持旧值(首次调用就是未初始化内存), 而函数还返回 1 表示成功 ——
     * 此后整套 nbytes 计算全建立在垃圾值上。 */
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

    /* 索引表项读不全就返回 0(取不到): ascinfo 是栈上变量, 未读满时里面是
     * 垃圾, 后面拿它的 width 算 nbytes、拿它的 addr 去 fseek 会一路错到底。 */
    if (font_sd_fread(info->ascpixel.file.fd, &ascinfo, sizeof(ASCSTRUCT)) != sizeof(ASCSTRUCT)) {
        return 0;
    }

    nbytes = ((info->ascpixel.size + 7) / 8) * ascinfo.width;

    if (info->ascpixel.pixelbuf == NULL) {
        return ascinfo.width;
    }

    if (nbytes > info->ascpixel.nbytes) {
        /* 注意 puts 本身会补换行, 字符串里不要再带 '\n'。 */
        puts("error:pixelbuf overlay!");
        printf("ascinfo.width = %d, info->ascpixel.size = %d, nbytes = %d, info->ascpixel.nbytes = %d\n",
               ascinfo.width, info->ascpixel.size, nbytes, info->ascpixel.nbytes);
        /*
         * 【分配失败不能更新 nbytes】否则 pixelbuf 是 NULL 而 nbytes 已是新值,
         * 此后每次调用都走上面那个 "pixelbuf == NULL" 分支直接返回宽度 ——
         * 不会立刻崩, 但这个字库从此再也取不出点阵, 而且没有任何提示。
         * 所以失败时保持 nbytes 不变并报一声, 下次调用还会再试一次。
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

    /* 点阵读失败必须返回 0: 此时 pixelbuf 里还是【上一个字】的点阵, 若照常
     * 返回宽度, 界面上就是"显示上一个字", 排查起来很费劲。 */
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

    /* 读失败必须返回 0, 否则会把栈垃圾当宽度返回。 */
    if (font_sd_fread(info->ascpixel.file.fd, &ascinfo, sizeof(ASCSTRUCT)) != sizeof(ASCSTRUCT)) {
        return 0;
    }

    return ascinfo.width;
}

/*
 * 实现注意事项
 *
 *  1) 【点阵缓冲重分配】GetASCIICharacterData 在 nbytes 超过当前缓冲时重新
 *     分配。分配失败时【保持 nbytes 不变】并返回 0 —— 若把 nbytes 更新成新值,
 *     pixelbuf 又是 NULL, 之后每次调用都会走 "pixelbuf == NULL" 分支直接返回
 *     宽度, 这个字库从此再也取不出点阵, 且没有任何提示。
 *
 *  2) 【nbytes 的两种算法】info->ascpixel.nbytes 是 InitFont_ASCII 里按字高算
 *     的, 而每次取模时的 nbytes 是按【本字符的 width】算的。两者共用同一个
 *     字段做上限比较, 所以比字高更宽的字符会走进重分配分支 —— 这是有意的。
 *
 *  3) 【读盘返回值都要判】四处 font_sd_fread 都判了实际长度: 字高读不到会让
 *     整套 nbytes 计算建立在垃圾值上; 索引项读不全会拿栈垃圾去 fseek;
 *     点阵读不全则会把上一个字的点阵当本字显示。
 *
 *  4) 【puts 自带换行】字符串里不要再写 '\n', 否则错误信息会多空一行。
 */
