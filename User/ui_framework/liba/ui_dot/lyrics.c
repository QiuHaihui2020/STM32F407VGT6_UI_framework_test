#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".lyrics.data.bss")
#pragma data_seg(".lyrics.data")
#pragma const_seg(".lyrics.text.const")
#pragma code_seg(".lyrics.text")
#endif

#include "ui/lyrics.h"
#include "jl_fs.h"
#include "jl_fs.h"
#include "jl_os_api.h"
#include "jl_debug.h"
#include "ui/ui_text.h"

extern u8 sfc_erase(u32 cmd, u32 addr);
extern u32 sfc_write(u8 *buf, u32 addr, u32 len);
extern int get_utf8_size(u8 c);
extern int utf8_2_unicode_one(u8 *buf, u16 *unicode);
extern int utf8_2_unicode(u8 *buf, u8 len, u8 *out, u8 outlen);

enum _FLASH_ERASER {
    CHIP_ERASER = 0,
    BLOCK_ERASER = 1,
    SECTOR_ERASER = 2,
    PAGE_ERASER = 3,
};

enum {
    LRC_LABEL_OK = 0,
    LRC_BUF_ERR = 65530,
    LRC_LABEL_END = 65530,
    LRC_BUF_OVER = 65531,
};

static u8 lyrics_lib_flag = 1;
LRC_INFO *g_lrc_info = NULL;
static u32 lrc_flash_addr = 0;
static u32 lrc_flash_len = 0;

static u8 lrc_analysis_in_flash(void *lrc_handle, const LRC_FILE_IO *file_io);
static u8 lrc_analysis_in_ram(void *lrc_handle, const LRC_FILE_IO *file_io);
static u8 lrc_data_get(u16 dbtime_s, u8 btime_100ms, LRC_INFO *lrc_info);
static u8 lrc_coding_judge(void);
static u8 lrc_find_row_timelabel(u8 *plabel, u8 *pn);
static u16 lrc_get_data(LRC_FILE *file, u32 faddr, u8 *buf, u16 len);
static int check_and_read(u16 nead_data);
static u16 coding_data_pick(u8 *char_len);
static int lrc_text_get(u16 dbtime_s, u8 btime_100ms, LRC_INFO *lrc_info, TIME_LABEL *lrc_labelbuf, u16 *res_cnt);
static int read_next_lrc(TIME_LABEL lrc_label, LRC_INFO *lrc_info, int *cnt);
static int get_sel_label(u16 dbtime_s, u8 btime_100ms, TIME_LABEL *labelbuf);
static void error_coding_wipe(u32 wfirst, u32 wlast, LRC_INFO *lrc_info);
static int compare_label_time(u16 dbtime_s, u8 btime_100ms, TIME_LABEL lrc_label);

void lyrics_set_firsttime_show(u8 flag)
{
    lyrics_lib_flag = flag;
}

bool lrc_analysis(void *lrc_handle, const LRC_FILE_IO *file_io)
{
    if (g_lrc_info->save_flash) {
        return lrc_analysis_in_flash(lrc_handle, file_io);
    }
    return lrc_analysis_in_ram(lrc_handle, file_io);
}

