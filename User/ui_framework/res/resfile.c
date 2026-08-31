/*
 * resfile.c —— 资源文件(.sty / .res / .str)的读取与解码入口
 *
 * 【来源】从 cpu/br27/liba/res.a 的 resfile.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/resfile.ll
 *     原始路径: btsdk/lib/utils/ui/resource/resfile.c
 *
 * 【谁在用】res.a 里 UI 依赖最重的一块。驱动层 ui_resources_manager.c /
 *   ui_synthesis_oled.c 用 open_resfile / open_image_by_id / br23_read_image_data
 *   / res_f* 一族; 已还原的框架代码 ui_draw/image_process.c 用
 *   br23_read_image_data / read_palette / select_resfile。
 *
 * 【无需行号锁定】本模块【没有任何 ASSERT】(declare 区无 cpu_assert),
 *   也没有 __FILE__ 字符串, 因此不存在 __LINE__ 依赖, 与 quicklz.c 一样自然书写。
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

/*
 * 原厂构建时 res_ver.h 里 STRING_VERION 的取值是 0xa2f21442 —— 它被硬编进了
 * 库里(还原阶段必须照抄才能与库等价)。加固时 str_file_version_compare 已改为
 * 使用形参 str_ver, 于是这个常量【再无任何代码引用】, 就地删掉, 免得后人误以为
 * 版本校验还锁在原厂那一版上。取值本身作为考古信息留在文末 TODO 9。
 */

/* 资源文件头的魔数 "RU21" */
#define RES_MAGIC_0     'R'
#define RES_MAGIC_1     'U'
#define RES_MAGIC_2     '2'
#define RES_MAGIC_3     '1'

static int g_language_id = 1;
/*
 * 加固: 原库四处读盘重试都写成
 *     int retry = 3;  do { ... } while (retry-- > 0);
 * 而 retry-- 是【后置自减】, 3、2、1、0 各判一次, 【实际循环 4 次】——
 * 字面与实际不符, 看代码的人会以为是 3 次。
 * 改成显式的"总共尝试 N 次"写法, 【次数保持 4 不变】: 减少一次重试对读盘
 * 容错是净损失, 不是修复。
 */
#define RES_READ_MAX_TRY    4

