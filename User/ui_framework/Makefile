# ============================================================================
# 点阵屏 UI 框架 —— GCC/Make 工程的 include 片段
#
# 顶层 Makefile 里一行 include 即可:
#     include apps/common/ui/ui_framework/Makefile
#
# 注意: 必须 include 在顶层的 c_SRC_FILES := 与 INCLUDES := 【之后】(本文件用 +=),
#       杰理 703 工程放在 c_OBJS := 那一行之前。
#
# 移植到别的工程时, 把整个 ui_framework/ 目录拷过去, 顶层加这一行 include,
# 并按实际存放位置改下面的 UI_FW 变量。
#
# ⚠ 本工程(STM32F407 + Keil)【不读本文件】—— Keil 用 MDK-ARM/*.uvprojx 里的
#   文件列表。本文件是给 GCC/Make 工程用的, 同时也充当"这棵树由哪些文件组成"
#   的清单。改了目录结构记得两边都更新。
# ============================================================================

# ui_framework 在你的工程里的位置(相对顶层 Makefile)。
# 杰理 703 SDK 的惯例位置是 apps/common/ui/ui_framework。
UI_FW ?= apps/common/ui/ui_framework


# ---- 头文件路径 ------------------------------------------------------------
# include/ 目录只暴露 common/ font/ res/ ui/ ui_draw/ ui_vm/ 六个前缀,
# 与工程里其它 -I 根【零重名】(已逐一比对 interface/ui、apps/soundbox/ui、
# apps/soundbox/include/ui、apps/common/ui 四处的 ui/ 目录, 无同名文件),
# 所以放在 INCLUDES 表尾也安全。
#
# ⚠ 曾经踩过的坑: 这棵树原本是 include/ui/{includes.h,font,res,ui,ui_vm},
#   需要 -I include/ui, 而那个根目录里有个【裸 includes.h】——
#   只要它在搜索序里靠前, 就会把别处的 #include "includes.h" 抢过去
#   (cpu/br27/power/power_app.c 因此拿不到 system/init.h)。
#   现在把 includes.h 挪进 ui/ 子目录、其余上提一级, 该 -I 根不复存在, 问题消失。
#
# 【别写成 CFLAGS +=】顶层编译命令是 $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES),
#   写进 CFLAGS 会让这两条排到整条命令最前面, 优先级最高, 又会引发上面那类抢占。
INCLUDES += \
	-I$(UI_FW)/config \
	-I$(UI_FW)/include \
	-I$(UI_FW)/include/common \
	-I$(UI_FW)/include/font \
	-I$(UI_FW)/include/res \
	-I$(UI_FW)/include/ui \
	-I$(UI_FW)/include/ui/cpu/br27 \
	-I$(UI_FW)/include/ui/cpu/br27/asm \
	-I$(UI_FW)/include/ui_draw \
	-I$(UI_FW)/include/ui_vm \
	-I$(UI_FW)/port/bsp \
	-I$(UI_FW)/port/bsp/stm32f4 \
	-I$(UI_FW)/port/res \
	-I$(UI_FW)/port/rtos


# ============================================================================
#  一、框架核心 —— 与 MCU / 文件系统无关, 所有平台都要编
# ============================================================================

# ---- UI 引擎: 任务与消息队列、推屏调度、资源管理、单色绘制原语 -------------
c_SRC_FILES += \
	$(UI_FW)/lcd_drive/middle/lcd_ui_api.c \
	$(UI_FW)/lcd_drive/middle/ui_platform.c \
	$(UI_FW)/lcd_drive/middle/ui_pushScreen_manager.c \
	$(UI_FW)/lcd_drive/middle/ui_resources_manager.c \
	$(UI_FW)/lcd_drive/middle/ui_synthesis_oled.c
# watch_bgp.c(彩屏手表表盘背景) 本移植没有该文件 —— 点阵屏下
# UI_WATCH_RES_ENABLE=0, 调用点被整块编译掉。从杰理 SDK 拷回来时再加这一行:
#	$(UI_FW)/lcd_drive/middle/watch_bgp.c