static u8 lrc_analysis_in_flash(void *lrc_handle, const LRC_FILE_IO *file_io)
{
    TIME_LABEL ptime_label[10];
    TIME_LABEL tmp;
    u8 bn = 1;
    u16 i;

    if (!lrc_handle || !g_lrc_info || !file_io) {
        return 0;
    }

    g_lrc_info->file.hdl = lrc_handle;
    g_lrc_info->file._io = (LRC_FILE_IO *)file_io;
    memset(g_lrc_info->blrc_buf, 0, g_lrc_info->blrc_buf_len);
    g_lrc_info->label_id = 0;
    g_lrc_info->data_len_count = 0;
    g_lrc_info->real_len = 0;
    g_lrc_info->content_len = 0;
    g_lrc_info->lab_info->dblabel_cnt = 0;
    g_lrc_info->sorting->dbnow_fp_addr = 0;

    if (!lrc_coding_judge()) {
        return 0;
    }

    {
        u32 flash_addr = lrc_flash_addr;
        u32 flash_len = lrc_flash_len;

        if (flash_len & 0xFFF) {
            return 0;
        }

        {
            u32 sectors = flash_len >> 12;
            u32 si;
            for (si = 0; si < sectors; si++) {
                if (!sfc_erase(SECTOR_ERASER, flash_addr + (si << 12))) {
                    return 0;
                }
                os_time_dly(1);
            }
        }
    }

    while (lrc_find_row_timelabel((u8 *)ptime_label, &bn)) {
        u16 label_cnt = g_lrc_info->lab_info->dblabel_cnt;
        u16 write_len = (u16)(bn * 8);

        if ((u32)write_len + label_cnt > (lrc_flash_len >> 1)) {
            return 0;
        }

        sfc_write((u8 *)ptime_label, lrc_flash_addr + label_cnt, write_len);
        g_lrc_info->lab_info->dblabel_cnt += bn * 8;
    }

    {
        u16 label_cnt = g_lrc_info->lab_info->dblabel_cnt;
        if (label_cnt == 0) {
            return 0;
        }

        g_lrc_info->lab_info->dblabel_cnt = label_cnt / 8;

        {
            TIME_LABEL *plabel_buf = (TIME_LABEL *)sdfile_flash_addr2cpu_addr(lrc_flash_addr);
            u16 cnt;

            /*
             * @note g_plabel_buf 要在【排序之前】就指向 flash 映射地址:
             *       下面排序途中任何一条 return 0 的失败路径都会直接退出,
             *       若那时它还是上一首歌的旧值(或 NULL), 调用方拿去取歌词
             *       就会读到野指针。排完序后会再指向排好的那一半。
             */
            g_lrc_info->lab_info->g_plabel_buf = plabel_buf;

            cnt = g_lrc_info->lab_info->dblabel_cnt;
            u8 *idx_buf = g_lrc_info->lab_info->plabel_buf_tmp;
            u16 plabel_buf_len = g_lrc_info->lab_info->plabel_buf_len;

            if ((plabel_buf_len / 2) < cnt) {
                return 0;
            }

            if (!idx_buf || (u32)(cnt * 8) > (lrc_flash_len >> 1)) {
                return 0;
            }

            for (i = 0; i < cnt; i++) {
                ((u16 *)idx_buf)[i] = i;
            }

            for (i = 0; i < cnt; i++) {
                u8 is_sort_over = 1;
                u16 j;
                u16 sub = cnt - i;
                for (j = 1; j < sub; j++) {
                    u16 idx_a = ((u16 *)idx_buf)[j - 1];
                    u16 idx_b = ((u16 *)idx_buf)[j];
                    u16 time_s_a = plabel_buf[idx_a].dbtime_s;
                    u16 time_s_b = plabel_buf[idx_b].dbtime_s;

                    /*
                     * @note 这段与 lrc_analysis_in_ram 里的排序同构, 三个容易
                     *       写错的地方:
                     *   1. 第二次比较的方向是 time_s_a > time_s_b, 也就是
                     *      "前一条晚于后一条才换"。写反的话整首歌的歌词顺序
                     *      会倒过来。
                     *   2. 秒数相等、而 100ms 位又不需要换时直接 continue,
                     *      不要落到第二个比较 —— 秒数已经相同, 再比一次不会
                     *      有结果, 只是白跑。
                     *   3. 相等分支换过之后, 局部的 idx_a/idx_b 与
                     *      time_s_a/time_s_b 都要跟着换回来, 否则紧接着的
                     *      第二个比较用的还是交换前的值。
                     */
                    if (time_s_a == time_s_b) {
                        if (plabel_buf[idx_a].btime_100ms > plabel_buf[idx_b].btime_100ms) {
                            ((u16 *)idx_buf)[j - 1] = idx_b;
                            ((u16 *)idx_buf)[j] = idx_a;
                            is_sort_over = 0;
                            {
                                u16 t = idx_a;
                                idx_a = idx_b;
                                idx_b = t;
                            }
                            time_s_a = plabel_buf[idx_a].dbtime_s;
                            time_s_b = plabel_buf[idx_b].dbtime_s;
                        } else {
                            continue;
                        }
                    }
                    if (time_s_a > time_s_b) {
                        ((u16 *)idx_buf)[j - 1] = idx_b;
                        ((u16 *)idx_buf)[j] = idx_a;
                        is_sort_over = 0;
                    }
                }
                if (is_sort_over) {
                    break;
                }
            }

            for (i = 0; i < cnt; i++) {
                u16 idx = ((u16 *)idx_buf)[i];
                memset(&tmp, 0, sizeof(tmp));
                *(u64 *)&tmp = *(u64 *)&plabel_buf[idx];
                sfc_write((u8 *)&tmp, lrc_flash_addr + (lrc_flash_len >> 1) + i * 8, 8);
            }
        }

        {
            TIME_LABEL *sorted_buf = (TIME_LABEL *)sdfile_flash_addr2cpu_addr(lrc_flash_addr + (lrc_flash_len >> 1));
            g_lrc_info->lab_info->g_plabel_buf = sorted_buf;
            g_lrc_info->lab_info->dbtime_base = sorted_buf[0].dbtime_s;
            g_lrc_info->lab_info->base_100ms = sorted_buf[0].btime_100ms;
            {
                u16 last = g_lrc_info->lab_info->dblabel_cnt - 1;
                g_lrc_info->lab_info->dbtime_limit = sorted_buf[last].dbtime_s;
                g_lrc_info->lab_info->limit_100ms = sorted_buf[last].btime_100ms;
            }
            g_lrc_info->sorting->bis_next_file = 1;
            g_lrc_info->blast_lable = 0;
            g_lrc_info->lrc_label_len = g_lrc_info->lab_info->dblabel_cnt * 8;
        }
    }

    return 1;
}

