/*
 * res_norflash.c —— 资源文件从 NOR flash 读取的适配层(占位实现)
 *
 * 【来源】从 cpu/br27/liba/ui_cpu.a 的 res_norflash.c.o 还原。该库交付的是
 *   LLVM bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/res_norflash.ll
 *     原始路径: btsdk/lib/utils/ui/ui_cpu/br27/res_norflash.c
 *
 * 【还原依据】函数原始行号(DISubprogram):
 *     sfc_spi_read@29  norflash_hardware_read@36  norflash_hardware_read_watch@45
 *   形参名与类型取自 DWARF。本模块无 ASSERT、无全局变量、无字符串常量。
 *
 * 【这个模块是一套占位实现】三个函数全部无条件 `return 0`, 调用结果一律丢弃:
 *   - sfc_spi_read 是【弱符号】且函数体为空, 由板级的
 *     apps/common/device/storage_device/norflash/norflash_sfc.c 去覆盖;
 *     这里留一份空实现是为了在没有 NOR flash 驱动时也能链接过。
 *   - 另外两个只是把调用转下去, 返回值不看、wait 参数不用。
 *   本工程【没有任何代码调用它们】(sdk.lst 里查无此符号), 属死代码。
 *
 * 【段属性】sfc_spi_read 在 .res_norflash.text; 另外两个在 .ui_ram
 *   (要在 RAM 里执行, 用 rect.h 的 AT_UI_RAM 宏标注), 见 ref IR 的 section 属性。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma code_seg(".res_norflash.text")
#endif

#include "jl_typedef.h"
#include "jl_rect.h"

int sfc_spi_read(u32 addr, void *buf, u32 len);
int norflash_hardware_read(u8 *buf, u32 addr, u32 len, u8 wait);
int norflash_hardware_read_watch(u8 *buf, u32 addr, u32 len, u8 wait);

/*
 * @note 弱符号占位: 板级有真正的 SFC 驱动时会覆盖掉它。
 */
__attribute__((weak))
int sfc_spi_read(u32 addr, void *buf, u32 len)
{
    return 0;
}

AT_UI_RAM
int norflash_hardware_read(u8 *buf, u32 addr, u32 len, u8 wait)
{
    /* 加固: 原库不检查入参。 */
    if (buf == NULL || len == 0) {
        return -1;
    }

    /* 加固: 形参未被使用(下层 sfc_spi_read 没有这个概念), 显式标明。
     * 参考 IR 里 norflash_hardware_read_watch 传下来的第 4 个实参已被优化成
     * undef, 正是因为被调方不用它。 */
    (void)wait;

    /* 加固: 原库【丢弃下层返回值并无条件 return 0】, 读失败与读成功对调用方
     * 完全不可区分。这里如实透传。 */
    return sfc_spi_read(addr, buf, len);
}

AT_UI_RAM
int norflash_hardware_read_watch(u8 *buf, u32 addr, u32 len, u8 wait)
{
    /* 加固: 同上 —— 入参检查由被调方做, 这里只负责如实透传返回值。 */
    return norflash_hardware_read(buf, addr, len, wait);
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— 三个函数丢弃下层返回值并无条件 return 0 -> 改为如实透传。
 *   [已修] 2 —— wait 形参从未使用 -> (void) 显式标明。
 *   [已修] 3 —— 不检查 buf / len -> 已补。
 *
 * 1) 三个函数都【丢弃下层返回值并无条件返回 0】, 也就是说读失败与读成功
 *    对调用方完全不可区分。
 * 2) wait 参数从头到尾没被使用(参考 IR 里 norflash_hardware_read_watch
 *    传给下层的第 4 个实参已经被优化成 undef, 正是因为被调方不用它)。
 * 3) 两个 _read 函数不检查 buf 是否为 NULL、len 是否为 0。
 */
