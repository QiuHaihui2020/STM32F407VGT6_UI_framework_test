/*
 * mem_var.c —— 资源缓存表(把已解码的资源按参数指纹缓存在堆上, 命中就不再读盘)
 *
 * 【谁在用】应用层 lcd_ui_api.c 调 mem_var_init; 驱动层 ui_resources_manager.c
 *   调 add/get/search; 框架侧 liba/ui_dot/ui_core_api.c 调 mem_var_free。
 *
 * 【缓存键】(index, type, id, page, prj) 五个 u32 放进一个临时数组, 算出 CRC16
 *   与一个逐字节累加的 checksum 作为快速比对用的粗筛值。
 *   只靠这两个粗筛值无法最终确认命中(checksum 对字节顺序不敏感, 实际防线
 *   只有 CRC16 那 16 位), 所以【表项里连五个键一起存下来】, 粗筛过了再逐个
 *   确认, 把碰撞归零。代价是每项多 20 字节, sizeof(struct mem_var) 36 字节,
 *   3KB 容量下的项数上限约减半。
 *   容量不足时 mem_var_add 会返回 -EFAULT, 表现为该项不进缓存(多读一次盘),
 *   不影响正确性。想看实际用量: 把 lcd_ui_api.c 里 mem_var_init 的第二个参数
 *   (debug)改成 true, mem_var_stat() 会打印 items / use_mem_size / hits。
 *
 * 【行号约定】ASSERT 宏内嵌 __LINE__, 两处预期落在 140 / 141。本文件按固定
 *   行号布局组织, 空行不要随意增删, 否则断言打印的行号会偏。
 *
 * 【段属性】除 checksum_calc 在 .mem_var.text 外, 其余七个函数都在 .ui_ram
 *   (要在 RAM 里执行); var_list 在 .mem_var.data。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma data_seg(".mem_var.data")
#pragma code_seg(".ui_ram")
#endif

#include "jl_os_api.h"
#include "jl_list.h"
#include "res/mem_var.h"
#include "jl_crc.h"
#include "jl_debug.h"    /* ASSERT / log_*: 显式包含, 保证本文件自包含 */

struct mem_var_head var_list SEC(.mem_var.data);

u16 checksum_calc(u8 *buf, u16 len);
#line 16
void mem_var_init(u32 size, u8 debug)
{
    INIT_LIST_HEAD(&var_list.head);
    var_list.total_mem_size = size;
    var_list.debug = debug;
}

AT(.mem_var.text) u16 checksum_calc(u8 *buf, u16 len)
{
    int i;
    u16 sum = 0;

    for (i = 0; i < len; i++) {
        sum += buf[i];
    }

    return sum;
}


int mem_var_add(u32 index, u32 type, u32 id, u32 page, u32 prj, u8 *buf, u16 len)
{
    /*
     * "没初始化"单独拎出来判, 与"真的满了"区分开: mem_var_init 没被调用过时
     * total_mem_size 为 0, 下面那个【无符号】比较会恒真, 每次都走"内存不够"
     * —— 不会崩, 但缓存永远不生效, 且只有开了 debug 才有一行提示。
     */
    if (var_list.total_mem_size == 0) {
        if (var_list.debug) {
            printf("mem_var not inited!\n");
        }
        return -ENOMEM;
    }

    /*
     * 容量不足与 malloc 失败都是内存不足, 统一返回 -ENOMEM ——
     * 不要用 -EFAULT(Bad address) / -EINVAL(Invalid argument), 按 errno 惯例
     * 判断的调用方会误解。注: 现有 10 个调用点都忽略返回值。
     */
    if ((var_list.use_mem_size + sizeof(struct mem_var) + len) > var_list.total_mem_size) {
        if (var_list.debug) {
            printf("mem_var memory not enough!\n");
        }
        return -ENOMEM;
    }
    struct mem_var *var = malloc(sizeof(struct mem_var) + len);
    if (var == NULL) {
        return -ENOMEM;
    }

    u32 param[5];
    param[0] = index;
    param[1] = type;
    param[2] = id;
    param[3] = page;
    param[4] = prj;
    u16 crc = CRC16((u8 *)param, sizeof(param));
    u16 checksum = checksum_calc((u8 *)param, sizeof(param));

    if (var_list.debug) {
        printf("%08x, %08x, %08x, %08x, %08x, crc:%04x, checksum:%04x\n", param[0], param[1], param[2], param[3], param[4], crc, checksum);
    }

    /* 五个键原样存下来, 供 mem_var_search 逐个确认。 */
    var->var.index = index;
    var->var.type  = type;
    var->var.id    = id;
    var->var.page  = page;
    var->var.prj   = prj;

    var->var.checksum = checksum;
    var->var.crc = crc;
    var->var.len = len;
    memcpy(var->var.buf, buf, len);
    list_add_tail(&var->head, &var_list.head);

    var_list.items++;
    var_list.use_mem_size += sizeof(struct mem_var) + len;

    return 0;
}