static u8 lrc_analysis_in_ram(void *lrc_handle, const LRC_FILE_IO *file_io)
{
    TIME_LABEL ptime_label[10];
    u8 bn = 1;
    u8 *plabel_buf;

    plabel_buf = (u8 *)g_lrc_info->lab_info->g_plabel_buf;

    if (!lrc_handle || !g_lrc_info || !file_io) {
        return 0;
    }

    g_lrc_info->file.hdl = lrc_handle;
    g_lrc_info->file._io = (LRC_FILE_IO *)file_io;
    memset(g_lrc_info->blrc_buf, 0, g_lrc_info->blrc_buf_len);
    g_lrc_info->label_id = 0;
    g_lrc_info->data_len_count = 0;
    g_lrc_info->real_len = 0;
    g_lrc_info->content_len = 0;
    g_lrc_info->lab_info->dblabel_cnt = 0;
    g_lrc_info->sorting->dbnow_fp_addr = 0;

    if (!lrc_coding_judge()) {
        return 0;
    }

    while (lrc_find_row_timelabel((u8 *)ptime_label, &bn)) {
        u16 label_cnt = g_lrc_info->lab_info->dblabel_cnt;
        u16 write_len = (u16)(bn * 8);

        if ((u32)write_len + label_cnt > g_lrc_info->lab_info->plabel_buf_len) {
            return 0;
        }

        memcpy(plabel_buf + label_cnt, ptime_label, write_len);
        g_lrc_info->lab_info->dblabel_cnt += bn * 8;

        if (g_lrc_info->lab_info->dblabel_cnt > g_lrc_info->lab_info->plabel_buf_len) {
            g_lrc_info->lab_info->dblabel_cnt -= bn * 8;
            break;
        }
    }

    {
        u16 label_cnt = g_lrc_info->lab_info->dblabel_cnt;
        if (label_cnt == 0) {
            return 0;
        }

        g_lrc_info->lab_info->dblabel_cnt = label_cnt / 8;

        {
            TIME_LABEL *plabel = g_lrc_info->lab_info->g_plabel_buf;
            u16 cnt = g_lrc_info->lab_info->dblabel_cnt;
            u16 i;

            for (i = 0; i < cnt; i++) {
                u8 is_sort_over = 1;
                u16 j;
                u16 sub = cnt - i;
                /*
                 * @note 冒泡内层有三个容易写错的地方:
                 *   1. 第二次比较是 plabel[j-1].dbtime_s > plabel[j].dbtime_s,
                 *      也就是"前一条晚于后一条才换"。写成反向的话整首歌的
                 *      歌词顺序会倒过来。
                 *   2. 秒数相等、而 100ms 位又不需要换时直接 continue, 不要
                 *      落到第二个比较 —— 秒数已经相同, 再比一次不会有结果。
                 *   3. 交换代码只有【两处】, 不是三处: 相等分支换完之后,
                 *      紧接着那次比较读的已经是交换后的元素, 不用再补一处。
                 */
                for (j = 1; j < sub; j++) {
                    if (plabel[j - 1].dbtime_s == plabel[j].dbtime_s) {
                        if (plabel[j - 1].btime_100ms > plabel[j].btime_100ms) {
                            u64 tmp = *(u64 *)&plabel[j - 1];
                            *(u64 *)&plabel[j - 1] = *(u64 *)&plabel[j];
                            *(u64 *)&plabel[j] = tmp;
                            is_sort_over = 0;
                        } else {
                            continue;
                        }
                    }
                    if (plabel[j - 1].dbtime_s > plabel[j].dbtime_s) {
                            u64 tmp = *(u64 *)&plabel[j - 1];
                            *(u64 *)&plabel[j - 1] = *(u64 *)&plabel[j];
                            *(u64 *)&plabel[j] = tmp;
                            is_sort_over = 0;
                    }
                }
                if (is_sort_over) {
                    break;
                }
            }
        }

        {
            TIME_LABEL *plabel = g_lrc_info->lab_info->g_plabel_buf;
            g_lrc_info->lab_info->dbtime_base = plabel[0].dbtime_s;
            g_lrc_info->lab_info->base_100ms = plabel[0].btime_100ms;
            {
                u16 last = g_lrc_info->lab_info->dblabel_cnt - 1;
                g_lrc_info->lab_info->dbtime_limit = plabel[last].dbtime_s;
                g_lrc_info->lab_info->limit_100ms = plabel[last].btime_100ms;
            }
            g_lrc_info->sorting->bis_next_file = 1;
            g_lrc_info->blast_lable = 0;
            g_lrc_info->lrc_label_len = label_cnt;
        }
    }

    return 1;
}

bool lrc_get(u16 dbtime_s, u8 btime_100ms)
{
    if (!g_lrc_info) {
        return 0;
    }
    return lrc_data_get(dbtime_s, btime_100ms, g_lrc_info) ? 1 : 0;
}

static u8 lrc_data_get(u16 dbtime_s, u8 btime_100ms, LRC_INFO *lrc_info)
{
    LABEL_INFO *lab = lrc_info->lab_info;
    int cnt0 = 0;

    if (lab->dbtime_limit < dbtime_s) {
        dbtime_s = lab->dbtime_limit;
        btime_100ms = lab->limit_100ms;
    } else if (lab->dbtime_limit == dbtime_s) {
        if (lab->limit_100ms < btime_100ms) {
            btime_100ms = lab->limit_100ms;
        }
    }

    if (lyrics_lib_flag) {
        if (dbtime_s < lab->dbtime_base) {
            dbtime_s = lab->dbtime_base;
            btime_100ms = lab->base_100ms;
            lrc_info->bfirst_lable = 1;
        } else if (dbtime_s == lab->dbtime_base) {
            if (btime_100ms < lab->base_100ms) {
                btime_100ms = lab->base_100ms;
            }
            lrc_info->bfirst_lable = 0;
        } else {
            lrc_info->bfirst_lable = 0;
        }
    }

    {
        TIME_LABEL *plabel_buf = lab->g_plabel_buf;
        u16 label_len = lrc_info->lrc_label_len;

        if ((u32)(lrc_info->label_id * 8) >= label_len) {
            lrc_info->label_id = (label_len / 8) - 1;
        }

        cnt0 = lrc_text_get(dbtime_s, btime_100ms, lrc_info, plabel_buf, &lrc_info->label_id);
    }

    lrc_info->lrc_data_len = lrc_info->content_len;

    if (dbtime_s >= lab->dbtime_limit) {
        lrc_info->blast_lable = 1;
        return 1;
    }

    lrc_info->blast_lable = 0;

    if ((u32)cnt0 > 65529) {
        return 0;
    }

    if (!lrc_info->read_next_lrc_flag) {
        return 1;
    }

    {
        u16 id = ++lrc_info->label_id;
        int cnt = lrc_info->content_len;
        int ret;

        while (1) {
            ret = read_next_lrc(lab->g_plabel_buf[id], lrc_info, &cnt);
            if (ret == 0) {
                id = ++lrc_info->label_id;
                continue;
            }
            break;
        }

        lrc_info->lrc_data_len = (u8)cnt;
        return 1;
    }
}

