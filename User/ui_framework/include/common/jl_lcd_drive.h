/**
 * @file    jl_lcd_drive.h
 * @brief   屏驱注册结构体与推屏接口定义
 *
 * 只保留本移植真正用到的部分(原厂那份还带彩屏 QSPI/DSPI/RGB 的一大堆
 * 像素格式组合宏, 点阵屏一个都用不到)。
 */
#ifndef __JL_LCD_DRIVE_H__
#define __JL_LCD_DRIVE_H__

#include "jl_typedef.h"
#include "jl_os_api.h"

/* Keil 的某些头文件里有 Reset 宏, 会和结构体成员名撞 */
#ifdef Reset
#undef Reset
#endif

/* ---- 屏驱调试打印 --------------------------------------------------- */
#ifndef SPI_LCD_DEBUG_ENABLE
#define SPI_LCD_DEBUG_ENABLE    0
#endif

#if (SPI_LCD_DEBUG_ENABLE == 0)
#define lcd_d(...)
#define lcd_w(...)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)
#elif (SPI_LCD_DEBUG_ENABLE == 1)
#define lcd_d(...)
#define lcd_w(fmt, ...)	printf("[LCD WARNING]: "fmt, ##__VA_ARGS__)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)
#else
#define lcd_d(fmt, ...)	printf("[LCD DEBUG]: "fmt, ##__VA_ARGS__)
#define lcd_w(fmt, ...)	printf("[LCD WARNING]: "fmt, ##__VA_ARGS__)
#define lcd_e(fmt, ...)	printf("[LCD ERROR]: "fmt, ##__VA_ARGS__)
#endif


/* ---- 屏初始化代码的编码格式 -----------------------------------------
 * 屏驱把初始化命令序列写成一个 u8 数组, 每条命令用
 *   _BEGIN_, <cmd>, <param...>, _END_
 * 括起来; 需要延时时写成
 *   _BEGIN_, REGFLAG_DELAY, <毫秒数>, _END_
 * 这些 4 字节魔数由 port 层的 lcd_init_code() 逐字节扫描识别。 */
#define BEGIN_FLAG          0x55555555
#define END_FLAG            0xaaaaaaaa
#define _BEGIN_             ((BEGIN_FLAG >> 24) & 0xff), ((BEGIN_FLAG >> 16) & 0xff), \
                            ((BEGIN_FLAG >> 8) & 0xff),  (BEGIN_FLAG & 0xff)
#define _END_               ((END_FLAG >> 24) & 0xff),   ((END_FLAG >> 16) & 0xff), \
                            ((END_FLAG >> 8) & 0xff),    (END_FLAG & 0xff)

#define REGFLAG_DELAY_FLAG  0xff5aa5ff
#define REGFLAG_DELAY       ((REGFLAG_DELAY_FLAG >> 24) & 0xff), ((REGFLAG_DELAY_FLAG >> 16) & 0xff), \
                            ((REGFLAG_DELAY_FLAG >> 8) & 0xff),  (REGFLAG_DELAY_FLAG & 0xff)

#define REGFLAG_CONFIRM_FLAG 0xff5bb5ff
#define REGFLAG_CONFIRM     ((REGFLAG_CONFIRM_FLAG >> 24) & 0xff), ((REGFLAG_CONFIRM_FLAG >> 16) & 0xff), \
                            ((REGFLAG_CONFIRM_FLAG >> 8) & 0xff),  (REGFLAG_CONFIRM_FLAG & 0xff)

// 两毫秒延时
extern void delay_2ms(int cnt);
#define delay2ms(t)         delay_2ms(t)


/* ---- 颜色格式 / 接口类型 -------------------------------------------- */
enum LCD_COLOR {
    LCD_COLOR_RGB888,
    LCD_COLOR_RGB565,
    LCD_COLOR_MONO,     /**< 单色点阵, 本移植用这个 */
};

enum LCD_IF {
    LCD_SPI,
    LCD_MCU,
    LCD_RGB,
    LCD_EMI,
};

