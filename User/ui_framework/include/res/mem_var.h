#ifndef MEM_VAR_H
#define MEM_VAR_H

#include "jl_typedef.h"
#include "jl_list.h"

struct mem_var_element {
    /*
     * 缓存键原样存在表项里, 供 mem_var_search 逐个确认 —— 【不能只靠下面的
     * crc + checksum 判命中】: 两组不同的 (index,type,id,page,prj) 一旦撞上
     * 同一对校验值, 就会返回错误的资源, 且调用方无从察觉。
     *
     * 风险比看上去大: checksum 是逐字节累加, 【对字节顺序不敏感】—— index /
     * page / prj 这类小整数字段互换位置时 checksum 必然相同, 只剩 CRC16 那
     * 16 位防线。
     *
     * 代价是每项多 20 字节(sizeof(struct mem_var) 36)。crc / checksum 留作
     * 粗筛(比 5 个 u32 的比较快), 不作为唯一判据。
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