__attribute__((weak))
u8 lrc_ui_show(int text_id, u8 encode_type, u8 *buf, int len, u8 lrc_show_flag, u8 lrc_update)
{
    return 0;
}

bool lrc_show(int text_id, u16 dbtime_s, u8 btime_100ms)
{
    static u8 bremain_len = 0;
    static u8 bmust_show_next = 0;
    static bool lrc_roll_show = 0;
    static bool next_lrc_show = 0;

    if (!g_lrc_info) {
        return 0;
    }

    if (!lrc_data_get(dbtime_s, btime_100ms, g_lrc_info)) {
        return (u8)-1;
    }

    if (g_lrc_info->clr_lrc_disp_cb) {
        g_lrc_info->clr_lrc_disp_cb();
    }

    if (g_lrc_info->bis_lrc_update == 1) {
        g_lrc_info->bis_lrc_update = 0;
        next_lrc_show = 1;
        g_lrc_info->broll_speed_control = 0;
        bmust_show_next = 0;
        bremain_len = g_lrc_info->lrc_data_len;
        lrc_roll_show = 1;
    } else {
        if (bmust_show_next == 1 && g_lrc_info->bfirst_lable == 0) {
            if (g_lrc_info->broll_speed_control < g_lrc_info->blcd_roll_speed) {
                g_lrc_info->broll_speed_control++;
            } else {
                g_lrc_info->broll_speed_control = 0;
                bmust_show_next = 0;
                lrc_roll_show = 1;
            }
        }
    }

    if (!lrc_ui_show(text_id, g_lrc_info->coding_type, g_lrc_info->blrc_buf,
                     bremain_len, lrc_roll_show, next_lrc_show)) {
        u8 coding = g_lrc_info->coding_type;
        if (coding == 1) {
            ui_text_set_textw_by_id(text_id, (const char *)g_lrc_info->blrc_buf, bremain_len, 1, 2);
        } else if (coding == 2) {
            ui_text_set_textw_by_id(text_id, (const char *)g_lrc_info->blrc_buf, bremain_len, 0, 2);
        } else if (coding == 3) {
            ui_text_set_textw_by_id(text_id, (const char *)g_lrc_info->blrc_buf, bremain_len, 1, 6);
        } else {
            ui_text_set_text_by_id(text_id, (const char *)g_lrc_info->blrc_buf, bremain_len, 2);
        }
    }

    lrc_roll_show = 0;
    next_lrc_show = 0;
    bmust_show_next = (bremain_len != 0);

    return 1;
}

void lrc_destroy(void)
{
    if (g_lrc_info) {
        g_lrc_info = NULL;
    }
}

int lrc_param_init(LRC_CFG *cfg, u8 *lrc_info_buf)
{
    u8 *buf = NULL;

    if (!cfg) {
        return -1;
    }

    {
        /*
         * @note cfg 的字段都在【用到的地方】现读, 不要提前抄到局部变量 ——
         *       调用方常把 cfg 和 lrc_info_buf 放在同一块静态内存里, 下面对
         *       g_lrc_info(即 lrc_info_buf)的赋值有可能覆盖到 cfg, 提前读
         *       就会用上被覆盖前的旧值。
         */
        if (!g_lrc_info) {
            if (!lrc_info_buf) {
                return -1;
            }

            g_lrc_info = (LRC_INFO *)lrc_info_buf;
            buf = lrc_info_buf + 60;
            g_lrc_info->lab_info = (LABEL_INFO *)buf;
            buf = lrc_info_buf + 80;
            g_lrc_info->sorting = (SORTING_INFO *)buf;
            buf = lrc_info_buf + 84;
            g_lrc_info->blrc_buf = buf;
            /* @note LRC_SIZEOF_ALIN 宏里已经做了 ((var+al-1)/al)*al 的向上对齐,
             *       别再手动写成 (len + 3) —— 那样等于对齐两次, 白占 4 字节。 */
            buf = buf + LRC_SIZEOF_ALIN(cfg->once_disp_len, 4);

            /*
             * @note 两个分支各只写一个字段, 语义不同, 不要都写:
             *       标签存 flash 时, 这块 buf 是排序用的索引临时区
             *       (plabel_buf_tmp), g_plabel_buf 要留空, 等 lrc_analysis
             *       把它指向 flash 映射地址;
             *       标签留在 RAM 时, 这块 buf 本身就是标签数组。
             */
            if (cfg->enable_save_lable_to_flash) {
                g_lrc_info->lab_info->g_plabel_buf = NULL;
                g_lrc_info->lab_info->plabel_buf_tmp = buf;
            } else {
                g_lrc_info->lab_info->g_plabel_buf = (TIME_LABEL *)buf;
            }

            buf = buf + LRC_SIZEOF_ALIN(cfg->label_temp_buf_len, 4);
            g_lrc_info->lrc_read_buf = buf;
            g_lrc_info->file.hdl = NULL;
            g_lrc_info->file._io = NULL;
            g_lrc_info->blrc_buf_len = cfg->once_disp_len;
            g_lrc_info->lab_info->plabel_buf_len = cfg->label_temp_buf_len;
            g_lrc_info->once_read_len = cfg->once_read_len;
            g_lrc_info->roll_speed_ctrl_cb = cfg->roll_speed_ctrl_cb;
            g_lrc_info->clr_lrc_disp_cb = cfg->clr_lrc_disp_cb;
            g_lrc_info->lrc_text_id = cfg->lrc_text_id;
            g_lrc_info->read_next_lrc_flag = cfg->read_next_lrc_flag;
            g_lrc_info->save_flash = cfg->enable_save_lable_to_flash;
        }
    }

    if (!lrc_flash_addr) {
        RESFILE *fp = resfile_open("flash/app/LRIF");
        if (!fp) {
            log_print(4, NULL, "LRIF open err++++++++++++++++++++++\n");
            return 0;
        }
        {
            struct resfile_attrs attr;
            memset(&attr, 0, sizeof(attr));
            resfile_get_attrs(fp, &attr);
            lrc_flash_addr = sdfile_cpu_addr2flash_addr(attr.sclust);
            lrc_flash_len = attr.fsize;
            resfile_close(fp);
        }
    }

    return 0;
}

