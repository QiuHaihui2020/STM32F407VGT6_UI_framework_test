/*
 * resfile.c —— 资源文件(.sty / .res / .str)的读取与解码入口
 *
 * 【谁在用】资源读取里 UI 依赖最重的一块。驱动层 ui_resources_manager.c /
 *   ui_synthesis_oled.c 用 open_resfile / open_image_by_id / br23_read_image_data
 *   / res_f* 一族; 框架侧 liba/ui_draw/image_process.c 用
 *   br23_read_image_data / read_palette / select_resfile。
 *
 * 【无 ASSERT】本模块不用 ASSERT, 也不依赖 __FILE__ / __LINE__。
 *
 * 【资源文件布局】
 *     偏移 0            : RES_HEAD_T   —— magic("RU21") / 版本 / 页数
 *     偏移 16 + n*8     : RES_PAGE_T   —— 第 n 页的页号与页表地址
 *     pageAddr(+12)     : RES_ENTRY_T  —— 该页的条目数与条目表偏移
 *     dwOffset + id*20  : RES_BMP_T / RES_STR_T —— 单张图/单条字符串图的元信息
 *     其中 dwOffset     : 真正的像素数据(可能是 RLE 或 QuickLZ 压缩的)
 *   两级"页 -> 条目"索引, 每次取图都要走 4 次 seek+read。
 *
 * 【段属性】多数函数在 .resfile.text; 与推屏同步相关的十个在 .ui_ram;
 *   全局在 .resfile.data, state_decompress 在 .resfile.data.bss。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma data_seg(".resfile.data")
#pragma bss_seg(".resfile.data.bss")
#pragma code_seg(".resfile.text")
#endif

#include "jl_os_api.h"
#include "res/resfile.h"
#include "jl_crc.h"

typedef struct {
    u8  magic[4];
    u16 version;
    u16 bPanelType;
    u16 totalPage;
    u16 reserved;
    u32 resver;
} RES_HEAD_T;

typedef struct {
    u32 pageNum;
    u32 pageAddr;
} RES_PAGE_T;

typedef struct {
    u32 dwOffset;
    u16 wCount;
    u8  bItemType;
    u8  langsum;
    u32 language;
} RES_ENTRY_T;

typedef struct {
    u32 num;
    u32 dwOffset;
    u32 dwLength;
} RES_PAL_T;

typedef struct {
    u16 head_crc;
    u16 data_crc;
    u16 res_type;
    u16 typeId;
    u16 wWidth;
    u16 wHeight;
    u32 dwLength;
    u32 dwOffset;
} RES_BMP_T;

typedef struct {
    u16 head_crc;
    u16 data_crc;
    u16 res_type;
    u16 type_id;
    u16 wWidth;
    u16 wHeight;
    u32 dwLength;
    u32 dwOffset;
} RES_STR_T;

typedef struct {
    u32 stream_counter;
} qlz_state_decompress;

extern size_t qlz_decompress(const char *source, void *destination, qlz_state_decompress *state);
extern struct ui_load_info ui_load_info_table[];
extern int norflash_hardware_read_watch(u8 *buf, u32 addr, u32 len, u8 wait);

/* pj_id 是 3 位字段(取自 (x >> 29) & 0x7), 所以 > 7 即表末哨兵 {-1,...}。 */
#define PJ_ID_MAX       7

/* 资源文件头的魔数 "RU21" */
#define RES_MAGIC_0     'R'
#define RES_MAGIC_1     'U'
#define RES_MAGIC_2     '2'
#define RES_MAGIC_3     '1'

static int g_language_id = 1;
/*
 * 读盘重试次数。四处重试统一写成
 *     int try_cnt = RES_READ_MAX_TRY;  do { ... } while (--try_cnt > 0);
 * 这种"总共尝试 N 次"的形式 —— 字面次数与实际一致, 避免用
 * `while (retry-- > 0)` 那种后置自减写法(初值 3 实际会循环 4 次)。
 */
