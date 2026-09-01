/**
 * @file    jl_app_stub.h
 * @brief   原厂应用侧头文件的占位替代
 *
 * 框架的 lcd_drive/middle/ 几个文件 include 了 703 音箱工程的应用侧头:
 *   app_main.h / app_task.h / key_event_deal.h / dev_manager.h /
 *   rcsp_task.h / btstack/avctp_user.h / lua/lua.h / clock_cfg.h /
 *   ui/ui_page_switch.h / ui/ui_sys_param.h
 * 这些属于蓝牙音箱业务(A2DP/AVRCP、RCSP 私有协议、Lua 脚本 UI、设备管理),
 * 与 UI 显示无关, 在本移植里【一律不实现】。
 *
 * 本文件只给出框架编译期确实要用到的那几个声明。真正被调用到的运行期
 * 实现放在 liba/common/ui_port_stubs.c, 全部是安全的空动作 / 返回失败。
 */
#ifndef __JL_APP_STUB_H__
#define __JL_APP_STUB_H__

#include "ui_port_config.h"
#include "jl_typedef.h"




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
