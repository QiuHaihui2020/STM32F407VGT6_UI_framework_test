/**
 * @file    jl_app_stub.h
 * @brief   原厂应用侧头文件的占位替代
 *
 * 框架的 platform/ 几个文件 include 了 703 音箱工程的应用侧头:
 *   app_main.h / app_task.h / key_event_deal.h / dev_manager.h /
 *   rcsp_task.h / btstack/avctp_user.h / lua/lua.h / clock_cfg.h /
 *   ui/ui_page_switch.h / ui/ui_sys_param.h
 * 这些属于蓝牙音箱业务(A2DP/AVRCP、RCSP 私有协议、Lua 脚本 UI、设备管理),
 * 与 UI 显示无关, 在本移植里【一律不实现】。
 *
 * 本文件只给出框架编译期确实要用到的那几个声明。真正被调用到的运行期
 * 实现放在 port/ui_port_stubs.c, 全部是安全的空动作 / 返回失败。
 */
#ifndef __JL_APP_STUB_H__
#define __JL_APP_STUB_H__

#include "ui_port_config.h"
#include "jl_typedef.h"
#include "jl_os_api.h"

/* @note 这里【有意不定义】APP_CORE_TASK_NAME 与 KEY_NULL ——
 *       实测框架 39 个 .c 一处都没用到它们, 而工程 User/apps/apps.h 里
 *       KEY_NULL 是个枚举值(0xFFFF)。若这里再定义成宏, 两个头一起被包含时
 *       会把那句枚举展开成 "0xff = 0xFFFF", 直接编译报错。
 *       需要按键键值时请用 apps.h 的定义。 */

/* ---- 背光时间参数(原 ui/ui_sys_param.h) -----------------------------
 * 原厂从 VM(掉电保存区)读用户设置。本移植返回固定值; 需要可配时改成
 * 从自己的参数存储里读。 */
u16 get_backlight_time(void);
u16 get_backlight_brightness(void);
void set_backlight_time(u16 time);
void set_backlight_brightness(u16 brightness);

/* ---- 亮度档位 --------------------------------------------------------
 * lcd_drv_backlight_ctrl 在 PWM 调光模式下会调它。
 * 本移植 TCFG_BACKLIGHT_PWM_MODE=0, 这条路径编译不到, 留声明兜底。 */
int get_light_level(void);

/* ---- 关机 / 睡眠(原 app_main.h) -------------------------------------
 * 框架的自动关机定时器(TCFG_UI_SHUT_DOWN_TIME)超时后会调。
 * 本移植该功能关闭(=0), 留声明兜底。 */
void sys_enter_soft_poweroff(void *priv);


/* ---- 低功耗目标注册(原 driver/power/power_manage.h) -----------------
 * lcd_ui_api.c 用 REGISTER_LP_TARGET 把"背光是否空闲"登记给杰理的低功耗
 * 管理器, 让系统在背光亮着时不进休眠。
 *
 * STM32 侧没有对应的低功耗框架, 这里定义成一个【普通的未使用全局变量】:
 * 原厂用 sec(.lp_target) 段收集, 本移植 sec() 是空宏, 登记不会生效 ——
 * 这是有意的, 因为没有消费方。将来接自己的低功耗管理时, 直接引用
 * lcd_backlight_lp_target 这个符号即可。
 */
typedef u8 (*idle_handler_t)(void);

struct lp_target {
    char *name;
    idle_handler_t is_idle;
};

#define REGISTER_LP_TARGET(target)  const struct lp_target target

#endif /* __JL_APP_STUB_H__ */