#define RES_READ_MAX_TRY    4

/*
 * "版本已校验过"的标志放在【文件级】而不是校验函数内的 static 局部 ——
 * 换了资源文件(open_resfile / open_str_file 打开另一个)之后必须重新校验,
 * 而函数内的 static 一旦置位外部就无从清除。这两个标志由对应的 open
 * 成功时清掉。
 */
static bool res_ver_checked = false;
static bool str_ver_checked = false;

static RESFILE *res_file1 = NULL;

int ui_language_set(int language)
{
    g_language_id = language;

    return language;
}

int ui_language_get()
{
    return g_language_id;
}

void close_resfile()
{
    if (res_file1) {
        resfile_close(res_file1);
        res_file1 = NULL;
    }
}

/*
 * @brief 给某个 pj_id 换一条 .sty 路径, 并把它已经打开的三个句柄全部关掉
 * @return 路径没变返回 1; 换成功后继续遍历, 遍历完返回 -1
 */
int ui_set_sty_path_by_pj_id(int pj_id, const u8 *path)
{
    struct ui_load_info *info;

    /* pj_id 是 3 位字段, 取值大于 PJ_ID_MAX 的那一项就是表末哨兵 {-1,...},
     * 所以用"大于上界就跳出"作为遍历终止条件。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (path && info->path) {
                if (strcmp(info->path, (const char *)path) == 0) {
                    return 1;
                }
            }

            if (info->file) {
                res_fclose(info->file);
                info->file = NULL;
            }
            if (info->res) {
                res_fclose(info->res);
                info->res = NULL;
            }
            if (info->str) {
                res_fclose(info->str);
                info->str = NULL;
            }

            info->path = (const char *)path;
        }
    }

    return -1;
}

/*
 * @brief 按 pj_id 打开 .sty 并顺带打开配套的 .res, 返回 .sty 句柄
 * @note 路径是 "xxx.sty", 换后缀的做法是 zalloc(128) 拷贝一份再把末 4 字节
 *       覆盖成 ".res"(连 '\0' 共 5 字节)。
 */
void *ui_load_sty_by_pj_id(int pj_id)
{
    struct ui_load_info *info;

    /* 哨兵判断同 ui_set_sty_path_by_pj_id。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->file) {
                return info->file;
            }

            /* 与 ui_load_res/str_by_pj_id 不同, 这里各个出口不共用一条
             * return, 每种情况各自返回, 读起来更直接。 */
            if (info->path == NULL) {
                return info->file;
            }

            info->file = res_fopen(info->path, "r");
            printf("open path  <<<<< %s >>>> \n", info->path);
            if (info->file == NULL) {
                return info->file;
            }

            if (info->res == NULL) {
                char *name = zalloc(128);

                strcpy(name, info->path);
                memcpy(name + strlen(name) - 4, ".res", 5);
                info->res = res_fopen(name, "r");
                free(name);

                if (info->res == NULL) {
                    printf("find res fail  <<<<< %s >>>> \n", __FUNCTION__);
                    return NULL;
                }

                printf("find res succ  <<<<< %s >>>> \n", __FUNCTION__);
                return info->file;
            }

            return info->file;
        }
    }

    return NULL;
}

void *ui_load_res_by_pj_id(int pj_id)
{
    struct ui_load_info *info;

    /* 哨兵判断同 ui_set_sty_path_by_pj_id。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->res) {
                return info->res;
            }

            /* 下面三个中间出口都 goto 到同一条 return, 这样 info->res
             * 只读一次; 各写一条 return 会多出两次读。 */
            if (info->file == NULL) {
                if (info->path == NULL) {
                    goto __exit;
                }

                info->file = res_fopen(info->path, "r");
                printf("open path  <<<<< %s >>>> \n", info->path);
                if (info->file == NULL) {
                    goto __exit;
                }

                if (info->res) {
                    goto __exit;
                }
            }

            {
                char *name = zalloc(128);

                strcpy(name, info->path);
                memcpy(name + strlen(name) - 4, ".res", 5);
                info->res = res_fopen(name, "r");
                free(name);

                if (info->res == NULL) {
                    printf("find res fail  <<<<< %s >>>> \n", __FUNCTION__);
                    return NULL;
                }

                printf("find res succ  <<<<< %s >>>> \n", __FUNCTION__);
            }

__exit:
            return info->res;
        }
    }

    return NULL;
}