static u8 lrc_coding_judge(void)
{
    u16 read_len;

    read_len = lrc_get_data(&g_lrc_info->file, 0, g_lrc_info->lrc_read_buf, g_lrc_info->once_read_len);
    g_lrc_info->real_len = read_len;

    if (read_len < 4) {
        return 0;
    }

    {
        u8 *buf = g_lrc_info->lrc_read_buf;
        u8 b0 = buf[0];
        u8 b1 = buf[1];
        u8 type;

        /*
         * @note 先用局部 type 定下来, 判完再【一次性】写回 coding_type ——
         *       四个分支各写一次的话, 以后加一种编码很容易漏掉赋值, 而
         *       coding_type 漏写的后果是整首歌按错编码取模, 满屏乱码。
         */
        if (b0 == 0xFF && b1 == 0xFE) {
            type = LRC_UTF16_S;
        } else if (b1 == 0xFF && b0 == 0xFE) {
            type = LRC_UTF16_B;
        } else if (b0 == 0xEF && b1 == 0xBB) {
            type = (buf[2] == 0xBF) ? LRC_UTF8 : LRC_GBK;
        } else {
            type = LRC_GBK;
        }

        g_lrc_info->coding_type = type;
    }

    return 1;
}

static u8 lrc_find_row_timelabel(u8 *plabel, u8 *pn)
{
    u8 plabel_text[20];
    TIME_LABEL *tlbl = (TIME_LABEL *)plabel;

    *pn = 1;
    g_lrc_info->sorting->bdata_len = 0;

    while (1) {
        u8 brlen = 0;
        u8 is_begin = 0;
        u16 dbnow_fp_addr_save = 0;
        u8 char_len;
        u16 ch;
        LRC_INFO *info;

        /*
         * @note 循环条件是 info->real_len, 不能写成 while (1) —— real_len
         *       变 0 表示文件已经读空, 这是本循环唯一的正常出口, 漏掉就会
         *       一直空转。
         *
         *       info 在 coding_data_pick 之后要重新取一次: 那个函数会改
         *       real_len(缓冲区不够时还会换一批数据), 沿用调用前的快照就会
         *       拿着过期的长度继续取字符。
         */
        info = g_lrc_info;
        while (info->real_len) {
            ch = coding_data_pick(&char_len);
            if (ch == (u16)-1) {
                return 0;
            }
            info = g_lrc_info;
            dbnow_fp_addr_save = info->sorting->dbnow_fp_addr;

            if (ch == 91) {
                is_begin = 1;
                brlen = 0;
                continue;
            }

            if (is_begin != 1) {
                continue;
            }

            if (ch == 93 || brlen > 19) {
                goto label_found;
            }

            plabel_text[brlen] = (u8)ch;
            brlen++;
        }

        /* real_len 变 0 = 数据读空, 后面再没有时间标签了 */
        return 0;

label_found:

        {
            u8 idx = *pn - 1;
            u16 *pdbtime_s = &tlbl[idx].dbtime_s;
            u8 *pbtime_100ms = &tlbl[idx].btime_100ms;
            u8 bmin = 0;
            u8 bsec;
            u32 flag_ms;
            u16 dbmultiple;
            u16 dbtime_val = 0;

            *pdbtime_s = 0;
            *pbtime_100ms = 0;

            for (bmin = 0; bmin < brlen; bmin++) {
                u8 c = plabel_text[bmin];
                if (!((c >= '0' && c <= '9') || c == ':' || c == '.' || c == ' ')) {
                    goto while_cond_backedge;
                }
            }

            bmin = 0;
            while (bmin <= brlen) {
                if (plabel_text[bmin] == ':') {
                    bmin++;
                    break;
                }
                bmin++;
                if (bmin > brlen) {
                    goto while_cond_backedge;
                }
            }

            {
                u16 pos = bmin;
                u16 val = 0;
                dbmultiple = 1;
                while (pos != 0) {
                    pos--;
                    if (plabel_text[pos] >= '0' && plabel_text[pos] <= '9') {
                        val += (plabel_text[pos] - '0') * dbmultiple;
                        *pdbtime_s = val;
                        dbmultiple *= 10;
                    }
                }
                *pdbtime_s = val * 60;
            }

            bsec = bmin;
            flag_ms = 1;
            while (bsec <= brlen) {
                if (plabel_text[bsec] == '.') {
                    break;
                }
                bsec++;
                if (bsec > brlen) {
                    flag_ms = 0;
                    break;
                }
            }

            {
                u32 pos = bsec;
                u16 val = *pdbtime_s;
                dbmultiple = 1;
                while (pos > bmin) {
                    pos--;
                    if (plabel_text[pos] >= '0' && plabel_text[pos] <= '9') {
                        val += (plabel_text[pos] - '0') * dbmultiple;
                        *pdbtime_s = val;
                        dbmultiple *= 10;
                    }
                }
            }

            if (flag_ms == 1) {
                u8 bms_val = 0;
                u32 pos = brlen;
                dbmultiple = 1;
                while (pos > (u32)(bsec + 1)) {
                    pos--;
                    if (plabel_text[pos] >= '0' && plabel_text[pos] <= '9') {
                        bms_val += (plabel_text[pos] - '0') * (u8)dbmultiple;
                        *pbtime_100ms = bms_val;
                        dbmultiple *= 10;
                    }
                }
            } else {
                *pbtime_100ms = 0;
            }
        }

        if (check_and_read(1)) {
            return (u8)-1;
        }

        {
            /*
             * @note 这里用 switch 而不是 if/else 链, 两点要注意:
             *   1. default 走的是【continue】(回外层循环重新找标签), 不是
             *      "当成单字节编码处理" —— coding_type 越界说明编码判别本身
             *      出了问题, 硬按某种编码往下读只会读出乱码。
             *   2. 各 case 各自读一次 lrc_read_buf / data_len_count, 不提到
             *      switch 外面缓存, 免得将来某个分支改了计数器还看不出来。
             */
            LRC_INFO *info = g_lrc_info;
            u8 bch;

            switch (info->coding_type) {
            case 0:
            case 3:
                bch = info->lrc_read_buf[info->data_len_count];
                break;
            case 2:
                bch = info->lrc_read_buf[info->data_len_count + 1];
                break;
            case 1:
                bch = info->lrc_read_buf[info->data_len_count];
                break;
            default:
                goto while_cond_backedge;
            }

            if (bch == 91) {
                if (*pn < 9) {
                    (*pn)++;
                }
                continue;
            }

            {
                u8 char_len2 = 0;
                u16 dbtext = 0;
                u16 prev_ch = 0;

                do {
                    dbtext = coding_data_pick(&char_len2);
                    if (dbtext == (u16)-1) {
                        break;
                    }
                    if (dbtext != 10 && dbtext != 13 && dbtext != 91) {
                        g_lrc_info->sorting->bdata_len += char_len2;
                    }
                    if (prev_ch == 10 || dbtext == 91) {
                        break;
                    }
                    prev_ch = dbtext;
                } while (1);

                {
                    u16 cl = char_len2;
                    g_lrc_info->sorting->dbnow_fp_addr -= cl;
                    g_lrc_info->data_len_count -= cl;
                }

                if (g_lrc_info->sorting->bdata_len == 0) {
                    *pn = 1;
                    continue;
                }

                {
                    /*
                     * @note 同一行歌词可能挂着多个时间标签([00:12][01:20] 这种
                     *       写法), 这里把行位置和行长度回填给每一个。
                     *       bdata_len 每轮都从 g_lrc_info 里现读: 它是全局
                     *       状态, 缓存到循环外, 将来循环体里一旦改了它就
                     *       不一致。计数器用 u32 而不是 u8 —— 先减再判 0 的
                     *       写法用无符号宽类型不会绕回。
                     */
                    LRC_INFO *info2 = g_lrc_info;
                    u32 k = *pn;

                    while (k != 0) {
                        k--;
                        tlbl[k].wline_pos = dbnow_fp_addr_save;
                        tlbl[k].btext_len = info2->sorting->bdata_len;
                    }
                }

                return 1;
            }
        }

    while_cond_backedge:
        continue;
    }
}