/*
 * 加固: 原库把"版本已校验过"的标志写成 res_file_version_compare /
 * str_file_version_compare 函数内的 static 局部变量, 一旦置位就【永不复位】——
 * 换了资源文件(open_resfile / open_str_file 打开另一个)之后不会重新校验版本,
 * 拿旧的校验结论继续用。提到文件级, 由这两个 open 成功时清掉。
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

    /* 原厂是"大于上界就跳出"(IR 为 icmp ugt 7), 不是 "<= 上界"(那会编成
     * icmp ult 8 并把两个分支目标对调)。极性必须照抄。 */
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

    /* 原厂是"大于上界就跳出"(IR 为 icmp ugt 7), 不是 "<= 上界"(那会编成
     * icmp ult 8 并把两个分支目标对调)。极性必须照抄。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->file) {
                return info->file;
            }

            /* 与 ui_load_res/str_by_pj_id 不同, 这里几个出口在原厂里【不汇合】,
             * 各自 return; 而且外层没有 else 包裹 —— 块排布必须照抄。 */
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

    /* 原厂是"大于上界就跳出"(IR 为 icmp ugt 7), 不是 "<= 上界"(那会编成
     * icmp ult 8 并把两个分支目标对调)。极性必须照抄。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->res) {
                return info->res;
            }

            /* 下面三个中间出口在原厂里【汇合到同一条 return】—— IR 里
             * info->res 只有一次 load。各写一条 return 会多出两次 load,
             * memcheck 会按 README 5.3.2 报"访问集合不同"。 */
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

    /* 原厂是"大于上界就跳出"(IR 为 icmp ugt 7), 不是 "<= 上界"(那会编成
     * icmp ult 8 并把两个分支目标对调)。极性必须照抄。 */
    for (info = ui_load_info_table; ; info++) {
        if (info->pj_id > PJ_ID_MAX) {
            break;
        }

        if (info->pj_id == pj_id) {
            if (info->str) {
                return info->str;
            }

            /* 下面三个中间出口在原厂里【汇合到同一条 return】—— IR 里
             * info->str 只有一次 load。各写一条 return 会多出两次 load,
             * memcheck 会按 README 5.3.2 报"访问集合不同"。 */
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
     * 加固: 原库按 JLUI_TYPE_AND_VERSION 的 bit0 分出 if / else 两支, 而
     * 【两支的魔数校验逐字相同】(都比 RES_MAGIC_0..3) —— 复制粘贴后忘了改。
     * 本该是两套魔数, 但正确的第二套取值无从考证, 所以这里只把冗余分支合并,
     * 不臆造魔数。bit0 若真要区分, 需拿到打包工具的约定后另行补。
     *
     * 加固: 校验失败时原库【只 return, 不关句柄也不置 NULL】, 留下一个开着的
     * res_file1, 且 res_file 仍指向上一次的旧句柄。这里与读头失败那一支对齐,
     * 统一关掉并置 NULL。
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

    /* 加固: 换了资源文件, 之前那次版本校验的结论就不作数了, 清掉标志
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
        /* 加固: 原库关掉句柄后【没有置回 NULL】(open_resfile 的同一处是置了的),
         * 留下一个已关闭的悬空句柄; 下次进来开头的 close_str_file() 判到它非空,
         * 就会对同一个句柄二次 resfile_close。 */
        resfile_close(str_file1);
        str_file1 = NULL;
        return -EFAULT;
    }

    /*
     * 加固: 同 open_resfile —— 原库按 JLUI_TYPE_AND_VERSION 的 bit0 分出的
     * if / else 两支, 魔数校验逐字相同(都比 RES_MAGIC_0..3), 是复制粘贴后
     * 忘了改。这里合并成一支, 不臆造第二套魔数。
     * 失败时一并关掉句柄并置 NULL, 与读头失败那一支保持一致。
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

    /* 加固: 同 open_resfile —— 换了字符串资源文件就该重新校验版本。 */
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
         * 加固: 原库【完全忽略形参 str_ver】, 比对的是编译期常量 STRING_VERION
         * —— 还原自原厂构建时的取值。同文件的 res_file_version_compare 用的
         * 却是形参, 两者不对称, 显然是漏改。这里改为使用形参。
         * 注: 本函数目前全工程【零调用者】, 所以此改动不影响现有行为。
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
         * 加固: 原库这四次 res_fread 的返回值全被丢弃。最后一次(res_pic)有
         * 末尾的 CRC16 兜着 —— 读失败时栈上的垃圾几乎必然校验不过, 会走重试;
         * 但前三次没有任何兜底: head 读失败会把垃圾写进 f->version,
         * page / entry 读失败则拿垃圾当偏移去 seek。
         *
         * 读失败一律 continue【走原有的重试】而不是直接 return: 这里的重试
         * 本来就是靠末尾 CRC 失败驱动的, 直接 return 等于把重试废掉,
         * 而 flash 偶发读错正是重试要救的场景。
         *
         * @note res_fseek 的返回值故意不判, 理由见 ascii.c ——
         *       resfile_seek 是闭源的, 成功语义无从确认; 定位失败会由紧接着
         *       的 res_fread 读不满暴露出来。
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

    /* 加固: 原库不判 str_file 就往下走(同文件 str_file_version_compare 是判了的)。 */
    if (str_file == NULL) {
        return -EINVAL;
    }

    do {
        /* 加固: 原库丢弃返回值。读失败 continue 走重试, 理由同 open_image_by_id。 */
        res_fseek(str_file, sizeof(RES_HEAD_T), SEEK_SET);
        if (res_fread(str_file, (u8 *)&res_entry, sizeof(res_entry)) != sizeof(res_entry)) {
            continue;
        }

        /*
         * 加固【本函数的硬伤】: 下面 tmp 的计算里有
         *     res_entry.wCount / res_entry.langsum
         * 而 langsum 直接来自读盘数据 —— 上面那次读失败时它是栈垃圾, 资源文件
         * 损坏时它也可能就是 0, 于是整数除零。这里先挡住, 当作坏数据走重试。
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
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍然照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在
 * cpu/br27/tools/ui_reimpl/accept/resfile.txt 并锁定指纹)。
 *
 *   [已修]  1 —— 两支相同的魔数校验已合并为一支(不臆造第二套魔数, 正确取值
 *                 无从考证); 校验失败时补了 resfile_close + 置 NULL。
 *   [已修]  9 —— str_file_version_compare 改为使用形参 str_ver; 本地那个
 *                 #define STRING_VERION 0xa2f21442 随之删除(已无引用)。
 *                 该函数目前全工程零调用者, 改动不影响现有行为。
 *   [已修] 10 —— open_str_file 读头失败后置 str_file1 = NULL, 不再留悬空句柄,
 *                 也就不会被下次的 close_str_file() 二次 close。
 *
 *   [已修]  2 —— res_fread 的返回值。先用最终固件符号表把这"十几处"的死活
 *                 分开了(方法见 README 3.3, 并跑了活函数对照组):
 *                   活: open_image_by_id(11)、open_string_pic(12) —— 已全部补判;
 *                   死: read_palette、load_pallet_table、res_get_picture_number
 *                       (固件符号数均为 0) —— 不动。
 *                 读失败一律 continue【走原有重试】而不是直接 return: 这里的
 *                 重试本就靠末尾 CRC 失败驱动, 直接 return 等于废掉重试,
 *                 而 flash 偶发读错正是重试要救的场景。
 *                 res_fseek 的返回值仍【故意不判】, 理由见 ascii.c。
 *   [已修]  4 —— open_string_pic 的 langsum 除零已挡住(当作坏数据走重试)。
 *                 langsum 直接来自读盘数据, 读失败时是栈垃圾、文件损坏时也
 *                 可能就是 0 —— 这是本文件唯一会【直接触发异常】的一条。
 *
 *   [已修]  3 —— retry 初值 3 而实际循环 4 次(retry-- 是【后置自减】,
 *                 3、2、1、0 各判一次), 字面与实际不符, 看代码的人会以为是 3 次。
 *                 已把四处统一改成
 *                     int try_cnt = RES_READ_MAX_TRY;
 *                     do { ... } while (--try_cnt > 0);
 *                 【次数保持 4 不变】—— 把它改成 3 次等于少一次重试,
 *                 对读盘容错是净损失, 那不叫修复。
 *   [保留]  5 —— 两处读调色板都不校验调用方缓冲区大小。read_palette 与
 *                 load_pallet_table 在最终固件里符号数均为 0, 是死代码。
 *   [保留]  6 —— rle_decode 越界时 return 0。同样是死代码(固件符号数 0);
 *                 注意它与 rle.c 的 Rle_Decode(37, 活)不是同一套。
 *   [保留]  7 —— quicklz_decode 的两个长度形参完全没用。整条 quicklz 链
 *                 (image_decode -> quicklz_decode -> qlz_decompress)固件符号数
 *                 全为 0, 见 quicklz.c 开头。
 *   [已修]  8 —— checked 标志一旦置位永不复位, 换资源文件后不再校验版本
 *                 (它原本是两个函数各自的 static 局部变量, 外部无从清除)。
 *                 -> 提到文件级(res_ver_checked / str_ver_checked), 由
 *                    open_resfile / open_str_file 成功打开新文件时清掉。
 *
 *
 * 1) 【open_resfile / open_str_file 的 if-else 两支完全相同】。
 *    JLUI_TYPE_AND_VERSION 的 bit0 把魔数校验分成两条路径, 但两边比的是同一组
 *    常量 'R''U''2''1' —— 显然是复制粘贴后忘了改新版魔数。IR 里两个分支的
 *    指令逐条相同, 说明原厂就是这样, 不是还原引入的。
 *
 * 2) 【几乎所有 res_fseek / res_fread 的返回值都被丢弃】。
 *    open_image_by_id / read_palette / load_pallet_table / open_string_pic /
 *    res_get_picture_number 里的十几次读盘全部不判返回值, 读失败时后续拿着
 *    栈上未初始化的 RES_*_T 继续算偏移, 会 seek 到任意位置。
 *
 * 3) 【重试次数比字面多一次】。do { ... } while (retry-- > 0) 且 retry 初值为 3,
 *    实际循环 4 次(3、2、1、0 各判一次)。read_image_data / read_str_data /
 *    open_image_by_id / open_string_pic 都是这个模式。
 *
 * 4) open_string_pic 用 res_entry.wCount / res_entry.langsum 做除法,
 *    【不判 langsum 是否为 0】—— 资源文件损坏时会直接除零。
 *
 * 5) read_palette 与 load_pallet_table 读调色板时, 前者写死 512 字节,
 *    后者用 pal.dwLength; 两者都不校验调用方给的缓冲区够不够大。
 *
 * 6) rle_decode 的重复计数取自 pSour[i-1] - 0xC0, 而判断用的是 > 0xC0,
 *    所以计数最小为 1; 但它【不校验 at + count 是否越过 DestLen】, 只在每写一个
 *    字节后判一次, 判到越界就直接 return 0 —— 返回值 0 与"源长度为 0"无法区分。
 *
 * 7) quicklz_decode 的 SourLen / DestLen 两个形参【完全没用】(IR 里已被优化成
 *    undef), 长度全靠压缩流头部自述 —— 见 quicklz.c 的 TODO 1。
 *
 * 8) res_file_version_compare / str_file_version_compare 的 checked 标志一旦置位
 *    就永不复位, 换资源文件后不会重新校验版本。
 *
 * 9) 【str_file_version_compare 的形参 str_ver 被完全忽略】。它比对的是编译期
 *    常量 STRING_VERION(0xa2f21442, 原厂构建时 res_ver.h 里的值), 而不是调用方
 *    传进来的 str_ver —— 连打印用的也是那个常量。同一文件里的
 *    res_file_version_compare 用的却是形参 res_ver, 两者不对称, 显然是漏改。
 *    后果: 该函数实际锁死在原厂那一版字符串资源上, 调用方传什么都不起作用;
 *    而本工程 res_ver.h 的 STRING_VERION 已经是 0x0c1b27ae, 对不上。
 *    (还原时必须照抄 0xa2f21442 才能与库等价, 改正属于行为变更, 单独提。)
 *
 * 10) 【open_str_file 的读头失败路径不把 str_file1 置回 NULL】。
 *    它 resfile_close(str_file1) 之后就直接 return, 留下一个已关闭的悬空句柄;
 *    下次再调 open_str_file 时开头的 close_str_file() 会对这个悬空句柄
 *    再 close 一次(double close)。open_resfile 的同一处是置了 NULL 的,
 *    又一处两支不对称。
 */
