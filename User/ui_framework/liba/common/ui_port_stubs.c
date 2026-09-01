/**
 * @file    ui_port_stubs.c
 * @brief   本移植不实现的功能的落地点
 *
 * 分两类:
 *   1. 蓝牙音箱业务侧接口(背光时长参数、亮度档位、软关机) —— 与 UI 显示
 *      无关, 给出安全默认值;
 *   2. Flash 直存(歌词索引回写) —— 本移植资源在 FATFS 上, 不做直存,
 *      一律返回失败, 让框架如实放弃而不是写坏数据。
 *
 * 还有原厂【自己就缺定义】的几个符号也在这里补上, 见文件末尾。
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
 *  并经 jl_fs.h 拿到 sdfile_* 的声明, 而那个文件受等价性锁保护改不得。
 *  歌词零调用时链接器会把这几个连 lyrics.o 一起丢弃, 不占 code。
 * ==================================================================== */

#if (UI_PORT_LYRICS_FLASH_SAVE_ENABLE == 0)

/*
 * 【失败语义必须看调用方怎么判】lyrics.c:104 写的是
 *
 *     if (!sfc_erase(SECTOR_ERASER, addr)) {
 *         return 0;                     // 放弃 flash 直存
 *     }
 *
 * 即【返回 0 = 失败、非 0 = 成功】—— 与一般的 "0 为成功" 约定相反。
 *
 * 原先这里 return 1 并注成"非 0 表示失败", 把语义弄反了: 那等于
 * 告诉歌词模块"擦除成功了", 于是它继续往下走 sfc_write(返回值在
 * lyrics.c:120 根本没检查), 最后到 lyrics.c:133 把 lrc_flash_addr 当指针
 * 解引用 —— 而那个地址恒为 0(sclust 拿不到), 即空指针。
 *
 * 现在返回 0, 歌词模块会在第一个擦除就干净放弃。
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
 * @note 原先是恒等返回(return addr)。改成恒 0 是为了语义明确:
 *       本移植没有"flash 物理地址"这个概念, 恒等映射会让调用方以为
 *       拿到了一个看似合法的地址。lyrics.c:564 靠 `if (!lrc_flash_addr)`
 *       判空, 返回 0 正好让它一直当成"还没拿到"。
 */
u32 sdfile_cpu_addr2flash_addr(u32 addr)
{
    (void)addr;
    return 0;
}

/** @brief flash 物理地址 -> CPU 可寻址指针(XIP)。本移植无此能力, 恒 0 */
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
 *  三、原厂就缺定义的符号
 *
 *  这些在 703 原工程里【全工程都没有定义】, 之所以能链接过, 是因为引用
 *  它们的代码路径在那个配置下是死代码, 被 LTO 整个丢掉了。
 *  Keil 默认不做跨模块的激进消除, 所以必须补上真实符号, 否则链接失败。
 *  详见 ui_framework/移植接口清单.md 第 8 节。
 * ==================================================================== */

/* ---- 泰语支持(liba/font/font_other_language.c) --------------------------
 * 靠 lange_info_table 恒为 NULL 被消除。本移植不支持泰语。 */

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
 * 其封装 _norflash_read_watch 在原工程里零调用者。
 * 本移植没有直连 norflash, 一律返回失败。 */

int norflash_hardware_read_watch(u8 *buf, u32 addr, u32 len, u8 wait)
{
    (void)buf;
    (void)addr;
    (void)len;
    (void)wait;
    return -1;
}
