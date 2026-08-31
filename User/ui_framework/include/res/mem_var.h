#ifndef MEM_VAR_H
#define MEM_VAR_H

#include "jl_typedef.h"
#include "jl_list.h"

struct mem_var_element {
    /*
     * 加固: 原库【不保存原始键】, 只存下面的 crc + checksum, 命中判断就是
     * "两个校验值都相等"。两组不同的 (index,type,id,page,prj) 一旦撞上同一对
     * 校验值, 就会返回错误的资源, 且调用方无从察觉。
     *
     * 风险比看上去大: checksum 是逐字节累加, 【对字节顺序不敏感】—— index /
     * page / prj 这类小整数字段互换位置时 checksum 必然相同, 实际防线只剩
     * CRC16 那 16 位。
     *
     * 这五个字段就是为了让 mem_var_search 能逐键确认而加的, 每项多 20 字节
     * (sizeof(struct mem_var) 16 -> 36)。crc / checksum 保留作粗筛(比 5 个
     * u32 的比较快), 不再是唯一判据。
     */
    u32 index;
    u32 type;
    u32 id;
    u32 page;
    u32 prj;

    u16 crc;
    u16 checksum;
    u16 len;
    u8 buf[0];
};

struct mem_var {
    struct list_head head;
    struct mem_var_element var;
};

struct mem_var_head {
    struct list_head head;
    int total_mem_size;
    int items;
    int use_mem_size;
    int hits;
    u8 debug;
};

extern struct mem_var_head var_list;

void mem_var_init(u32 size, u8 debug);
int mem_var_add(u32 index, u32 type, u32 id, u32 page, u32 prj, u8 *buf, u16 len);
void mem_var_free();
int mem_var_del(struct mem_var *var);
void mem_var_get(struct mem_var *var, u8 *buf, u16 len);
struct mem_var *mem_var_search(u32 index, u32 type, u32 id, u32 page, u32 prj);
void mem_var_stat();

#endif
