/**
 * @file    ui_port_stubs.c
 * @brief   本工程不实现的功能的落地点
 *
 * 分两类:
 *   1. 蓝牙音箱业务侧接口(背光时长参数、亮度档位、软关机) —— 与 UI 显示
 *      无关, 给出安全默认值;
 *   2. Flash 直存(歌词索引回写) —— 本工程资源在 FATFS 上, 不做直存,
 *      一律返回失败, 让框架如实放弃而不是写坏数据。
 *
 * 还有几个【框架引用了但没人定义】的符号也在这里补上, 见文件末尾。
 */
#include "jl_typedef.h"
#include "jl_app_stub.h"
#include "ui_port_config.h"    /* UI_PORT_LYRICS_FLASH_SAVE_ENABLE */



/* ==================================================================== *
 *  二、Flash 直存(仅歌词时间标签索引回写)
 *
 *  开关在 config/ui_port_config.h 的 UI_PORT_LYRICS_FLASH_SAVE_ENABLE,
 *  默认 0 = 不支持(那里写了为什么)。
 *
 *  四个符号必须留着: liba/ui_dot/lyrics.c 里 extern 了 sfc_erase/sfc_write,
 *  并经 jl_fs.h 拿到 sdfile_* 的声明 —— 少一个就是链接错误。
 *  歌词零调用时链接器会把这几个连 lyrics.o 一起丢弃, 不占 code。
 * ==================================================================== */

#if (UI_PORT_LYRICS_FLASH_SAVE_ENABLE == 0)

/*
 * 【失败语义要看调用方怎么判】lyrics.c 的 lrc_analysis_in_flash() 里写的是
 *
 *     if (!sfc_erase(SECTOR_ERASER, addr)) {
 *         return 0;                     // 放弃 flash 直存
 *     }
 *
 * 即【返回 0 = 失败、非 0 = 成功】—— 与一般的 "0 为成功" 约定正好相反。
 *
 * 所以这里【必须返回 0】。返回非 0 等于告诉歌词模块"擦除成功了", 于是它
 * 继续往下走 sfc_write(那个返回值调用方根本没检查), 再往下就把
 * lrc_flash_addr 当指针解引用 —— 而这条路上那个地址恒为 0(簇号拿不到),
 * 也就是空指针。
 *
 * 返回 0, 歌词模块会在第一个擦除处就干净放弃。
 */
u8 sfc_erase(u32 cmd, u32 addr)
{
    (void)cmd;
    (void)addr;
    return 0;       /* 0 = 失败, 调用方立即 return 0 放弃直存 */
}

/** @return 实际写入字节数; 0 = 失败。走不到这里(擦除已先失败) */
u32 sfc_write(u8 *buf, u32 addr, u32 len)
{
    (void)buf;
    (void)addr;
    (void)len;
    return 0;
}

/**
 * @brief 文件起始簇号 -> flash 物理地址
 * @return 恒 0 = 拿不到地址
 * @note 这里【不做恒等映射】(return addr), 而是恒返回 0: 本工程没有
 *       "flash 物理地址"这个概念, 恒等映射会让调用方以为拿到了一个
 *       看似合法的地址。lrc_param_init() 靠 `if (!lrc_flash_addr)` 判空,
 *       返回 0 正好让它一直当成"还没拿到"。
 */
u32 sdfile_cpu_addr2flash_addr(u32 addr)
{
    (void)addr;
    return 0;
}

/** @brief flash 物理地址 -> CPU 可寻址指针(XIP)。本工程无此能力, 恒 0 */
u32 sdfile_flash_addr2cpu_addr(u32 addr)
{
    (void)addr;
    return 0;
}

#else
#error "UI_PORT_LYRICS_FLASH_SAVE_ENABLE=1 需要自己实现这四个函数: \
sfc_erase/sfc_write 直接擦写 flash(注意不要写坏 FATFS 卷), \
sdfile_cpu_addr2flash_addr 要能从文件拿到物理地址, \
sdfile_flash_addr2cpu_addr 要求 flash 可 XIP 寻址。详见 ui_port_config.h 里的说明。"
#endif /* UI_PORT_LYRICS_FLASH_SAVE_ENABLE */


/* ==================================================================== *
 *  三、框架引用了但缺少定义的符号
 *
 *  这几个符号框架里有引用、却没有任何一处定义。开启全局优化(LTO)的工程
 *  能链过, 是因为引用它们的分支在那种配置下是死代码、被整块消除了。
 *  Keil 默认不做跨模块的激进消除, 所以必须补上真实符号, 否则链接失败。
 * ==================================================================== */

/* ---- 泰语支持(liba/font/font_other_language.c) --------------------------
 * 靠 lange_info_table 恒为 NULL 被消除。本工程不支持泰语。 */

int GetThaiLanguageCharacterData(void *info, u16 unicode, u8 *buf, int len)
{
    (void)info;
    (void)unicode;
    (void)buf;
    (void)len;
    return 0;       /* 0 = 取不到字模, 调用方会跳过该字符 */
}

int IsThaiOneWord_W(u16 *str, int len)
{
    (void)str;
    (void)len;
    return 0;
}

int ThaiLanguagecompose(u16 *in, int in_len, u16 *out, int out_len)
{
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return 0;
}

/* ---- norflash 加速读(liba/res/resfile.c) --------------------------------
 * 其封装 _norflash_read_watch 在本工程里零调用者。
 * 本工程没有直连 norflash, 一律返回失败。 */

int norflash_hardware_read_watch(u8 *buf, u32 addr, u32 len, u8 wait)
{
    (void)buf;
    (void)addr;
    (void)len;
    (void)wait;
    return -1;
}
