/**
 * @file    ui_port_config.h
 * @brief   点阵屏 UI 框架 —— 移植配置总入口
 *
 * 本文件承担原厂 SDK 里 app_config.h + board_config.h 两者的角色。
 * compat/app_config.h 只是转发到这里, 让框架源码里的
 * #include "app_config.h" 原样可用。
 *
 * 改板子 / 改屏 / 改 MCU 时, 优先只改这个文件。
 */
#ifndef __UI_PORT_CONFIG_H__
#define __UI_PORT_CONFIG_H__

/* 本文件【不再 include 任何硬件相关头】。引脚与外设完全封在 port 里:
 *   port/ui_lcd_if.h            框架看得到的唯一硬件边界(纯语义函数)
 *   port/board/ui_board_pins.h  接线     —— 只有 port/lcd/ 下的实现读得到
 *   port/board/ui_board_*.h     外设实例 —— 同上 */

/* ========================================================================
 * 一、功能裁剪开关(对应原厂 app_config.h 的 TCFG_*)
 * ======================================================================== */

#define TCFG_UI_ENABLE                  1   /**< UI 框架总开关 */
#define TCFG_LCD_OLED_ENABLE            1   /**< 使用点阵(单色)屏通路 */
#define TCFG_OLED_SPI_SSD1306_ENABLE    1   /**< 屏驱: SSD1306 128x64 SPI */
#define TCFG_SPI_LCD_ENABLE             0   /**< 彩屏 IMD 通路, 本工程不用 */
#define TCFG_SIMPLE_LCD_ENABLE          0
#define TCFG_LCD_SPI_ST7789V_ENABLE     0

#define TCFG_LUA_ENABLE                 0   /**< Lua 脚本 UI, 本工程不用 */
#define TCFG_VIRFAT_FLASH_ENABLE        0   /**< 虚拟 FAT, 本工程用真 FATFS */
#define TCFG_NOR_FAT                    0
#define TCFG_UI_ENABLE_LEFT_MENU        0
#define TCFG_BACKLIGHT_PWM_MODE         0   /**< 0=GPIO 开关(OLED 无背光) */
#define TCFG_UI_SHUT_DOWN_TIME          0   /**< 0=不自动关机 */
/* SPI 控制器编号原先在这里(TCFG_TFT_LCD_DEV_SPI_HW_NUM=3), 但它是个装饰品:
 * 一路传到 shim 就被 (void)spi 丢掉, 真值硬编码在 HAL 里的 hspi3。
 * 现在挪到 board/ui_board_stm32f4.h 的 UI_BOARD_SPI_BUS_ID, 与句柄、
 * DMA 通路放在一起 —— 换 SPI 时三处在同一个文件里改。 */

/* UI 风格。框架用它选 CONFIG_UI_STYLE 分支, 点阵屏走通用框架分支 */
#define STYLE_JL_LED7                   0
#define STYLE_UI_SIMPLE                 1
#define STYLE_JL_SOUNDBAR               2
#define STYLE_JL02                      3
#define CONFIG_UI_STYLE                 STYLE_JL02

/* 双 buffer: 128x64/8 = 1KB 一帧, F407 RAM 充裕, 但单 buf 整帧推送足够,
 * 且原厂 703 配置就是单 buf(BUF_NUM=1), 保持一致以免行为漂移 */
#define UI_USED_DOUBLE_BUFFER           0

/* 点阵屏标记。lcd_ui_api.c 靠它关掉"卡片滑动"整块代码 ——
 * 那套是彩屏触摸屏的翻页手势(依赖 ui_page_switch 模块与 struct ui_page),
 * SSD1306 无触摸, 框架源码注释里也写明该模式仅彩屏可用。
 * 原厂在编译命令行里定义它, 本移植放在配置头里更直观。 */
#define EXPORT_DOT_UI_ENABLE    1

/* 原厂用 CONFIG_CPU_BR27 选 IMD 硬件推屏。STM32 无 IMD, 一律【不要】定义
 * CONFIG_CPU_BR2x —— 定义了会把 asm/imd.h 的寄存器访问打开 */
/* #define CONFIG_CPU_BR27 */
#define CONFIG_APP_SOUNDBOX             1
/* #define CONFIG_APP_WATCH */
/* #define CONFIG_WATCH_CASE_ENABLE */


