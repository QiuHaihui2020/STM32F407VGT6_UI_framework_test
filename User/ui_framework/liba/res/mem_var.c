/*
 * mem_var.c —— 资源缓存表(把已解码的资源按参数指纹缓存在堆上, 命中就不再读盘)
 *
 * 【来源】从 cpu/br27/liba/res.a 的 mem_var.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/mem_var.ll
 *     原始路径: btsdk/lib/utils/ui/resource/mem_var.c
 *
 * 【谁在用】应用层 lcd_ui_api.c 调 mem_var_init; 驱动层 ui_resources_manager.c
 *   调 add/get/search; 已还原的框架代码 liba/ui_dot/ui_core_api.c 调 mem_var_free。
 *
 * 【缓存键】(index, type, id, page, prj) 五个 u32 放进一个临时数组, 算出 CRC16
 *   与一个逐字节累加的 checksum 作为快速比对用的粗筛值。
 *   原库到粗筛为止就算命中(表里不存原始键值, 无法最终确认); 【加固后表项里
 *   存下了这五个键, 粗筛过了还要逐个确认】, 碰撞归零。代价是每项多 20 字节,
 *   sizeof(struct mem_var) 由 16 变 36, 3KB 容量下的项数上限约减半。
 *   容量不足时 mem_var_add 会返回 -EFAULT, 表现为该项不进缓存(多读一次盘),
 *   不影响正确性。想看实际用量: 把 lcd_ui_api.c 里 mem_var_init 的第二个参数
 *   (debug)改成 true, mem_var_stat() 会打印 items / use_mem_size / hits。
 *
 * 【行号锁定】ASSERT 宏内嵌 __LINE__, 两处必须落在 140 / 141。函数体由
 *   cpu/br27/tools/ui_reimpl/gen_mem_var.py 按绝对行号拼出, 空行不要随意增删。
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
#include "jl_debug.h"    /* ASSERT / log_*: 原厂靠别处间接带入, 这里补成自包含 */

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
     * 加固: mem_var_init 没被调用过时 total_mem_size 为 0, 下面那个【无符号】
     * 比较恒真, 每次都走"内存不够" —— 不会崩, 但缓存永远不生效, 而且只有开了
     * debug 才有一行提示。这里把"没初始化"单独拎出来, 与"真的满了"可区分。
     */
    if (var_list.total_mem_size == 0) {
        if (var_list.debug) {
            printf("mem_var not inited!\n");
        }
        return -ENOMEM;
    }

    /*
     * 加固: 原库这两个返回码与 errno 惯例【正好相反】—— 容量不足给 -EFAULT
     * (Bad address), malloc 失败给 -EINVAL(Invalid argument), 按 errno 判断的
     * 调用方会误解。两种都是内存不足, 统一成 -ENOMEM。
     * 注: 现有 10 个调用点全部忽略返回值, 此改动不影响现有行为。
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

    /* 加固: 把五个键原样存下来, 供 mem_var_search 逐个确认。 */
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
         * 加固: crc + checksum 只作【粗筛】, 筛过之后逐个键确认。
         * 原库到粗筛为止就 return —— 撞上就返回错误的资源(见 mem_var.h 里
         * 那段说明: checksum 对字节顺序不敏感, 实际防线只有 16 位)。
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
    /* 加固: 原库【不校验 len】—— 表项里明明存了 var.len 却不用, 直接按调用方
     * 给的 len 拷贝。传大了就越界读表项、同时越界写调用方的缓冲区。
     * 这里按表项实际长度截断, 并补上入参判空。 */
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
            /* 加固: 原库这里删的是【实参 var】而不是遍历到的 p。此刻两者相等,
             * 行为本就正确, 但那种写法容易让人以为可以传个不在链表里的指针进来。
             * 统一用 p, 意图更清楚。 */
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
     * 加固: 这两个 ASSERT 在 config_asser 为假时只调 cpu_assert【不停机】,
     * 随后照样往下跑。链表此刻确实已经空了, 所以计数对不上只说明中途有过
     * 不走 mem_var_add / mem_var_del 的增减 —— 与其带着错误的计数继续跑
     * (下次 mem_var_add 的容量判断就会用它), 不如一并归零。
     */
    ASSERT(var_list.items == 0);
    ASSERT(var_list.use_mem_size == 0);

    var_list.items = 0;
    var_list.use_mem_size = 0;
    var_list.hits = 0;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍然照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在
 * cpu/br27/tools/ui_reimpl/accept/mem_var.txt 并锁定指纹)。
 *
 *   [已修] 1 —— 缓存命中只比 CRC16 + checksum, 不保存也不比对原始键值
 *                (本文件危害最大的一条: 碰撞就返回错误的资源且无从察觉)。
 *                已把五个键存进 struct mem_var_element 并在 mem_var_search 里
 *                逐个确认, crc / checksum 降为粗筛。代价: 每项多 20 字节,
 *                3KB 容量下项数上限约减半; 容量不足只是少缓存几项, 不影响正确性。
 *   [已修] 2 —— mem_var_get 现按表项实际 var.len 截断, 并补了入参判空。
 *
 *   [已修] 3 —— 容量判断用无符号比较; mem_var_init 没被调用过时 total_mem_size
 *                为 0, 判断恒真, 缓存永远不生效(不会崩, 只是白跑)。
 *                -> "没初始化"单独拎出来判, 与"真的满了"可区分, 且都带 debug 提示。
 *   [已修] 4 —— 两个返回码与 errno 惯例【正好相反】(容量不足给 -EFAULT
 *                "Bad address", malloc 失败给 -EINVAL"Invalid argument")。
 *                -> 两种都是内存不足, 统一成 -ENOMEM。已核过 10 个调用点【全部
 *                   忽略返回值】, 所以这次接口语义修正不影响现有行为。
 *   [已修] 5 —— mem_var_del 删的是实参而非遍历到的 p(此时两者相等, 行为正确,
 *                只是写法容易误导)。-> 统一用 p。
 *   [已修] 6 —— 末尾两个 ASSERT 不停机时仍会把 hits 清零。-> 链表此刻确实已空,
 *                计数对不上只说明中途有过不走 add/del 的增减, 索性把 items 与
 *                use_mem_size 一并归零, 免得下次 add 的容量判断用到错值。
 *
 * 【注意】文件头"两处 ASSERT 必须落在 140 / 141"那条【已随加固失效】:
 * mem_var_get 的加固在它们前面加了 10 行, 现已漂到 150 / 151。
 * 这是【故意不用 #line 拨回】的 —— 源码已改, ASSERT 就该打印真实行号。
 *
 *
 * 1) 【缓存命中只比对 CRC16 + checksum, 不保存也不比对原始键值】。
 *    mem_var_search 找到 crc 与 checksum 都相等的项就直接返回, 而表项里
 *    (struct mem_var_element)只有 crc / checksum / len / buf, 五个键值根本没存。
 *    也就是说一旦两组不同的 (index,type,id,page,prj) 撞上同一对校验值,
 *    就会【返回错误的资源】, 而且无法察觉。要修得把键值一并存进表项再比对。
 *
 * 2) mem_var_get 【不校验 len】: 调用方传进来的 len 与表项里的 var.len 可能
 *    不一致, 直接 memcpy(buf, var->var.buf, len) —— 传大了就越界读表项、
 *    越界写调用方缓冲。表项里明明存了 len 却不用。
 *
 * 3) mem_var_add 的容量判断 (use_mem_size + sizeof + len) > total_mem_size
 *    用的是【无符号比较】(u32)。若 mem_var_init 没被调用过, total_mem_size
 *    为 0, 判断恒真, 每次都走"内存不够"返回 -EFAULT —— 不会崩, 但缓存永远不生效,
 *    且只有开了 debug 才有一行提示。
 *
 * 4) 两个返回码用得不对: 容量不足返回 -EFAULT(Bad address), malloc 失败返回
 *    -EINVAL(Invalid argument), 语义与 errno 惯例相反, 调用方按 errno 判断会误解。
 *
 * 5) mem_var_del 先遍历确认 p == var 再删, 但删的是【实参 var】而不是遍历到的 p。
 *    两者此时相等, 行为正确, 只是写法容易让人误以为可以传入不在链表里的指针。
 *
 * 6) mem_var_free 末尾的两个 ASSERT 在 config_asser 为假时只调 cpu_assert 不停机,
 *    随后仍会把 hits 清零 —— 计数对不上时不会中断, 只是继续跑。
 */