# ---- 屏驱 ------------------------------------------------------------------
# 一块屏一个文件, 靠 REGISTER_LCD_DEVICE() 注册。换屏就换这一行。
c_SRC_FILES += \
	$(UI_FW)/lcd_drive/oled_spi_ssd1306_128x64.c


# ---- 控件树 + 脏矩形重绘引擎 + 控件(由 ui_dot.a 还原) ----------------------
c_SRC_FILES += \
	$(UI_FW)/liba/ui_dot/ui_p.c \
	$(UI_FW)/liba/ui_dot/ui_pic.c \
	$(UI_FW)/liba/ui_dot/ui_battery.c \
	$(UI_FW)/liba/ui_dot/ui_time.c \
	$(UI_FW)/liba/ui_dot/ui_number.c \
	$(UI_FW)/liba/ui_dot/ui_slider.c \
	$(UI_FW)/liba/ui_dot/ui_slider_vert.c \
	$(UI_FW)/liba/ui_dot/ui_text.c \
	$(UI_FW)/liba/ui_dot/layout.c \
	$(UI_FW)/liba/ui_dot/window.c \
	$(UI_FW)/liba/ui_dot/layer.c \
	$(UI_FW)/liba/ui_dot/ui_rotate.c \
	$(UI_FW)/liba/ui_dot/ui_core_api.c \
	$(UI_FW)/liba/ui_dot/lyrics.c \
	$(UI_FW)/liba/ui_dot/ui_core_dot.c \
	$(UI_FW)/liba/ui_dot/ui_grid.c


# ---- 字体点阵取模 + 多语言编码转换(由 font.a 还原) --------------------------
c_SRC_FILES += \
	$(UI_FW)/liba/font/utf8_2_unicode.c \
	$(UI_FW)/liba/font/font_sdfs.c \
	$(UI_FW)/liba/font/font_ascii.c \
	$(UI_FW)/liba/font/font_textout.c \
	$(UI_FW)/liba/font/font_gbk.c \
	$(UI_FW)/liba/font/font_big5.c \
	$(UI_FW)/liba/font/font_ksc.c \
	$(UI_FW)/liba/font/font_sjis.c \
	$(UI_FW)/liba/font/font_other_language.c


# ---- 圆弧 / 图像解码(由 ui_draw.a 还原) ------------------------------------
c_SRC_FILES += \
	$(UI_FW)/liba/ui_draw/image_process.c \
	$(UI_FW)/liba/ui_draw/ui_circle.c


# ---- 资源文件读取 / 字模 / 解压 / 缓存表(由 res.a 还原) --------------------
c_SRC_FILES += \
	$(UI_FW)/liba/res/ascii.c \
	$(UI_FW)/liba/res/mem_var.c \
	$(UI_FW)/liba/res/quicklz.c \
	$(UI_FW)/liba/res/resfile.c \
	$(UI_FW)/liba/res/rle.c


# ---- 杂项库函数(由原厂 bitcode 还原) + 移植存根 ----------------------------
# jl_ascii/jl_crc  原厂在 btsdk 库里, 本移植还原成源码
# jl_debug         分级日志与断言的落点
# ui_config        CRC/ASCII 之外的零碎配置
# ui_port_stubs    本移植不实现的功能(背光时长/亮度/软关机/flash 直存)
#
# ⚠ 杰理 703 SDK 里前四个由 btsdk 库提供, 列进来会【重复定义】。
#   在那个工程里注释掉这一节, 只保留 ui_port_stubs.c。
c_SRC_FILES += \
	$(UI_FW)/liba/common/jl_ascii.c \
	$(UI_FW)/liba/common/jl_crc.c \
	$(UI_FW)/liba/common/jl_debug.c \
	$(UI_FW)/liba/common/ui_config.c \
	$(UI_FW)/liba/common/ui_port_stubs.c


# ============================================================================
#  二、移植层 —— 换 MCU / 换文件系统时【只动这一节】
# ============================================================================

