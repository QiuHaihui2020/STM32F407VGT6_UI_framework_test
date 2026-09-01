# ============================================================================
# 点阵屏 UI 框架
#
# 顶层 Makefile 里一行 include 即可, 不需要引用任何变量:
#     include apps/common/ui/ui_framework/Makefile
#
# 注意: 必须 include 在顶层的 c_SRC_FILES := 与 INCLUDES := 【之后】(本文件用 +=),
#       本工程放在 c_OBJS := 那一行之前。
#
# 移植到别的工程时, 把整个 ui_framework/ 目录拷过去, 顶层加这一行 include 就行。
# ============================================================================


# ---- 头文件路径 ------------------------------------------------------------
# include/ 目录只暴露 font/ res/ ui/ ui_vm/ 四个前缀, 与工程里其它 -I 根【零重名】
# (已逐一比对 interface/ui、apps/soundbox/ui、apps/soundbox/include/ui、
#  apps/common/ui 四处的 ui/ 目录, 无同名文件), 所以放在 INCLUDES 表尾也安全。
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
	-Iapps/common/ui/ui_framework/include \
	-Iapps/common/ui/ui_framework/include/ui/cpu/br27


# ---- 平台适配层 ------------------------------------------------------------
# UI 任务/消息队列、推屏调度、资源管理、单色绘制原语
c_SRC_FILES += \
	apps/common/ui/ui_framework/middle/lcd_ui_api.c \
	apps/common/ui/ui_framework/middle/ui_platform.c \
	apps/common/ui/ui_framework/middle/ui_pushScreen_manager.c \
	apps/common/ui/ui_framework/middle/ui_resources_manager.c \
	apps/common/ui/ui_framework/middle/ui_synthesis_oled.c \
	apps/common/ui/ui_framework/middle/watch_bgp.c


# ---- 屏驱 ------------------------------------------------------------------
# 一块屏一个文件, 靠 REGISTER_LCD_DEVICE() 注册
c_SRC_FILES += \
	apps/common/ui/ui_framework/lcd_drive/oled_spi_ssd1306_128x64.c


# ---- 控件树 + 脏矩形重绘引擎 + 12 个控件(由 ui_dot.a 还原) ------------------
c_SRC_FILES += \
	apps/common/ui/ui_framework/ui_dot/ui_p.c \
	apps/common/ui/ui_framework/ui_dot/ui_pic.c \
	apps/common/ui/ui_framework/ui_dot/ui_battery.c \
	apps/common/ui/ui_framework/ui_dot/ui_time.c \
	apps/common/ui/ui_framework/ui_dot/ui_number.c \
	apps/common/ui/ui_framework/ui_dot/ui_slider.c \
	apps/common/ui/ui_framework/ui_dot/ui_slider_vert.c \
	apps/common/ui/ui_framework/ui_dot/ui_text.c \
	apps/common/ui/ui_framework/ui_dot/layout.c \
	apps/common/ui/ui_framework/ui_dot/window.c \
	apps/common/ui/ui_framework/ui_dot/layer.c \
	apps/common/ui/ui_framework/ui_dot/ui_rotate.c \
	apps/common/ui/ui_framework/ui_dot/ui_core_api.c \
	apps/common/ui/ui_framework/ui_dot/lyrics.c \
	apps/common/ui/ui_framework/ui_dot/ui_core_dot.c \
	apps/common/ui/ui_framework/ui_dot/ui_grid.c


# ---- 字体点阵取模 + 多语言编码转换(由 font.a 还原) --------------------------
c_SRC_FILES += \
	apps/common/ui/ui_framework/font/utf8_2_unicode.c \
	apps/common/ui/ui_framework/font/font_sdfs.c \
	apps/common/ui/ui_framework/font/font_ascii.c \
	apps/common/ui/ui_framework/font/font_textout.c \
	apps/common/ui/ui_framework/font/font_gbk.c \
	apps/common/ui/ui_framework/font/font_big5.c \
	apps/common/ui/ui_framework/font/font_ksc.c \
	apps/common/ui/ui_framework/font/font_sjis.c \
	apps/common/ui/ui_framework/font/font_other_language.c


# ---- 圆弧 / 图像解码(由 ui_draw.a 还原) ------------------------------------
c_SRC_FILES += \
	apps/common/ui/ui_framework/ui_draw/image_process.c \
	apps/common/ui/ui_framework/ui_draw/ui_circle.c


# ---- 资源文件读取 / 字模 / 解压 / 缓存表(由 res.a 还原) --------------------
c_SRC_FILES += \
	apps/common/ui/ui_framework/res/ascii.c \
	apps/common/ui/ui_framework/res/mem_var.c \
	apps/common/ui/ui_framework/res/quicklz.c \
	apps/common/ui/ui_framework/res/resfile.c \
	apps/common/ui/ui_framework/res/rle.c


# ---- ui_cpu/ —— BR27 的 IMD【硬件推屏】驱动, 本工程不编译 ------------------
# 本工程 OLED 点阵屏走 SPI DMA(middle/ui_pushScreen_manager.c +
# lcd_drive/oled_spi_ssd1306_128x64.c), 所有 imd_* 调用点都在 #if 0 或
# #if (TCFG_SPI_LCD_ENABLE) 里, 实测最终固件【0 处 IMD 寄存器访问】。
#
# 为什么是不列进来、而不是在源码里加 #if:
#   这三个文件是从 ui_cpu.a 还原来的, 受 cpu/br27/tools/ui_reimpl/verify.sh 的
#   等价性锁保护。源码里加 #if 会让它们编成空 TU, 校验就没东西可比、锁失效 ——
#   而它们烧机测不出, 等价性证明是唯一的正确性保障。不列进来则不影响校验:
#   objs/%.c.o 是通用模式规则, 仍可单独 make 出 .o 来比对。
#
# 改回彩屏时: 去掉下面三行的注释, 并去掉 middle/ui_platform.c 里
# imd_wait() 外面那个 #if (TCFG_SPI_LCD_ENABLE)。
# c_SRC_FILES += \
# 	apps/common/ui/ui_framework/ui_cpu/imd.c \
# 	apps/common/ui/ui_framework/ui_cpu/imd_spi.c \
# 	apps/common/ui/ui_framework/ui_cpu/res_norflash.c