static u16 lrc_get_data(LRC_FILE *file, u32 faddr, u8 *buf, u16 len)
{
    u16 read_len = 0;

    if (!file || !file->_io) {
        return 0;
    }

    if (file->_io->seek) {
        file->_io->seek((RESFILE *)file->hdl, faddr, 0);
    }

    if (file->_io->read) {
        read_len = (u16)file->_io->read(buf, len, 1, (RESFILE *)file->hdl);
    }

    g_lrc_info->cur_faddr = faddr + read_len;

    return read_len;
}

static int check_and_read(u16 nead_data)
{
    u16 remain = g_lrc_info->real_len - g_lrc_info->data_len_count;

    if (remain < nead_data) {
        u32 faddr;

        g_lrc_info->data_len_count = 0;
        faddr = g_lrc_info->cur_faddr - remain;
        g_lrc_info->cur_faddr = faddr;

        {
            u16 read_len = lrc_get_data(&g_lrc_info->file, faddr,
                                        g_lrc_info->lrc_read_buf,
                                        g_lrc_info->once_read_len);
            g_lrc_info->real_len = read_len;
            if (read_len == 0) {
                return -1;
            }
        }
    }

    return 0;
}

static u16 coding_data_pick(u8 *char_len)
{
    u16 dbtext = 0;

    *char_len = 0;

    if (g_lrc_info->coding_type == 0) {
        if (check_and_read(1)) {
            return (u16)-1;
        }
        {
            u16 dlc = g_lrc_info->data_len_count;
            g_lrc_info->data_len_count = dlc + 1;
            dbtext = g_lrc_info->lrc_read_buf[dlc];
        }
        g_lrc_info->sorting->dbnow_fp_addr++;
        *char_len = 1;
    } else if (g_lrc_info->coding_type < 3) {
        if (check_and_read(2)) {
            return (u16)-1;
        }
        {
            u8 *buf = g_lrc_info->lrc_read_buf;
            /*
             * @note 两个字节分【两次】取, 每次都用 data_len_count++ 推进 ——
             *       两次读之间还要读 coding_type 判字节序, 一次算出 dlc + 2
             *       再存回的写法留不下这个插入点。
             *       组合时用局部 b0, 不要回读已写进 dbtext 的值: dbtext 是
             *       u16, 取低字节还得再截一次, 白绕一圈。
             */
            u8 b0 = buf[g_lrc_info->data_len_count++];
            u8 is_be;
            u8 b1;

            dbtext = b0;

            /*
             * @note 先把字节序判断的结果存进 is_be, 再读第二个字节, 最后才
             *       分支 —— 若把 buf[..++] 搬进两个分支里, 计数器的递增就被
             *       复制成两份, 以后改一处漏一处, 偏移就错开了。
             */
            is_be = (g_lrc_info->coding_type == 2);
            b1 = buf[g_lrc_info->data_len_count++];

            if (is_be) {
                dbtext = (u16)((b0 << 8) | b1);
            } else {
                dbtext = (u16)(b0 | (b1 << 8));
            }
        }
        g_lrc_info->sorting->dbnow_fp_addr += 2;
        *char_len = 2;
    } else if (g_lrc_info->coding_type == 3) {
        if (check_and_read(1)) {
            return (u16)-1;
        }
        {
            u8 *buf = g_lrc_info->lrc_read_buf;
            u16 dlc = g_lrc_info->data_len_count;
            u8 b0 = buf[dlc];
            int utf8_size;

            dbtext = b0;
            utf8_size = get_utf8_size(b0);

            if ((u32)(utf8_size - 2) < 5) {
                if (check_and_read((u16)utf8_size)) {
                    return (u16)-1;
                }
                /*
                 * @note data_len_count 在这里必须【现读】, 不能沿用上面那个
                 *       dlc —— 紧挨着的 check_and_read(utf8_size) 在数据不够
                 *       时会重新读一批并把计数器清零, 拿旧快照做下标就会读到
                 *       缓冲区里错位的字节。
                 */
                utf8_2_unicode_one(&g_lrc_info->lrc_read_buf[g_lrc_info->data_len_count],
                                   &dbtext);
                g_lrc_info->data_len_count += utf8_size;
                g_lrc_info->sorting->dbnow_fp_addr += utf8_size;
                *char_len = (u8)utf8_size;
            } else {
                *char_len = 1;
                g_lrc_info->sorting->dbnow_fp_addr++;
                g_lrc_info->data_len_count++;
            }
        }
    }

    return dbtext;
}