# ---- 控件 / 风格 / 推屏接口的显式注册表 ------------------------------------
# 原厂靠 GNU ld 的链接脚本收集 .control_ops / .ui_style / .lcd_if_info 三个段。
# armlink 没有 PROVIDE 造不出那类段边界符号, 所以改成显式表。
#
# ⚠ 在【用 GNU ld 的工程】(如杰理 703 SDK)里, 段收集本来就能用,
#   这个文件是多余的 —— 注释掉本节, 改用原厂的链接脚本。
c_SRC_FILES += \
	$(UI_FW)/config/ui_port_registry.c


# ---- 页面事件响应(风格层) ----------------------------------------------
# 一个页面一个文件, 对应 703 的 apps/soundbox/ui/lcd/STYLE_SOUNDBOX/。
# 每个页面定义自己的 ui_handlers_<页面> 表, 再到
# config/ui_port_registry.c 的 g_ui_handler_table 里登记一行。
c_SRC_FILES += \
	$(UI_FW)/ui_action/music_action.c \
	$(UI_FW)/ui_action/bt_action.c \
	$(UI_FW)/ui_action/system_action.c


# ---- 屏硬件: 控制线拉高拉低 + SPI 数据通道 ---------------------------------
# 接口在 port/bsp/ui_lcd_if.h(MCU 无关), 板级接线与实现在 port/bsp/<mcu>/。
# 换 MCU: 照 ui_lcd_stm32f4.c 写一个 ui_lcd_<芯片>.c, 换掉下面这一行。
c_SRC_FILES += \
	$(UI_FW)/port/bsp/stm32f4/ui_lcd_stm32f4.c


# ---- 资源读取: 介质无关的 core + 介质相关的后端 ----------------------------
# ui_res_core.c   句柄池 / 校验 / seek 换算 / 挂载幂等 —— 换介质【不用改】
# ui_res_fatfs.c  FatFs 后端, 只有 8 个函数
#
# 换文件系统: 照 ui_res_fatfs.c 写一个新后端, 换掉第二行;
#             并在 port/res/ui_res_backend.h 的 CTX_SIZE 表里加一行、
#             改 config/ui_port_config.h 的 UI_RES_BACKEND_* 宏。
c_SRC_FILES += \
	$(UI_FW)/liba/res/ui_res_core.c \
	$(UI_FW)/port/res/fatfs/ui_res_fatfs.c


# ============================================================================
#  三、BR27 的 IMD【硬件推屏】驱动 —— 本移植不编译
# ============================================================================
# 本工程 OLED 点阵屏走 SPI DMA(lcd_drive/middle/ui_pushScreen_manager.c +
# lcd_drive/oled_spi_ssd1306_128x64.c), 所有 imd_* 调用点都在 #if 0 或
# #if (TCFG_SPI_LCD_ENABLE) 里, 实测最终固件【0 处 IMD 寄存器访问】。
#
# 为什么是不列进来、而不是在源码里加 #if:
#   这三个文件是从 ui_cpu.a 还原来的, 受 cpu/br27/tools/ui_reimpl/verify.sh 的
#   等价性锁保护。源码里加 #if 会让它们编成空 TU, 校验就没东西可比、锁失效 ——
#   而它们烧机测不出, 等价性证明是唯一的正确性保障。不列进来则不影响校验:
#   objs/%.c.o 是通用模式规则, 仍可单独 make 出 .o 来比对。
#
# 改回彩屏时: 去掉下面三行的注释, 并去掉 lcd_drive/middle/ui_platform.c 里
# imd_wait() 外面那个 #if (TCFG_SPI_LCD_ENABLE)。
# 注: 这三个文件本移植的目录树里【没有】, 要从杰理 SDK 拷回来。
# c_SRC_FILES += \
# 	$(UI_FW)/ui_cpu/imd.c \
# 	$(UI_FW)/ui_cpu/imd_spi.c \
# 	$(UI_FW)/ui_cpu/res_norflash.c