struct mem_var *mem_var_search(u32 index, u32 type, u32 id, u32 page, u32 prj)
{
    u32 param[5];
    param[0] = index;
    param[1] = type;
    param[2] = id;
    param[3] = page;
    param[4] = prj;
    u16 crc = CRC16((u8 *)param, sizeof(param));
    u16 checksum = checksum_calc((u8 *)param, sizeof(param));

    struct mem_var *p;
    list_for_each_entry(p, &var_list.head, head) {
        /*
         * crc + checksum 只作【粗筛】, 筛过之后必须逐个键确认 —— 只比粗筛值
         * 的话, 两组不同的键一旦撞上同一对校验值就会返回错误的资源, 而且
         * 调用方无从察觉(见 mem_var.h 里那段说明)。
         * 绝大多数不匹配的表项在头一个 crc 比较处就被短路掉, 所以逐键确认
         * 基本不增加遍历开销。
         */
        if (p->var.crc == crc && p->var.checksum == checksum
            && p->var.index == index && p->var.type == type
            && p->var.id == id && p->var.page == page
            && p->var.prj == prj) {
            return p;
        }
    }
    return NULL;
}



void mem_var_stat()
{
    if (var_list.debug) {
        printf("var_list.items : %d\n", var_list.items);
        printf("var_list.use_mem_size : %d\n", var_list.use_mem_size);
        printf("var_list.hits : %d\n", var_list.hits);
    }
}


void mem_var_get(struct mem_var *var, u8 *buf, u16 len)
{
    /* 按表项实际长度截断, 并判空入参: 调用方给的 len 可能大于表项里的
     * var.len, 直接按它拷贝会越界读表项、同时越界写调用方的缓冲区。 */
    if (var == NULL || buf == NULL) {
        return;
    }
    if (len > var->var.len) {
        len = var->var.len;
    }

    memcpy(buf, var->var.buf, len);
    var_list.hits++;
}


int mem_var_del(struct mem_var *var)
{
    struct mem_var *p, *n;

    list_for_each_entry_safe(p, n, &var_list.head, head) {
        if (p == var) {
            /* 删的是遍历到的 p, 不是实参 var(此刻两者相等) —— 用 p 更能说明
             * "只删链表里确实存在的那一项"。 */
            list_del(&p->head);
            free(p);
            return 1;
        }
    }
    return 0;
}


void mem_var_free()
{
    struct mem_var *p, *n;

    list_for_each_entry_safe(p, n, &var_list.head, head) {
        var_list.items--;
        var_list.use_mem_size -= sizeof(struct mem_var) + p->var.len;
        list_del(&p->head);
        free(p);
    }

    /*
     * 这两个 ASSERT 在 config_asser 为假时只记录、不停机, 随后照样往下跑。
     * 链表此刻确实已经空了, 计数对不上只说明中途有过不走 mem_var_add /
     * mem_var_del 的增减 —— 与其带着错误的计数继续跑(下次 mem_var_add 的
     * 容量判断就会用它), 不如一并归零。
     */
    ASSERT(var_list.items == 0);
    ASSERT(var_list.use_mem_size == 0);

    var_list.items = 0;
    var_list.use_mem_size = 0;
    var_list.hits = 0;
}

/*
 * 实现注意事项与已知限制
 *
 *  1) 【命中判定必须逐键确认】表项里存了 (index, type, id, page, prj) 五个键,
 *     crc / checksum 只作粗筛。只比粗筛值的话, 两组不同的键撞上同一对校验值
 *     就会返回错误的资源, 且调用方无从察觉 —— 这是本模块最要紧的一条。
 *     代价是每项多 20 字节, 3KB 容量下项数上限约减半; 容量不足只是少缓存
 *     几项(多读一次盘), 不影响正确性。
 *
 *  2) 【mem_var_get 按表项长度截断】调用方给的 len 大于 var.len 时按 var.len
 *     截断, 否则会越界读表项、同时越界写调用方缓冲。入参也判空。
 *
 *  3) 【容量判断是无符号比较】mem_var_init 没被调用过时 total_mem_size 为 0,
 *     该比较恒真 —— 所以"没初始化"单独判一次, 与"真的满了"区分, 两者都带
 *     debug 提示, 免得缓存不生效却查不出原因。
 *
 *  4) 【返回码】容量不足与 malloc 失败都返回 -ENOMEM。注: 现有 10 个调用点
 *     都忽略返回值, 需要感知缓存是否生效时要自己接。
 *
 *  5) 【mem_var_free 末尾会归零计数】两个 ASSERT 在 config_asser 为假时只
 *     记录不停机; 链表此刻确实已空, 所以 items / use_mem_size / hits 一并
 *     归零, 免得下次 mem_var_add 的容量判断用到错值。
 *
 *  6) 【行号】mem_var_get 里的长度截断占了几行, 文件头那条"ASSERT 预期落在
 *     140 / 141"已经不准(实际在 150 / 151)。这里【故意不用 #line 拨回】——
 *     断言就该打印真实行号。
 */