void *ui_load_str_by_pj_id(int pj_id)
{
    struct ui_load_info *info;

    /* 哨兵判断同 ui_set_sty_path_by_pj_id。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->str) {
                return info->str;
            }

            /* 下面三个中间出口都 goto 到同一条 return, 这样 info->str
             * 只读一次; 各写一条 return 会多出两次读。 */
            if (info->file == NULL) {
                if (info->path == NULL) {
                    goto __exit;
                }

                info->file = res_fopen(info->path, "r");
                printf("open path  <<<<< %s >>>> \n", info->path);
                if (info->file == NULL) {
                    goto __exit;
                }

                if (info->str) {
                    goto __exit;
                }
            }

            {
                char *name = zalloc(128);

                strcpy(name, info->path);
                memcpy(name + strlen(name) - 4, ".str", 5);
                info->str = res_fopen(name, "r");
                free(name);

                if (info->str == NULL) {
                    printf("find str fail  <<<<< %s >>>> \n", __FUNCTION__);
                    return NULL;
                }

                printf("find str succ  <<<<< %s >>>> \n", __FUNCTION__);
            }

__exit:
            return info->str;
        }
    }

    return NULL;
}

static RESFILE *res_file = NULL;
static RESFILE *str_file = NULL;
static RESFILE *str_file1 = NULL;

void select_resfile(u8 index)
{
    if (index) {
        res_file = ui_load_res_by_pj_id(index);
    } else {
        res_file = res_file1;
    }
}

void select_strfile(u8 index)
{
    if (index) {
        str_file = ui_load_str_by_pj_id(index);
    } else {
        str_file = str_file1;
    }
}

int open_resfile(const char *name)
{
    RES_HEAD_T head;

    close_resfile();

    res_file1 = resfile_open(name);
    if (res_file1 == NULL) {
        printf("open_resfile fail!\n");
        return -EINVAL;
    }

    if (resfile_read(res_file1, (u8 *)&head, sizeof(head)) != sizeof(head)) {
        resfile_close(res_file1);
        res_file1 = NULL;
        return -EFAULT;
    }

    /*
     * JLUI_TYPE_AND_VERSION 的 bit0 用来区分两种资源版本, 两版的头部魔数
     * 本可以不同; 目前只确定了 "RU21" 这一套, 所以这里只做这一套校验 ——
     * 需要区分第二套时, 得先拿到打包工具那边的约定再补。
     *
     * 校验失败必须【关掉句柄并置 NULL】, 与读头失败那一支保持一致:
     * 否则会留下一个开着的 res_file1, 而 res_file 还指向上一次的旧句柄。
     */
    if (JLUI_TYPE_AND_VERSION & 0x10) {
        if (head.magic[0] != RES_MAGIC_0 || head.magic[1] != RES_MAGIC_1
            || head.magic[2] != RES_MAGIC_2 || head.magic[3] != RES_MAGIC_3) {
            puts("-------------resfile_err\n");
            resfile_close(res_file1);
            res_file1 = NULL;
            return -EINVAL;
        }
    }

    res_file = res_file1;

    /* 换了资源文件, 之前那次版本校验的结论就不作数了, 清掉标志
     * 让 res_file_version_compare 下次重新校验。 */
    res_ver_checked = false;

    return 0;
}