static int lrc_text_get(u16 dbtime_s, u8 btime_100ms, LRC_INFO *lrc_info, TIME_LABEL *lrc_labelbuf, u16 *res_cnt)
{
    u8 utf8_buf[512];
    int sel;
    int content_len;
    u32 wline_pos;
    u32 time_gap;
    /*
     * @note wpre_place 记的是"上一次已经显示过的那一行在文件里的位置",
     *       靠它判断当前时间点对应的还是不是刚才那一行。所以它只能有
     *       【一个】, 而且必须是函数级 static: 若分散成几个块作用域里各自
     *       的 static, 它们互不相通, wpre_place == wline_pos 几乎永不成立,
     *       同一行歌词每次都会被重新读文件、重新刷屏。
     */
    static u32 wpre_place;

    if (lrc_info->sorting->bis_next_file == 1) {
        wpre_place = 0;
        lrc_info->bis_lrc_update = 0;
        lrc_info->sorting->bis_next_file = 0;
    }

    sel = get_sel_label(dbtime_s, btime_100ms, lrc_labelbuf);

    if ((u32)sel > 65529) {
        return sel;
    }

    *res_cnt = (u16)sel;

    {
        u16 next_time_s = lrc_labelbuf[sel + 1].dbtime_s;
        u16 cur_time_s = lrc_labelbuf[sel].dbtime_s;
        time_gap = next_time_s - cur_time_s;
    }

    content_len = lrc_labelbuf[sel].btext_len;

    if ((u32)content_len > (u32)(lrc_info->blrc_buf_len - 2)) {
        content_len = lrc_info->blrc_buf_len - 2;
    }

    wline_pos = lrc_labelbuf[sel].wline_pos;

    if (wpre_place == wline_pos) {
        return 0;
    }

    {
        u16 i;
        lrc_info->broll_speed_control = 0;
        lrc_info->bis_lrc_update = 1;

        for (i = 0; i < lrc_info->blrc_buf_len; i++) {
            lrc_info->blrc_buf[i] = 32;
        }

        if (lrc_info->coding_type == 3) {
            memset(utf8_buf, 0, 512);
            lrc_get_data(&lrc_info->file, wline_pos, utf8_buf, (u16)content_len);
            content_len = utf8_2_unicode(utf8_buf, (u8)content_len,
                                         lrc_info->blrc_buf,
                                         (u8)lrc_info->blrc_buf_len);
        } else {
            lrc_get_data(&lrc_info->file, wline_pos, lrc_info->blrc_buf, (u16)content_len);
        }

        lrc_info->content_len = (u8)content_len;
        error_coding_wipe(0, content_len & 0xFF, lrc_info);

        if (lrc_info->roll_speed_ctrl_cb) {
            lrc_info->roll_speed_ctrl_cb(lrc_info->content_len, time_gap,
                                         &lrc_info->blcd_roll_speed);
        }

        wpre_place = wline_pos;
    }

    return 0;
}