/* 框架里 in_format 字段既用 LCD_COLOR_* 也用 OUTPUT_FORMAT_* 两套名字。
 * OUTPUT_FORMAT_* 与 LCD_COLOR_MODE/LCD_DATA_MODE 由 asm/imd.h 以 enum
 * 形式给出, 这里【不能】再用 #define 重复定义 —— 宏会把 imd.h 里的
 * 枚举名替换掉, 报 "expected identifier"。
 * 两边取值已核对一致: RGB888=0, RGB565=1, 本移植的 MONO=2。 */


/* ---- 屏驱配置字 -----------------------------------------------------
 * 屏驱文件用一个 u32 把 "SPI 线制 + 输出色彩格式 + 像素打包方式" 打包成
 * LCD_DRIVE_CONFIG, 再用 SPI_IF_MODE/OUT_FORMAT/PIXEL_TYPE 解包填进
 * struct imd_param。本移植不走 IMD 硬件推屏, 这几个字段实际不参与推屏,
 * 但屏驱源码要用, 故保留原厂的打包规则。
 *
 * 右边那些常量(SPI_MODE / SPI_WIRE4 / FORMAT_RGB565 / PIXEL_1P2T /
 * PIXEL_1T8B)来自框架自带的 include/ui/cpu/br27/asm/imd_spi.h。 */
#define SPI_SUBMODE(config)     (((config) >> 16) & 0xf0)
#define SPI_WIRE(config)        (((config) >> 16) & 0x0f)
#define PIXEL_nPnT(config)      (((config)) & 0xe0)
#define PIXEL_nTnB(config)      (((config)) & 0x1f)
#define SPI_IF_MODE(config)     (((config) >> 16) & 0xff)
#define OUT_FORMAT(config)      (((config) >> 8) & 0xff)
#define PIXEL_TYPE(config)      (((config)) & 0xff)
#define LCD_CONFIG(mode, format, type)     (((mode) << 16) | ((format) << 8) | (type))

/** 4 线 SPI + RGB565 + 每次传 8bit。SSD1306 屏驱用的就是这个 */
#define SPI_4WIRE_RGB565_1T8B     LCD_CONFIG(SPI_MODE | SPI_WIRE4, FORMAT_RGB565, PIXEL_1P2T | PIXEL_1T8B)


/* ---- 屏的板级配置 -------------------------------------------------
 * 【引脚字段已全部删除】。
 *
 * 框架不再持有任何引脚: 控制线通过 ui_lcd_cs()/dc()/rst()/bl()/
 * power() 这几个语义函数拉高拉低, "CS 接在 PB6" 只写在
 * port/board/ui_board_pins.h 里, 只有 port/lcd/ui_lcd_<mcu>.c 读得到。
 *
 * 原厂这里有过下面这些东西, 本移植已全部去除:
 *
 *   pin_reset/cs/dc/en/bl/te   引脚编号。框架拿着它们去调 gpio_set_mode(),
 *                              等于让 UI 层知道接线。现在改接线不波及框架。
 *
 *   hw_spi_dev / spi_cfg       SPI 控制器编号。一路传到底就被丢掉, 真值在
 *                              board/ui_board_stm32f4.h。留着只会让人以为
 *                              改它就能换 SPI。
 *
 *   NO_CONFIG_PORT (-1)        "引脚未配置"的第二种标记。框架里 -1 和 0 两种
 *                              写法混用, 判断散在十几处, 漏一处就是去操作一个
 *                              不存在的引脚。现在这类判断只在 port 内部一处。
 *
 *   IO_PORT_SPILT(x)           把引脚号拆成 (port, bitmask) 两个参数的宏。配上
 *                              框架里另一套 (pin/16, BIT(pin%16)) 写法, 同一个函数有
 *                              两种调用约定, 只能靠"第二参是否为 0"去猜。
 *
 *   PORT_HIGHZ / PORT_INPUT_PULLUP_10K / PORT_INPUT_PULLDOWN_10K
 *                              杰理 gpio_set_mode() 复用第三参表示输入模式的约定。
 *                              新接口读 TE 是独立的 ui_lcd_te_read()。
 *
 * 结构体本身保留: 框架的 ui_devices_cfg.private_data 机制还在用它做
 * "屏配置已就位"的非空检查, 且 MCU 屏 / RGB 屏 那两条通路将来启用时
 * 仍需要一个搾放屏参的地方。
 */