int res_file_version_compare(int res_ver)
{
    RES_HEAD_T head;

    if (!res_ver_checked) {
        if (res_file == NULL) {
            puts("res_file null!\n");
            return -EINVAL;
        }

        res_fseek(res_file, 0, SEEK_SET);
        if (res_fread(res_file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
            puts("res_file head len err!\n");
            return -EFAULT;
        }

        printf("IMAGE_VERION:0x%x, head.resver: 0x%x .\n", res_ver, head.resver);
        if (head.resver != res_ver) {
            printf("IMAGE_VERION:0x%x, head.resver: 0x%x .err!\n", res_ver, head.resver);
            return -EINVAL;
        }

        str_ver_checked = true;
    }

    return 0;
}

void close_str_file()
{
    if (str_file1) {
        resfile_close(str_file1);
        str_file1 = NULL;
    }
}

int open_str_file(const char *name)
{
    RES_HEAD_T head;

    close_str_file();

    str_file1 = resfile_open(name);
    if (str_file1 == NULL) {
        printf("open_str_file fail.\n");
        return -EINVAL;
    }

    if (resfile_read(str_file1, (u8 *)&head, sizeof(head)) != sizeof(head)) {
        /* 关掉之后必须置回 NULL: 否则留下一个已关闭的悬空句柄, 下次进来
         * 开头的 close_str_file() 判到它非空, 会对同一句柄二次 close。 */
        resfile_close(str_file1);
        str_file1 = NULL;
        return -EFAULT;
    }

    /*
     * 魔数校验同 open_resfile: 只校验已确定的那一套, 失败时一并关掉句柄
     * 并置 NULL, 与读头失败那一支保持一致。
     */
    if (JLUI_TYPE_AND_VERSION & 0x10) {
        if (head.magic[0] != RES_MAGIC_0 || head.magic[1] != RES_MAGIC_1
            || head.magic[2] != RES_MAGIC_2 || head.magic[3] != RES_MAGIC_3) {
            puts("-------------resfile_err\n");
            resfile_close(str_file1);
            str_file1 = NULL;
            return -EINVAL;
        }
    }

    str_file = str_file1;

    /* 同 open_resfile —— 换了字符串资源文件就该重新校验版本。 */
    str_ver_checked = false;

    return 0;
}

int str_file_version_compare(int str_ver)
{
    RES_HEAD_T head;

    if (!str_ver_checked) {
        if (str_file == NULL) {
            puts("str_file null!\n");
            return -EINVAL;
        }

        res_fseek(str_file, 0, SEEK_SET);
        if (res_fread(str_file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
            puts("str_file head len err!\n");
            return -EFAULT;
        }

        /*
         * 版本比对用【形参 str_ver】而不是写死的常量 —— 与同文件的
         * res_file_version_compare 保持对称, 换一版字符串资源只需调用方传新值。
         * 注: 本函数目前全工程零调用者。
         */
        printf("str_ver:0x%x, head.resver: 0x%x .\n", str_ver, head.resver);
        if (head.resver != str_ver) {
            printf("str_ver:0x%x, head.resver: 0x%x .err!\n", str_ver, head.resver);
            return -EINVAL;
        }

        res_ver_checked = true;
    }

    return 0;
}

/*
 * @brief 按 id 从资源文件里取出一张图的元信息(不含像素数据)
 * @note 头部 CRC 对不上就重试, 总共尝试 RES_READ_MAX_TRY(4) 次。
 */
AT_UI_RAM
int open_image_by_id(RESFILE *specfile, struct image_file *f, int id, int page_num)
{
    RES_HEAD_T head;
    RES_ENTRY_T entry;
    RES_BMP_T res_pic;
    RES_PAGE_T page;
    RESFILE *file = specfile ? specfile : res_file;
    int try_cnt = RES_READ_MAX_TRY;

    if (file == NULL) {
        return -EINVAL;
    }

    do {
        /*
         * 四次 res_fread 的返回值都要判: 最后一次(res_pic)虽有末尾的 CRC16
         * 兜着(读失败时栈上的垃圾几乎必然校验不过), 但前三次没有任何兜底 ——
         * head 读失败会把垃圾写进 f->version, page / entry 读失败则拿垃圾
         * 当偏移去 seek。
         *
         * 读失败一律 continue【走重试】而不是直接 return: 本函数的重试就是
         * 靠末尾 CRC 失败驱动的, 直接 return 等于把重试废掉, 而 flash 偶发
         * 读错正是重试要救的场景。
         *
         * @note res_fseek 的返回值故意不判: resfile_seek 在各后端下"成功返回 0
         *       还是返回新偏移"并不统一; 定位失败会由紧接着的 res_fread
         *       读不满暴露出来。
         */
        res_fseek(file, 0, SEEK_SET);
        if (res_fread(file, (u8 *)&head, sizeof(head)) != sizeof(head)) {
            continue;
        }
        f->version = (head.magic[2] << 8) | head.magic[3];

        res_fseek(file, sizeof(head) + page_num * sizeof(RES_PAGE_T), SEEK_SET);
        if (res_fread(file, (u8 *)&page, sizeof(page)) != sizeof(page)) {
            continue;
        }

        res_fseek(file, page.pageAddr + 12, SEEK_SET);
        if (res_fread(file, (u8 *)&entry, sizeof(entry)) != sizeof(entry)) {
            continue;
        }
        if (entry.wCount < id) {
            return -EINVAL;
        }

        res_fseek(file, entry.dwOffset + (id - 1) * sizeof(RES_BMP_T), SEEK_SET);
        if (res_fread(file, (u8 *)&res_pic, sizeof(res_pic)) != sizeof(res_pic)) {
            continue;
        }

        if (CRC16((u8 *)&res_pic.data_crc, sizeof(res_pic) - 2) == res_pic.head_crc) {
            f->width = res_pic.wWidth;
            f->height = res_pic.wHeight;
            f->format = (res_pic.typeId >> 10) & 0x07;
            f->compress = res_pic.typeId >> 13;
            f->id = res_pic.typeId & 0x3ff;
            f->offset = res_pic.dwOffset;
            f->len = res_pic.dwLength;
            f->data_crc = res_pic.data_crc;

            return 0;
        }
    } while (--try_cnt > 0);

    return -EINVAL;
}

int read_palette(int prj_id, RESFILE *specfile, struct image_file *f, u8 *pal, int page_num)
{
    RES_ENTRY_T entry;
    RES_PAL_T res_pal;
    RES_PAGE_T page;
    RESFILE *file;

    select_resfile(prj_id);

    file = specfile ? specfile : res_file;
    if (file == NULL) {
        return -EINVAL;
    }

    res_fseek(file, sizeof(RES_HEAD_T) + page_num * sizeof(RES_PAGE_T), SEEK_SET);
    res_fread(file, (u8 *)&page, sizeof(page));

    res_fseek(file, page.pageAddr, SEEK_SET);
    res_fread(file, (u8 *)&entry, sizeof(entry));

    res_fseek(file, entry.dwOffset, SEEK_SET);
    res_fread(file, (u8 *)&res_pal, sizeof(res_pal));

    res_fseek(file, res_pal.dwOffset, SEEK_SET);
    res_fread(file, pal, 512);

    return 0;
}

int read_image_data(struct image_file *f, u8 *data, int len)
{
    int try_cnt = RES_READ_MAX_TRY;

    do {
        res_fseek(res_file, f->offset, SEEK_SET);
        if (res_fread(res_file, data, len) != len) {
            return -EFAULT;
        }

        if (CRC16(data, len) == f->data_crc) {
            return len;
        }
    } while (--try_cnt > 0);

    return -EFAULT;
}

AT_UI_RAM
int br23_read_image_data(RESFILE *specfile, struct image_file *f, u8 *data, int len, int offset)
{
    RESFILE *file = specfile ? specfile : res_file;

    res_fseek(file, f->offset + offset, SEEK_SET);
    if (res_fread(file, data, len) != len) {
        return -EFAULT;
    }

    return len;
}

AT_UI_RAM
int br25_read_image_data(RESFILE *specfile, struct image_file *f, u8 *data, int len, int offset)
{
    RESFILE *file = specfile ? specfile : res_file;

    res_fseek(file, f->offset + offset, SEEK_SET);
    if (res_fread(file, data, len) != len) {
        return -EFAULT;
    }

    return len;
}

/*
 * @brief RLE 解码(资源文件自带的简单变体, 与 rle.c 的 Rle_Decode 不是同一套)
 * @note 编码: 字节 > 0xC0 表示"重复", 重复次数 = 该字节 - 0xC0, 值取下一字节;
 *       否则该字节本身就是一个像素。
 */
int rle_decode(const u8 *pSour, u8 *pDest, u32 SourLen, u32 DestLen)
{
    u32 at = 0;
    u32 i, j;
    u32 k;

    if (SourLen == 0) {
        return 0;
    }

    for (i = 0; i < SourLen; i++) {
        /* 先把这个字节存下来: i 马上要自增, 而 pDest 与 pSour 可能重叠,
         * 重读 pSour[i-1] 会多出一次 load, 且 -0xC0 会被算进内层循环。 */
        k = pSour[i];
        if (k > 0xC0) {
            i++;
            if (i >= SourLen) {
                return 0;
            }

            for (j = 0; j < k - 0xC0; j++) {
                pDest[at++] = pSour[i];
                if (at >= DestLen) {
                    return 0;
                }
            }
        } else {
            pDest[at++] = k;
            if (at >= DestLen) {
                return 0;
            }
        }
    }

    return at;
}

static qlz_state_decompress state_decompress;

int quicklz_decode(const u8 *pSour, u8 *pDest, u32 SourLen, u32 DestLen)
{
    return qlz_decompress((const char *)pSour, pDest, &state_decompress);
}

u32 image_decode(const void *pSour, void *pDest, u32 SourLen, u32 DestLen, u8 compress)
{
    switch (compress) {
    case 1:
        return rle_decode((const u8 *)pSour, (u8 *)pDest, SourLen, DestLen);
    case 2:
        return quicklz_decode((const u8 *)pSour, (u8 *)pDest, SourLen, DestLen);
    default:
        memcpy(pDest, pSour, SourLen);
        return SourLen;
    }
}

/*
 * @brief 按 id 取一条"字符串图"的元信息, 并按当前语言选择对应的那一份
 * @note 条目表按语言分块存放: 总条目数 wCount 除以语言数 langsum 得每种语言的
 *       条目数, 再按当前语言在 language 位图里的序号定位到该语言的块。
 */
AT_UI_RAM
int open_string_pic(struct image_file *file, int id)
{
    RES_STR_T res_str;
    RES_ENTRY_T res_entry;
    int try_cnt = RES_READ_MAX_TRY;
    int i, language_index;
    u32 tmp;

    /* 字符串资源没打开时直接返回错误, 不往下走。 */
    if (str_file == NULL) {
        return -EINVAL;
    }

    do {
        /* 读失败 continue 走重试, 理由同 open_image_by_id。 */
        res_fseek(str_file, sizeof(RES_HEAD_T), SEEK_SET);
        if (res_fread(str_file, (u8 *)&res_entry, sizeof(res_entry)) != sizeof(res_entry)) {
            continue;
        }

        /*
         * 下面 tmp 的计算里有 res_entry.wCount / res_entry.langsum, 而 langsum
         * 直接来自读盘数据 —— 上面那次读失败时它是栈垃圾, 资源文件损坏时它也
         * 可能就是 0, 于是整数除零。所以先挡住, 当作坏数据走重试。
         */
        if (res_entry.langsum == 0) {
            continue;
        }

        if ((res_entry.language & (1 << (g_language_id - 1))) == 0) {
            /* 当前语言这份资源里没有, 退回第 1 种语言 */
            g_language_id = 1;
            if ((res_entry.language & 1) == 0) {
                return -ENOENT;
            }
        }

        language_index = 0;
        for (i = 0; i < g_language_id; i++) {
            if (res_entry.language & (1 << i)) {
                language_index++;
            }
        }

        tmp = (res_entry.wCount / res_entry.langsum) * (language_index - 1) + id;
        res_fseek(str_file, res_entry.dwOffset + (tmp - 1) * sizeof(RES_STR_T), SEEK_SET);
        if (res_fread(str_file, (u8 *)&res_str, sizeof(res_str)) != sizeof(res_str)) {
            continue;
        }

        if (CRC16((u8 *)&res_str.data_crc, sizeof(res_str) - 2) == res_str.head_crc) {
            file->format = (res_str.type_id >> 10) & 0x07;
            file->compress = res_str.type_id >> 13;
            file->width = res_str.wWidth;
            file->height = res_str.wHeight;
            file->offset = res_str.dwOffset;
            file->len = res_str.dwLength;
            file->data_crc = res_str.data_crc;

            return 0;
        }
    } while (--try_cnt > 0);

    return -EFAULT;
}

AT_UI_RAM
int read_str_data(struct image_file *f, u8 *data, int len)
{
    int try_cnt = RES_READ_MAX_TRY;

    do {
        res_fseek(str_file, f->offset, SEEK_SET);
        if (res_fread(str_file, data, len) != len) {
            return -EFAULT;
        }

        if (CRC16(data, len) == f->data_crc) {
            return len;
        }
    } while (--try_cnt > 0);

    return -EFAULT;
}

AT_UI_RAM
int br23_read_str_data(struct image_file *f, u8 *data, int len, int offset)
{
    res_fseek(str_file, f->offset + offset, SEEK_SET);
    if (res_fread(str_file, data, len) != len) {
        return -EFAULT;
    }

    return len;
}

AT_UI_RAM
int br25_read_str_data(struct image_file *f, u8 *data, int len, int offset)
{
    res_fseek(str_file, f->offset + offset, SEEK_SET);
    if (res_fread(str_file, data, len) != len) {
        return -EFAULT;
    }

    return len;
}

int load_pallet_table(int id, u32 *data)
{
    RES_PAGE_T page;
    RES_ENTRY_T entry;
    RES_PAL_T pal;

    if (res_file == NULL) {
        return -EINVAL;
    }

    res_fseek(res_file, sizeof(RES_HEAD_T) + id * sizeof(RES_PAGE_T), SEEK_SET);
    res_fread(res_file, (u8 *)&page, sizeof(page));

    res_fseek(res_file, page.pageAddr, SEEK_SET);
    res_fread(res_file, (u8 *)&entry, sizeof(entry));

    res_fseek(res_file, entry.dwOffset, SEEK_SET);
    res_fread(res_file, (u8 *)&pal, sizeof(pal));

    res_fseek(res_file, pal.dwOffset, SEEK_SET);
    res_fread(res_file, (u8 *)data, pal.dwLength);

    return 0;
}

AT_UI_RAM
int _norflash_read_watch(u8 *buf, u32 addr, u32 len, u8 wait)
{
    return norflash_hardware_read_watch(buf, addr, len, wait);
}

AT_UI_RAM
RESFILE *res_fopen(const char *path, const char *mode)
{
    return resfile_open(path);
}

int res_flen(RESFILE *file)
{
    return resfile_get_len(file);
}

AT_UI_RAM
int res_fread(RESFILE *_file, void *buf, u32 len)
{
    return resfile_read(_file, buf, len);
}

AT_UI_RAM
int res_fseek(RESFILE *_file, int offset, int fromwhere)
{
    return resfile_seek(_file, offset, fromwhere);
}

AT_UI_RAM
int res_fclose(RESFILE *file)
{
    return resfile_close(file);
}

int res_get_picture_number(RESFILE *file, int page_num)
{
    RES_ENTRY_T entry;
    RES_PAGE_T page;

    res_fseek(file, sizeof(RES_HEAD_T) + page_num * sizeof(RES_PAGE_T), SEEK_SET);
    res_fread(file, (u8 *)&page, sizeof(page));

    res_fseek(file, page.pageAddr + 12, SEEK_SET);
    res_fread(file, (u8 *)&entry, sizeof(entry));

    return entry.wCount;
}

/*
 * 实现注意事项与已知限制
 *
 *  1) 【资源版本 bit0 只校验一套魔数】JLUI_TYPE_AND_VERSION 的 bit0 本可用来
 *     区分两种资源版本, 两版的头部魔数可以不同; 目前只确定了 "RU21" 这一套,
 *     所以 open_resfile / open_str_file 只做这一套校验。需要区分第二套时,
 *     先拿到打包工具那边的约定再补。
 *
 *  2) 【读盘返回值】活路径上的读盘全部判了返回值: open_image_by_id 四处、
 *     open_string_pic 两处, 读失败一律 continue 走重试。
 *     read_palette / load_pallet_table / res_get_picture_number 在当前配置下是
 *     死代码(最终固件符号表里数不到), 其中的读盘没有判返回值 —— 改回彩屏配置
 *     把它们用起来时, 要一并补上。
 *     res_fseek 的返回值【故意不判】: resfile_seek 在各后端下"成功返回 0 还是
 *     返回新偏移"并不统一, 贸然判断会把成功当失败; 定位失败会由紧接着的
 *     res_fread 读不满暴露出来。
 *
 *  3) 【重试次数】四处重试统一为 RES_READ_MAX_TRY(4) 次, 由末尾的 CRC 校验
 *     失败驱动。flash 偶发读错正是它要救的场景, 不要调小。
 *
 *  4) 【除零防护】open_string_pic 用 res_entry.wCount / res_entry.langsum 定位
 *     语言块, langsum 直接来自读盘数据(读失败是栈垃圾、文件损坏可能为 0),
 *     已在使用前挡住 0 并当作坏数据走重试。这是本文件唯一会直接触发异常的量。
 *
 *  5) 【调色板读取不校验缓冲区】read_palette 写死读 512 字节, load_pallet_table
 *     按 pal.dwLength 读, 两者都不校验调用方给的缓冲区够不够大。这两个函数
 *     当前是死代码; 启用前必须把缓冲区长度加进接口。
 *
 *  6) 【本文件的 rle_decode 与 rle.c 的 Rle_Decode 不是同一套】前者是资源文件
 *     自带的简单变体(字节 > 0xC0 表示重复), 越界时返回 0 —— 调用方无法把它和
 *     "源长度为 0"区分开。当前是死代码。
 *
 *  7) 【quicklz_decode 的两个长度形参没有用上】解压后的长度全靠压缩流头部自述,
 *     调用方必须先用 qlz_size_decompressed 算出长度并按它分配缓冲区。整条
 *     image_decode -> quicklz_decode -> qlz_decompress 链当前是死代码,
 *     见 quicklz.c 开头。
 *
 *  8) 【版本校验标志是文件级的】res_ver_checked / str_ver_checked 由
 *     open_resfile / open_str_file 成功打开新文件时清掉, 所以换资源文件后会
 *     重新校验版本, 不会拿旧结论继续用。
 *
 *  9) 【str_file_version_compare 目前零调用者】它按形参 str_ver 比对头部版本,
 *     与 res_file_version_compare 对称。
 *
 * 10) 【句柄置 NULL】任何关闭句柄的路径都要把对应的静态指针置回 NULL,
 *     否则下次 open 开头的 close_* 会对已关闭的句柄再 close 一次。
 */