static int read_next_lrc(TIME_LABEL lrc_label, LRC_INFO *lrc_info, int *cnt)
{
    u8 utf8_buf[512];
    TIME_LABEL *plabel_buf;
    int content_cnt = *cnt;
    int sel;
    int len;

    plabel_buf = lrc_info->lab_info->g_plabel_buf;

    if ((u32)(content_cnt + 4) >= lrc_info->blrc_buf_len) {
        return LRC_BUF_OVER;
    }

    {
        /*
         * @note 按编码往已有内容后面补一个换行, 好让下一行接着显示:
         *       双字节编码要补四个字节(CR 0 LF 0 或 0 CR 0 LF, 高低位顺序
         *       跟着 coding 走), 单字节编码只补 CR LF 两个。
         *       补错长度或字节序, 下一行就会从半个字符开始, 整行歪掉。
         */
        u8 coding = lrc_info->coding_type;

        if (coding == 1) {
            lrc_info->blrc_buf[content_cnt] = 13;
            lrc_info->blrc_buf[content_cnt + 1] = 0;
            lrc_info->blrc_buf[content_cnt + 2] = 10;
            lrc_info->blrc_buf[content_cnt + 3] = 0;
            content_cnt += 4;
        } else if (coding == 2) {
            lrc_info->blrc_buf[content_cnt] = 0;
            lrc_info->blrc_buf[content_cnt + 1] = 13;
            lrc_info->blrc_buf[content_cnt + 2] = 0;
            lrc_info->blrc_buf[content_cnt + 3] = 10;
            content_cnt += 4;
        } else {
            lrc_info->blrc_buf[content_cnt] = 13;
            if (coding == 3) {
                lrc_info->blrc_buf[content_cnt + 1] = 0;
                lrc_info->blrc_buf[content_cnt + 2] = 10;
                lrc_info->blrc_buf[content_cnt + 3] = 0;
                content_cnt += 4;
            } else {
                lrc_info->blrc_buf[content_cnt + 1] = 10;
                content_cnt += 2;
            }
        }
    }

    {
        u8 *dst = lrc_info->blrc_buf + content_cnt;

        sel = get_sel_label(lrc_label.dbtime_s, lrc_label.btime_100ms, plabel_buf);

        if (sel == LRC_LABEL_END) {
            return LRC_BUF_OVER;
        }

        {
            /*
             * @note 先判"这一行还放得下"再去读文件: text_len 加上已写入的
             *       content_cnt 超过缓冲区就直接返回 LRC_BUF_OVER, 否则
             *       lrc_get_data 会写出界。
             */
            u8 text_len = plabel_buf[sel].btext_len;

            if ((u32)text_len + content_cnt > lrc_info->blrc_buf_len) {
                return LRC_BUF_OVER;
            }

            if (lrc_info->coding_type == 3) {
                memset(utf8_buf, 0, 512);
                lrc_get_data(&lrc_info->file, plabel_buf[sel].wline_pos,
                             utf8_buf, (u16)text_len);
                len = utf8_2_unicode(utf8_buf, text_len, dst,
                                     (u8)(lrc_info->blrc_buf_len - content_cnt));
            } else {
                lrc_get_data(&lrc_info->file, plabel_buf[sel].wline_pos,
                             dst, (u16)text_len);
                len = text_len;
            }

            content_cnt += len;
            error_coding_wipe(*cnt, content_cnt, lrc_info);
            *cnt = content_cnt;
        }
    }

    return 0;
}

static int get_sel_label(u16 dbtime_s, u8 btime_100ms, TIME_LABEL *labelbuf)
{
    u16 label_len = g_lrc_info->lrc_label_len;
    u32 cnt = label_len / 8;
    u32 sub = cnt - 1;
    u32 i;

    for (i = 0; i < sub; i++) {
        int cmp1 = compare_label_time(dbtime_s, btime_100ms, labelbuf[i]);
        u16 cur_time_s = labelbuf[i].dbtime_s;

        if (cur_time_s == g_lrc_info->lab_info->dbtime_limit) {
            return i;
        }

        {
            int cmp2 = compare_label_time(dbtime_s, btime_100ms, labelbuf[i + 1]);

            if ((cmp1 == 1 && cmp2 == 0) || cmp1 == 2) {
                return i;
            }
            if (cmp2 == 2) {
                return i + 1;
            }
            if (cmp1 == 1 && cmp2 == 1) {
                continue;
            }
            return LRC_LABEL_END;
        }
    }

    return LRC_LABEL_END;
}

/*
 * 把取模时会显示成乱码的字节对(0xAC 0xE3 / 0xE3 0xAC)换成"空格 + 0",
 * 免得一个坏字符把整行往后都挤歪。
 *
 * @note 步进是【每轮一格】, 不是两格。虽然每次动的是相邻两个字节, 但下一轮
 *       还要从中间那个字节重新判一次 —— 跨过去会漏掉 "AC E3 AC" 这种连排
 *       的情况。
 */
static void error_coding_wipe(u32 wfirst, u32 wlast, LRC_INFO *lrc_info)
{
    u32 i;

    for (i = wfirst; i < wlast; i++) {
        u8 c = lrc_info->blrc_buf[i];
        if (c == 0xAC) {
            if (lrc_info->blrc_buf[i + 1] == 0xE3) {
                lrc_info->blrc_buf[i] = 32;
                lrc_info->blrc_buf[i + 1] = 0;
            }
        } else if (c == 0xE3) {
            if (lrc_info->blrc_buf[i + 1] == 0xAC) {
                lrc_info->blrc_buf[i] = 0;
                lrc_info->blrc_buf[i + 1] = 32;
            }
        }
    }
}

static int compare_label_time(u16 dbtime_s, u8 btime_100ms, TIME_LABEL lrc_label)
{
    if (lrc_label.dbtime_s < dbtime_s) {
        return 1;
    }
    if (lrc_label.dbtime_s != dbtime_s) {
        return 0;
    }
    if (lrc_label.btime_100ms < btime_100ms) {
        return 1;
    }
    if (lrc_label.btime_100ms > btime_100ms) {
        return 0;
    }
    return 2;
}