/* ========================================================================
 * 二、屏幕参数
 * ======================================================================== */

#define UI_PORT_LCD_WIDTH               128
#define UI_PORT_LCD_HEIGHT              64
/** 单色屏一帧显存字节数: 1bpp, 每 8 行打包成一个 page */
#define UI_PORT_LCD_BUF_SIZE            (UI_PORT_LCD_WIDTH * UI_PORT_LCD_HEIGHT / 8)
#define UI_PORT_LCD_PAGE_NUM            (UI_PORT_LCD_HEIGHT / 8)


/* ========================================================================
 * 三、UI 任务参数
 * ======================================================================== */

#define UI_PORT_TASK_NAME               "ui"
/* 框架递归重绘控件树 + 字模取模, 栈需求偏大。原厂给的是 1024 字(4KB),
 * 这里按 FreeRTOS 的"字"为单位配置, 见 port/ui_port_os.c 的注册表 */
#define UI_PORT_TASK_STACK_WORDS        1024
#define UI_PORT_TASK_PRIORITY           4
#define UI_PORT_TASK_QSIZE              32


/* ========================================================================
 * 四、硬件
 *
 * 【本节已空】—— 引脚、SPI 实例、DMA 通路全部移到 port/board/ 下。
 *
 * 原先这里写的是 GPIOB / GPIO_PIN_6 / DMA1_Stream5 这些【STM32 HAL 符号】,
 * 而本文件被 ui_dot/、common/、middle/ 都 include 了 —— 等于让 MCU 无关的
 * 核心层看见了 STM32。后来改成引脚 token 转发(TCFG_LCD_PIN_*), 虽然不再有
 * 厂商符号, 但框架仍然"知道有引脚这回事"、还要懂 token 的编码约定。
 *
 * 现在框架只调语义函数:
 *     ui_lcd_cs(0)   把片选拉低
 *     ui_lcd_dc(1)   切到数据
 *     ui_lcd_write_block(buf, len, 1)
 * 引脚这个概念到 port 层就终止了, 改接线/换 SPI/换 MCU 都波及不到框架。
 * ======================================================================== */


/* ========================================================================
 * 五、资源文件路径(FATFS)
 *
 * 片内 Flash 的 FAT 盘上的实际布局:
 *
 *     0:/JL/JL.res        图片资源
 *     0:/JL/JL.str        字符串图片
 *     0:/JL/JL.sty        窗口/控件布局
 *     0:/font/ascii.res   ASCII 字库
 *     0:/font/F_GB2312.*  中文字库(需要中文时)
 *
 * 框架不是直接用这两个宏, 而是自己拼 "子目录/文件名", 例如
 * ui_resources_manager.c 里写的是 RES_PATH"JL/JL.res" 和 FONT_PATH"ascii.res"
 * (见 compat/jl_res_config.h 的 RES_PATH / FONT_PATH)。
 *
 * ⚠ 所以 UI_PORT_RES_ROOT 是【盘根】, 不能再多一层目录 ——
 *   写成 "0:/ui" 会让路径变成 0:/ui/JL/JL.res, 与盘上的 0:/JL/JL.res 对不上,
 *   表现为 resfile_open 全部失败、界面全黑。
 * ======================================================================== */

#define UI_PORT_RES_ROOT                "0:"
#define UI_PORT_FONT_ROOT               "0:/font"

/* ========================================================================
 * 六、上板排查开关
 * ======================================================================== */

/** 打印文件系统实际目录树。见 compat/jl_fs.h 的 ui_fs_dump_tree() */
#define UI_PORT_FS_DUMP_TREE            1

/** 推屏跟踪。每推一帧打一行 len / 非零字节数, 用来区分三种"黑屏":
 *    没打印        -> 绘制流水线没走到推屏
 *    nonzero=0    -> 推屏了但帧缓冲全 0, 是控件没画出东西
 *    nonzero>0    -> 数据已发出, 问题在面板初始化/接线/SPI 时序
 *  排查完置 0, 整段代码被编译掉。 */
#define UI_PORT_PUSH_TRACE              1

#endif /* __UI_PORT_CONFIG_H__ */
