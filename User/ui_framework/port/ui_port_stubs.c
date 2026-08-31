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
/* 打开本文件的分级日志。jl_debug.h 的 log_* 是靠这几个宏开关的,
 * 不定义就是空实现 —— port 层是上板排查的关键路径, 必须留着。 */
#define LOG_INFO_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_ERROR_ENABLE
#include "jl_debug.h"
#include "jl_app_stub.h"
#include "jl_fs.h"
#include "ui_port_config.h"

/* ==================================================================== *
 *  一、背光 / 亮度 / 关机
 * ==================================================================== */

/** 背光超时时长(秒)。0 = 不自动熄灭。
 * SSD1306 自发光且无背光引脚, 熄屏靠面板命令(entersleep), 这里返回 0
 * 表示"不做自动熄屏", 由应用自己决定何时调 ui_backlight_close()。 */
u16 get_backlight_time(void)
{
    return 0;
}

void set_backlight_time(u16 time)
{
    /* 原厂写进掉电保存区(VM)。本移植没有参数存储, 忽略。
     * 需要可配时在这里接自己的参数区。 */
    (void)time;
}

/** 亮度。OLED 可以用 0x81 对比度命令调, 但框架这条路径只在
 * TCFG_BACKLIGHT_PWM_MODE != 0 时才走, 本移植是 0。 */
u16 get_backlight_brightness(void)
{
    return 100;
}

void set_backlight_brightness(u16 brightness)
{
    (void)brightness;
}

int get_light_level(void)
{
    return 4;   /* 原厂档位 0~4, 取最高 */
}

void sys_enter_soft_poweroff(void *priv)
{
    /* 自动关机功能(TCFG_UI_SHUT_DOWN_TIME)在本移植里是关的(=0),
     * 所以这里到不了。留个实现避免将来打开时链接失败。 */
    (void)priv;
    log_info("ui: 收到软关机请求, 本移植未实现\r\n");
}


/* ==================================================================== *
 *  二、Flash 直存(仅歌词功能)
 * ==================================================================== */

u32 sdfile_cpu_addr2flash_addr(u32 addr)
{
    /* 杰理把 norflash 映射进 CPU 地址空间, 所以有这对换算。
     * 本移植资源走 FATFS, 没有地址映射概念, 恒等返回。 */
    return addr;
}

u32 sdfile_flash_addr2cpu_addr(u32 addr)
{
    return addr;
}

/**
 * @brief 擦除 flash 扇区
 * @note 签名与 ui_dot/lyrics.c 里的 extern 声明保持一致(u8 返回值)。
 * @return 非 0 表示失败
 */
u8 sfc_erase(u32 cmd, u32 addr)
{
    (void)cmd;
    (void)addr;
    /* 不支持: 返回失败, 框架会放弃写回歌词索引, 改为每次重新解析。
     * 功能上只是慢一点, 不会出错。 */
    return 1;
}

/**
 * @brief 写 flash
 * @note 签名与 lyrics.c 的 extern 声明一致(u32 返回值 = 实际写入长度)。
 * @return 实际写入字节数, 0 表示失败
 */
u32 sfc_write(u8 *buf, u32 addr, u32 len)
{
    (void)buf;
    (void)addr;
    (void)len;
    return 0;
}


/* ==================================================================== *
 *  三、原厂就缺定义的符号
 *
 *  这些在 703 原工程里【全工程都没有定义】, 之所以能链接过, 是因为引用
 *  它们的代码路径在那个配置下是死代码, 被 LTO 整个丢掉了。
 *  Keil 默认不做跨模块的激进消除, 所以必须补上真实符号, 否则链接失败。
 *  详见 ui_framework/移植接口清单.md 第 8 节。
 * ==================================================================== */

/* ---- 泰语支持(font/font_other_language.c) --------------------------
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

/* ---- norflash 加速读(res/resfile.c) --------------------------------
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