/** lcd_ui_api.c 用这对宏定义板级配置实例 */
#define LCD_SPI_PLATFORM_DATA_BEGIN(data)     const struct lcd_platform_data data = {
#define LCD_SPI__PLATFORM_DATA_END()     };

struct lcd_platform_data {
    /** 屏驱私有参数。本移植不用, 置 NULL */
    const void *spi_pdata;
};


/* ---- 屏驱注册 -------------------------------------------------------
 * REGISTER_LCD_DEVICE() 展开成一个【普通全局变量】lcd_drive
 * (原厂就是这样, 不是段收集), 一个工程只能有一块屏。 */
struct _lcd_drive {
    char *logo;                 /**< 屏名, 如 "ssd1306" */
    u8 column_addr_align;
    u8 row_addr_align;
    u8 *lcd_cmd;                /**< 初始化命令序列 */
    int cmd_cnt;                /**< 序列字节数 */
    void *param;               /**< 指向屏参结构(struct imd_param) */
    void (*init)(void);
    void (*reset)(void);
    int  (*backlight_ctrl)(u8);
    int  (*power_ctrl)(u8);
    void (*entersleep)(void);
    void (*exitsleep)(void);
};

#define REGISTER_LCD_DEVICE()   struct _lcd_drive lcd_drive
extern struct _lcd_drive lcd_drive;


/* ---- 推屏接口 -------------------------------------------------------
 * 原厂用链接脚本收集 .lcd_if_info 段得到 lcd_interface_begin/end。
 * 本移植改成【显式注册表】(见 port/ui_port_registry.c) —— armlink 没有
 * GNU ld 的 PROVIDE, 而且段收集漏了是"界面整块不显示"的静默故障。 */
struct lcd_info {
    u16 width;
    u16 height;
    u16 stride;
    u8  color_format;
    u8  interface;
    u8  col_align;
    u8  row_align;
    u8  buf_num;
    u8  bl_status;
    u8  *buffer;
    int buffer_size;
};

struct lcd_interface {
    void (*init)(void *);
    void (*get_screen_info)(struct lcd_info *info);
    void (*buffer_malloc)(u8 **buf, u32 *size);
    void (*buffer_free)(u8 *buf);
    void (*draw)(u8 *buf, u32 len, u8 wait);
    void (*set_draw_area)(u16 xs, u16 xe, u16 ys, u16 ye);
    void (*clear_screen)(u32 color);
    int  (*backlight_ctrl)(u8 on);
    void (*draw_page)(u8 *buf, u8 page_star, u8 page_len);

    u8  *(*init_buffer)(u8 index, u8 *baddr, u32 size);
    u8  *(*get_buffer)(u8 index, u8 *prive_baddr);
    u8  *(*set_buffer_pending)(u8 index, u8 *buffer);
    void (*release_buffer)(u8 index);
    int  (*draw_area)(u8 index, u8 *lcd_buf, int left, int top, int width, int height);
};

/** 屏驱推屏接口注册。名字保持 lcd, 由 ui_port_registry.c 显式引用 */
#define REGISTER_LCD_INTERFACE(name) \
    const struct lcd_interface name

/** 取推屏接口句柄。实现在 port/ui_port_registry.c */
struct lcd_interface *lcd_get_hdl(void);

/** 背光 / 休眠状态查询, 实现在 ui_pushScreen_manager.c */
int lcd_backlight_status(void);
int lcd_sleep_status(void);
int lcd_sleep_ctrl(u8 enter);
int lcd_drv_backlight_ctrl(u8 on);
struct lcd_platform_data *lcd_get_platform_data(void);

#endif /* __JL_LCD_DRIVE_H__ */
