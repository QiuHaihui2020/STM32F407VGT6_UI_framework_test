#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".ui_pushScreen_manager.data.bss")
#pragma data_seg(".ui_pushScreen_manager.data")
#pragma const_seg(".ui_pushScreen_manager.text.const")
#pragma code_seg(".ui_pushScreen_manager.text")
#endif
/* COPYRIGHT NOTICE
 * 文件名称 ：ui_pushScreen_manager.c
 * 简    介 ：UI框架推屏管理层
 * 功    能 ：
 * 			输入控制：LCD屏幕驱动，板级SPI配置
 * 			出输控制：硬件SPI推TFT彩屏，硬件SPI推OLED点阵屏，IMD推TFT彩屏
 *
 * 			输入作用：
 * 				LCD屏幕驱动：LCD初始化代码，LCD特别控制参数和方法
 * 				板级SPI配置：SPI模块选择，控制IO配置，SPI模块配置参数，LCD类型
 * 			输出根据：
 * 				lcd->type 判断为TFT彩屏或OLED屏，选择推点阵屏或彩屏
 * 				cpu	判断推TFT彩屏时使用硬件SPI还是IMD（具有IMD模块的芯片默认使用IMD，否则默认使用硬件SPI）
 *
 * 			说明：
 * 				点阵屏只能用SPI驱动推
 * 				TFT彩屏使用硬件SPI或IMD模块，可通过CPU宏来控制
 *
 * 作    者 ：zhuhaifang
 * 创建时间 ：2022/05/10 10:26
 */

#include "ui_port_config.h"

#if (TCFG_UI_ENABLE && (TCFG_LCD_OLED_ENABLE || TCFG_SPI_LCD_ENABLE))

#include "jl_typedef.h"
#include "jl_lcd_drive.h"   /* struct lcd_platform_data / lcd_e / 初始化码魔数 */
#include "ui_lcd_if.h"      /* 与屏硬件之间唯一的边界(纯语义函数) */
#include "jl_ui_api.h"
#include "jl_os_api.h"
/* #include "jl_app_stub.h" */
#include "jl_res_config.h"
#include "asm/imd.h"
#include "ui/buffer_manager.h"
#include "jl_app_stub.h"

#define LOG_TAG_CONST       UI
/* 上面的 include 已间接带入 jl_debug.h, 它在 LOG_TAG 未定义时会给
 * 一个空串默认值, 所以这里要先 undef 再定义自己的 tag */
#undef  LOG_TAG
#define LOG_TAG             "[UI-PUSH]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
#define LOG_DUMP_ENABLE
#define LOG_CHAR_ENABLE
#include "jl_debug.h"

#define PUSH_BY_IMD     0
#define PUSH_BY_SPI     1
#define LCD_PUSHSCREEN_MODE     PUSH_BY_SPI



/* 推屏功能测试，将根据指定规则进行刷屏测试 */
static void push_screen_test(void);
#define LCD_DRV_TEST()		//push_screen_test()


static u8  backlight_status = 0;
static u8  lcd_sleep_in     = 0;
static volatile u8 is_lcd_busy = 0;
static struct lcd_platform_data *lcd_dat = NULL;
/* 原有 `struct mcpwm_config lcd_pwm_p_data;` 已删除: 该类型来自杰理的
 * asm/mcpwm.h(移植期由 port/ui_port.h 仿造), 而全工程【零处使用】——
 * TCFG_BACKLIGHT_PWM_MODE=0 走纯 GPIO, mcpwm_init/set_duty 连实现都没有。
 * 需要 PWM 调背光时, 在 port/bsp/ui_lcd_if.h 里加 ui_lcd_backlight_set() 更直接。 */


// 推屏管理模块私有参数，读写命令、数据需要根据不同屏幕配置，因此需根据屏幕类型设置
struct ui_push_screen_var {
    int lcd_type;
    struct _lcd_drive *lcd;
    struct imd_param  *param;

    void (*write_cmd)();
    void (*read_cmd)();
};
static struct ui_push_screen_var push_screen = {0};
#define	__this	(&push_screen)


/*
 * 控制线。本文件【不知道任何引脚】—— 只说"把这条线拉高/拉低",
 * 接在哪个端口哪一位写在 port/bsp/stm32f4/ui_board_pins.h 里。
 *
 * 原厂这几个函数里是 `gpio_set_mode(lcd_dat->pin_cs / 16,
 * BIT(lcd_dat->pin_cs % 16), val)` —— 框架既持有引脚编号、又要自己把它
 * 拆成端口号加位掩码。现在这两件事都不关框架的事了。
 *
 * 未接的线(本板的 BL/EN/TE)由 port 层静默忽略, 所以这里不需要逐个判空。
 */

// EN 控制
void lcd_en_ctrl(u8 val)
{
    ui_lcd_power(val);
}

// BL 控制
void lcd_bl_ctrl(u8 val)
{
    ui_lcd_bl(!!val);
}

// CS 控制
static void spi_cs_ctrl(u8 val)
{
    ui_lcd_cs(val);
}

// DC 控制
static void spi_dc_ctrl(u8 val)
{
    ui_lcd_dc(val);
}

// TE 控制
static int spi_te_stat()
{
    /* 原厂这里先用 gpio_set_mode 的第三参把脚切成上拉/下拉/高阱再读 ——
     * 那是杰理 gpio_set_mode() 复用第三参表示输入模式的约定。新接口里
     * 读 TE 是独立的 ui_lcd_te_read(), 引脚方向在 ui_lcd_init() 里定好。
     * @note 本板没接 TE, ui_lcd_te_read() 直接返回 -1, 让框架走不等 TE
     *       的直推路径 —— "有没接 TE" 也是 port 层的知识。 */
    return (int)ui_lcd_te_read();
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DEVICE RESET
 *
 * Description: LCD 设备复位
 *
 * Arguments  : none
 *
 * Returns    : none
 *
 * Notes      : 1、判断是否在屏驱有重新定义LCD复位函数，
 * 					是使用屏驱上定义的LCD复位函数，
 * 					否使用GPIO控制板级配置的LCD复位IO
 *********************************************************************************************************
 */

static void lcd_reset()
{
    if (__this->lcd->reset) {
        __this->lcd->reset();
    } else {
        ui_lcd_rst(1);
        os_time_dly(10);
        ui_lcd_rst(0);
        os_time_dly(10);
        ui_lcd_rst(1);
        os_time_dly(10);
    }
}



/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD BACKLIGHT CONTROL
 *
 * Description: LCD 背光控制
 *
 * Arguments  : on LCD 背光开关标志，0为关，其它值为开
 *
 * Returns    : none
 *
 * Notes      : 1、判断是否在屏驱有重新定义背光控制函数，
 * 					是使用屏驱上定义的背光控制函数，
 * 					否使用GPIO控制板级配置的背光IO
 *
 *              2、配置背光状态标志，打开为true，关闭为false
 *********************************************************************************************************
 */
void lcd_mcpwm_init()
{

}

int lcd_drv_backlight_ctrl(u8 on)
{
    if (__this->lcd->backlight_ctrl) {
        __this->lcd->backlight_ctrl(on);
    } else if (ui_lcd_has_backlight()) {
        /* 原为 `else if (lcd_dat->pin_bl != -1)`。框架不再持有背光脚,
         * 改成问 port 层"这块板能不能控背光" —— 语义等价,
         * 但引脚这个概念不再出现在本层。
         *
         * @note PWM_MODE == 0 分支原厂就是空的(GPIO 背光由 lcd_bl_ctrl()
         *       单独控), 这里不动它 —— 本次只做分层, 不改行为。 */
#if (TCFG_BACKLIGHT_PWM_MODE == 0)

#elif (TCFG_BACKLIGHT_PWM_MODE == 1)

#elif (TCFG_BACKLIGHT_PWM_MODE == 2)

#endif
    } else {
        backlight_status = false;
        return -1;
    }

    if (on) {
        backlight_status = true;
    } else {
        backlight_status = false;
    }

    return 0;
}


struct lcd_platform_data *lcd_get_platform_data()
{
    return lcd_dat;
}



static int find_begin(u8 *begin, u8 *end, int pos)
{
    int i;
    u8 *p = &begin[pos];
    while ((p + 3) < end) {
        if ((p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) == BEGIN_FLAG) {
            return (&p[4] - begin);
        }
        p++;
    }

    return -1;
}


static int find_end(u8 *begin, u8 *end, int pos)
{
    u8 *p = &begin[pos];
    while ((p + 3) < end) {
        if ((p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) == END_FLAG) {
            return (&p[0] - begin);
        }
        p++;
    }

    return -1;
}


void lcd_drv_cmd_list(u8 *cmd_list, int cmd_cnt)
{
    int i;
    int k;
    int cnt;
    u16 *p16;
    u8 *p8;

    u8 *temp = NULL;
    u16 temp_len = 5 * 64;
    u16 len;
    temp = (u8 *)malloc(temp_len);

    for (i = 0; i < cmd_cnt;) {
        p16 = (u16 *)&cmd_list[i];
        int begin = find_begin(cmd_list, &cmd_list[cmd_cnt], i);
        if ((begin != -1)) {
            int end = find_end(cmd_list, &cmd_list[cmd_cnt], begin);
            if (end != -1) {
                p8 = (u8 *)&cmd_list[begin];
                u8 *param = &p8[1];
                u8 addr = p8[0];
                u8 cnt = end - begin - 1;
                if (((p8[0] << 24) | (p8[1] << 16) | (p8[2] << 8) | p8[3]) == REGFLAG_DELAY_FLAG) {
                    os_time_dly(p8[4] / 10);
                    /* printf("delay %d ms\n", p8[4]); */
                }  else {
                    len = sprintf((char *)temp, "send : 0x%02x(%d), ", addr, cnt);
                    for (k = 0; k < cnt; k++) {
                        len += sprintf((char *)&temp[len], "0x%02x, ", param[k]);
                        if (len > (temp_len - 10)) {
                            len += sprintf((char *)&temp[len], "...");
                            break;
                        }
                    }
                    len += sprintf((char *)&temp[len], "\n");
                    /* printf("cmd:%s", temp); */
#if 0
                    // 根据LCD类型选择发送命令的API
                    if (__this->lcd_type == TFT_LCD) {
                        imd_write_cmd(addr, param, cnt);
                    } else if (__this->lcd_type == DOT_LCD) {
                        spi_oled_write_cmd(addr, param, cnt);
                    }
#endif
#if (TCFG_SPI_LCD_ENABLE)
                    imd_write_cmd(addr, param, cnt);
#endif
                }
                i = end + 4;
            }
        }
    }
    free(temp);

#if 0
    u8 buf[5];
    printf("lcd_spi_read:\n");
    imd_read_cmd(0x0a, buf, 1);
    imd_read_cmd(0x52, buf, 1);
    imd_read_cmd(0x54, buf, 1);
    imd_read_cmd(0x0a, buf, 1);
    imd_read_cmd(0x59, buf, 1);
    imd_read_cmd(0x64, buf, 1);
    imd_read_cmd(0xa1, buf, 5);
    imd_read_cmd(0x0a, buf, 1);
    imd_read_cmd(0xa8, buf, 5);
    imd_read_cmd(0xaa, buf, 1);
    imd_read_cmd(0x0a, buf, 1);
    imd_read_cmd(0xaf, buf, 1);
    imd_read_cmd(0x0a, buf, 1);
#endif
}




/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DEVICE INIT
 *
 * Description: LCD 设备初始化
 *
 * Arguments  : *p 板级配置的 LCD SPI 信息
 *
 * Returns    : 0 初始化成功
 * 				-1 初始化失败
 *
 * Notes      : 1、判断是否在板级文件配置SPI，是继续，否进入断言，
 *
 *              2、配置SPI可操作IO给IMD操作
 *
 *              3、LCD设备复位
 *
 *              4、SPI模块初始化，IMD模块初始化
 *********************************************************************************************************
 */

int lcd_drv_init(void *p)
{
    log_debug("lcd_drv_init ...\n");
    int err = 0;
    struct ui_devices_cfg *cfg = (struct ui_devices_cfg *)p;
    __this->lcd_type	= cfg->type;	// 保存LCD屏幕类型(TFT_LCD/DOT_LCD)
    __this->lcd			= &lcd_drive;	// 获取LCD驱动配置
    __this->param		= __this->lcd->param;	// 获取LCD参数配置
    lcd_dat = (struct lcd_platform_data *)cfg->private_data;
    ASSERT(lcd_dat, "Error! spi io not config");

    /* 引脚已不在本层可见, 要看实际接线去 port/bsp/stm32f4/ui_board_pins.h */
    log_debug("lcd drv init, type:%d\n", __this->lcd_type);

    /* 给屏供电。没接使能脚时这是空操作 */
    ui_lcd_power(1);

    /*** mcu屏io注册 br27 IMD not surpport mcu screen***/
    /* __this->param->pap.wr_sel = lcd_dat->mcu_pins.pin_wr; */
    /* __this->param->pap.rd_sel = lcd_dat->mcu_pins.pin_rd; */
    /* printf("location [[%s : %s : %d]]\n", __FILE__, __FUNCTION__, __LINE__); */
    lcd_reset(); /* lcd复位 */

#if (TCFG_SPI_LCD_ENABLE)
    void imd_set_ctrl_pin_func(void (*dc_ctrl)(u8), void (*cs_ctrl)(u8), int (*te_stat)());
    imd_set_ctrl_pin_func(spi_dc_ctrl, spi_cs_ctrl, spi_te_stat);
    imd_init(__this->param);
#endif

#if 0
    if (__this->lcd_type == TFT_LCD) {
        // 初始化TFT_LCD彩屏
        imd_set_ctrl_pin_func(spi_dc_ctrl, spi_cs_ctrl, spi_te_stat);
        imd_init(__this->param);

    } else if (__this->lcd_type == DOT_LCD) {
        // 初始化DOT_LCD点阵屏
        spi_oled_set_ctrl_pin_func(spi_dc_ctrl, spi_cs_ctrl, spi_te_stat);
        spi_oled_init(lcd_dat);

    } else {
        printf("unknow lcd type!\n");
        return -1;
    }
#endif

    /* SPI发送屏幕初始化代码 */
    lcd_drv_cmd_list(__this->lcd->lcd_cmd, __this->lcd->cmd_cnt);

    lcd_mcpwm_init();
    LCD_DRV_TEST();


    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD DEVICE INFO
 *
 * Description: 获取 LCD 设备信息
 *
 * Arguments  : *info LCD 设备信息缓存结构体，根据结构体内容赋值即可
 *
 * Returns    : 0 获取成功
 * 				-1 获取失败
 *
 * Notes      : 1、根据参数结构体的内容，将LCD对应信息赋值给结构体元素
 *********************************************************************************************************
 */

static int lcd_drv_get_screen_info(struct lcd_info *info)
{
    /* imb的宽高 */
    info->width = __this->param->in_width;
    info->height = __this->param->in_height;

    /* imb的输出格式 */
    info->color_format = __this->param->in_format;//OUTPUT_FORMAT_RGB565;
    if (info->color_format == OUTPUT_FORMAT_RGB565) {
        info->stride = (info->width * 2 + 3) / 4 * 4;
    } else if (info->color_format == OUTPUT_FORMAT_RGB888) {
        info->stride = (info->width * 3 + 3) / 4 * 4;
    }

    /* 屏幕类型 */
    info->interface = __this->param->drv_type;

    /* 对齐 */
    info->col_align = __this->lcd->column_addr_align;
    info->row_align = __this->lcd->row_addr_align;

    /* 背光状态 */
    info->bl_status = backlight_status;
    info->buf_num = __this->param->buffer_num;

    ASSERT(info->col_align, " = 0, lcd driver column address align error, default value is 1");
    ASSERT(info->row_align, " = 0, lcd driver row address align error, default value is 1");

    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       MALLOC DISPLAY BUFFER
 *
 * Description: 申请 LCD 显存 buffer
 *
 * Arguments  : **buf 保存显存buffer指针
 * 				*size 保存显存buffer大小
 *
 * Returns    : 0 成功
 * 				-1 失败
 *
 * Notes      : 1、根据LCD驱动中配置的显存大小和数量申请显存BUFFER
 *
 *				2、将显存buffer指针赋值给参数**buf，显存buffer大小赋值给参数*size
 *
 *				注意：buffer默认是lock状态，此时不能推屏，需由UI框架获取并写入数据后才能推屏
 *********************************************************************************************************
 */
static int lcd_drv_buffer_malloc(u8 **buf, u32 *size)
{
    int buf_size = (__this->param->buffer_size + 3) / 4 * 4;	// 把buffer大小做四字节对齐

#if UI_USED_DOUBLE_BUFFER
    *buf = (u8 *)malloc(buf_size * __this->param->buffer_num);
#else
    *buf = (u8 *)malloc(buf_size * __this->param->buffer_num);
#endif

    if (!buf) {
        // 如果buffer申请失败
        *buf = NULL;
        *size = 0;
        return -1;
    }
    *size = buf_size * __this->param->buffer_num;

    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       FREE DISPLAY BUFFER
 *
 * Description: 释放 LCD 显存 buffer
 *
 * Arguments  : *buf 显存buffer指针
 *
 * Returns    : 0 成功
 * 				-1 失败
 *
 * Notes      : 1、使用memory API 释放显存buffer
 *********************************************************************************************************
 */
static int lcd_drv_buffer_free(u8 *buf)
{
    if (buf) {
        /* printf("lcd_buffer_free : 0x%x\n", buf); */
        free(buf);
        buf = NULL;
    }
    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD DRAW BUFFER
 *
 * Description: 把显存 buf 推送到屏幕
 *
 * Arguments  : *buf 显存buffer指针
 * 				len 显存buffer的数据量
 * 				wait 是否等待
 *
 * Returns    : 0 成功
 * 				-1 失败
 *
 * Notes      : 1、使用 IMD 模块将显存buffer推给屏幕
 *********************************************************************************************************
 */
int lcd_drv_draw(u8 *buf, u32 len, u8 wait)
{
#if (TCFG_SPI_LCD_ENABLE)
    imd_wait();
    extern void imd_set_busy(bool busy);
    void imd_irq_enable();
    imd_set_busy(1);
    imd_irq_enable();
    imd_draw(LCD_DATA_MODE, (u32)buf);

#endif
    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD BACKLIGHT STATUS
 *
 * Description: 获取 LCD 背光状态
 *
 * Arguments  : none
 *
 * Returns    : 0 背光熄灭
 * 				1 背光点亮
 *
 * Notes      :
 *********************************************************************************************************
 */
int lcd_backlight_status()
{
    return !!backlight_status;
}


int lcd_sleep_status()
{
    return lcd_sleep_in;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       LCD SLEEP CONTROL
 *
 * Description: LCD 休眠控制
 *
 * Arguments  : enter 是否进入休眠，true 进入休眠，false 退出休眠
 *
 * Returns    : 0 成功
 * 				-1 失败
 *
 * Notes      : 1、判断 LCD 是否正在使用，是等待使用结束，否进入下一步
 *
 * 				2、enter是否进入休眠，是使用LCD休眠函数进入休眠状态，否使用LCD退出休眠函数退出休眠状态
 *
 * 				3、lcd_sleep_in 记录LCD的休眠状态
 *********************************************************************************************************
 */

int lcd_sleep_ctrl(u8 enter)
{
    if ((!!enter) == lcd_sleep_in) {
        return -1;
    }
    while (is_lcd_busy);
    is_lcd_busy = 0x11;

    if (enter) {
        if (__this->lcd->entersleep) {
            __this->lcd->entersleep();
            lcd_sleep_in = true;
        }
    } else {
        if (__this->lcd->exitsleep) {
            __this->lcd->exitsleep();
            lcd_sleep_in = false;
        }
    }

    is_lcd_busy = 0;
    return 0;
}


/*$PAGE*/
/*
 *********************************************************************************************************
 *                                       GET LCD DRIVE HANDLER
 *
 * Description: 获取LCD驱动句柄
 *
 * Arguments  : none
 *
 * Returns    : struct lcd_interface* LCD驱动接口句柄
 *
 * Notes      : 1、从LCD接口列表中找到LCD接口句柄并返回
 *********************************************************************************************************
 */

/* lcd_get_hdl() 已移到 config/ui_port_registry.c ——
 * 原实现遍历链接器收集的 .lcd_if_info 段, 移植后改为显式注册表,
 * 与控件/风格两张表放在同一个文件里便于对照。 */


void lcd_drv_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
#if (TCFG_SPI_LCD_ENABLE)
    imd_set_draw_area(xs, xe, ys, ye);

#endif
}


static void lcd_drv_clear_screen(u32 color)
{
#if (TCFG_SPI_LCD_ENABLE)
    imd_full_clear(color);
    /* imd_fill_rect(LCD_COLOR_MODE, color, 0, __this->param->lcd_width - 1, 0, __this->param->lcd_height - 1); */
#endif
}

/**************** 使用spi接口推屏所用接口 ********************/
// io口操作
static void lcd_reset_l()
{
    ui_lcd_rst(0);
}
static void lcd_reset_h()
{
    ui_lcd_rst(1);
}
static void lcd_cs_l()
{
    ui_lcd_cs(0);
}
static void lcd_cs_h()
{
    ui_lcd_cs(1);
}
static void lcd_rs_l()
{
    ui_lcd_dc(0);
}
static void lcd_rs_h()
{
    ui_lcd_dc(1);
}

static void lcd_bl_l()
{
    ui_lcd_bl(0);
}

static void lcd_bl_h()
{
    ui_lcd_bl(1);
}

static int lcd_spi_send_byte(u8 byte)
{
    /* 原厂这里把返回值存进 ret 又恒返回 0(错误被吞掉)。改为如实返回 */
    return (int)ui_lcd_write_byte(byte);
}

/*
 * 原有的 spi_dma_wait_finish() 与 static int spi_pnd 已删除。
 *
 * 那份实现靠 spi_get_pending() 轮询, 而移植层里该函数恒返回 1 ——
 * 整个循环是空转, 真正的等待发生在 HAL 内部。留着它的害处是让人以为
 * 推屏有一套异步握手, 实际没有。现在所有等待点直接调
 * ui_lcd_wait_done(), 只有一个真实现。
 *
 * lcd_bl_io() 也一并删了: 它把引脚 token 当 u8 返回(会截断),
 * 而全工程零处调用。
 */

/**
 * @brief 走 DMA 发一块数据
 * @param wait 非 0 = 发完才返回
 * @note 屏障必须在【启动 DMA 之前】: 确保调用方刚写进 buf 的显存内容已经
 *       落到内存, DMA 才读得到正确数据。原代码把 ui_lcd_memory_barrier()
 *       放在启动传输【之后】, 顺序是反的 —— 只因为那条路径实际是同步发送,
 *       这个错才没有暴露出来。
 */
static int __spi_dma_send(const void *buf, u32 len, u8 wait)
{
    /* 原为 pi32 的 asm("csync")(流水线/写缓冲同步) */
    ui_lcd_memory_barrier();

    return (int)ui_lcd_write_block((const u8 *)buf, len, wait);
}

void spi_dma_send_map(u8 *map, u32 size)
{
    int err = 0;

    if (lcd_dat) {
        err = __spi_dma_send(map, size, 0);
    }

    if (err < 0) {
        lcd_e("spi dma send map timeout\n");
    }

}
void spi_dma_send_byte(u8 dat)
{
    int err = 0;
    u32 _dat ALIGNED(4) = 0;

    ((u8 *)(&_dat))[0] = dat;

    if (lcd_dat) {
        err = __spi_dma_send(&_dat, 1, 1);
    }

    if (err < 0) {
        lcd_e("spi dma send byte timeout\n");
    }
}
/** 建立屏用到的全部硬件: 控制脚 + SPI + DMA。幂等
 * @note 原名 spi_init(int spi_cfg) —— 那个参数一路传到 shim 就被丢弃,
 *       真正的 SPI 实例选择在 board/ui_board_stm32f4.h。去掉参数后,
 *       "改哪里能换 SPI" 就只有一个答案。
 * @note 中断注册(原 LCD_SPI_INTERRUPT_ENABLE 分支的 request_irq)也去掉了:
 *       DMA 完成中断由 HAL 在 ui_lcd_init() 里自己配好并常开。 */
static void lcd_hw_init(void)
{
    if (ui_lcd_init() < 0) {
        lcd_e("ui_lcd_init failed\n");
        return;
    }
    y_printf("ui lcd hw init succ\n");
}
static void lcd_spi_write_cmd(u8 data)
{
    lcd_cs_l();
    lcd_rs_l();
    lcd_spi_send_byte(data);
    lcd_cs_h();
}

static void lcd_spi_write_data(u8 data)
{
    lcd_cs_l();
    lcd_rs_h();
    lcd_spi_send_byte(data);
    lcd_cs_h();
}

static u32 lcd_cmd_list_flag(u8 *code)
{
    return ((u32)code[0]) << 24 | ((u32)code[1]) << 16 | ((u32)code[2]) << 8 | (u32)code[3];
}

static void delay_ms(unsigned int ms)
{
    u32 cnt = (ms + 1) * 1000;
    while (cnt--) {
        ;
    }
}

static void lcd_init_code(u8 *code, u16 cnt)
{
    /* printf("init code cnt: %d\n", cnt); */
    for (int i = 0; i < cnt; i++) {
        if (i >= cnt) {
            /* ASSRT(0, "lcd_code_list read err"); */
        }
        if (lcd_cmd_list_flag((u8 *)&code[i]) == BEGIN_FLAG) {
            /* printf("begin flag: 0x%x\n", lcd_cmd_list_flag(&code[i])); */
            i += 4;
            if (lcd_cmd_list_flag((u8 *)&code[i]) == REGFLAG_DELAY_FLAG) {
                /* printf("delay flag: 0x%x\n", lcd_cmd_list_flag(&code[i])); */
                i += 4;
                extern void wdt_clear(void);
                wdt_clear();
                /* printf("delay: %d\n", code[i]); */
                delay_ms(code[i]);
            } else {
                /* printf("cmd: 0x%x\n", code[i]); */
                lcd_spi_write_cmd(code[i]);
                i++;
                while (lcd_cmd_list_flag((u8 *)&code[i]) != END_FLAG) {
                    /* printf("data: 0x%x\n", code[i]); */
                    lcd_spi_write_data(code[i]);
                    i++;
                }
                /* printf("end flag: 0x%x\n", lcd_cmd_list_flag(&code[i])); */
            }
            continue;
        }
    }
}

static void lcd_spi_dev_init(void *p)
{
    struct ui_devices_cfg *cfg = (struct ui_devices_cfg *)p;
    __this->lcd_type	= cfg->type;	// 保存LCD屏幕类型(TFT_LCD/DOT_LCD)
    __this->lcd			= &lcd_drive;	// 获取LCD驱动配置
    __this->param		= __this->lcd->param;	// 获取LCD参数配置
    lcd_dat = (struct lcd_platform_data *)cfg->private_data;
    ASSERT(lcd_dat, "Error! spi io not config");
    /* 【顺序修正】先建硬件再写电平。原代码是先 gpio_set_mode() 写三个脚,
     * 后调 spi_init() —— 而把引脚配成推挽输出恰好就在 spi_init() 里面,
     * 那三次写是打在未配置的脚上(无效)。之后 port 层的引脚初始化又置了
     * 一次空闲电平, 所以侥幸正确。现在顺序理顺了。 */
    lcd_hw_init();

    ui_lcd_rst(1);
    ui_lcd_cs(1);
    ui_lcd_dc(1);

    lcd_reset(); /* lcd复位 */

    lcd_init_code(__this->lcd->lcd_cmd, __this->lcd->cmd_cnt);  // 初始化屏幕

}

static int lcd_spi_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
    if ((is_lcd_busy == 0x11) || lcd_sleep_in) {
        return 0;
    }
    is_lcd_busy = 1;
    /* 原有 spi_set_ie(spi_cfg, 0) 已删: DMA 中断由 HAL 自己管, 框架不再开关它 */
    ui_lcd_wait_done();

    lcd_spi_write_cmd(0x2A);
    lcd_spi_write_data(xs >> 8);
    lcd_spi_write_data(xs);
    lcd_spi_write_data(xe >> 8);
    lcd_spi_write_data(xe);

    lcd_spi_write_cmd(0x2B);
    lcd_spi_write_data(ys >> 8);
    lcd_spi_write_data(ys);
    lcd_spi_write_data(ye >> 8);
    lcd_spi_write_data(ye);

    lcd_spi_write_cmd(0x2C);

    lcd_cs_l();
    lcd_rs_h();
    return 0;
}

static void lcd_spi_write_map(char *map, u32 size)
{
    spi_dma_send_map((u8 *)map, size);
}

static int lcd_spi_draw(u8 *buf, u32 len, u8 wait)
{
    if ((is_lcd_busy == 0x11) || lcd_sleep_in) {
        return 0;
    }

    lcd_spi_write_map((char *)buf, len);
    is_lcd_busy = 0;
    return 0;
}

static int lcd_spi_clear_screen(u16 color)
{
    int i;
    int buffer_lines;
    int remain;
    int draw_line;
    int y = 0;

    u8 *line_buffer = (u8 *)malloc(__this->param->buffer_size);

    if (__this->param->in_format == OUTPUT_FORMAT_RGB565) {
        buffer_lines = __this->param->buffer_size / __this->param->lcd_width / 2;
        for (i = 0; i < buffer_lines * __this->param->lcd_width; i++) {
            line_buffer[2 * i] = color >> 8;
            line_buffer[2 * i + 1] = color;
        }

        remain = __this->param->lcd_height;
        while (remain) {
            draw_line = buffer_lines > remain ? remain : buffer_lines;
            lcd_spi_set_draw_area(0, __this->param->lcd_width - 1, y, y + draw_line - 1);
            lcd_spi_draw(line_buffer, draw_line * __this->param->lcd_width * 2, 0);
            remain -= draw_line;
            y += draw_line;
        }
        ui_lcd_wait_done();
    } else if (__this->param->in_format == LCD_COLOR_MONO) {
        lcd_spi_set_draw_area(0, -1, 0, -1);
        memset(line_buffer, 0x00, __this->param->lcd_width * __this->param->lcd_height / 8);
        lcd_spi_write_map((char *)line_buffer, __this->param->lcd_width * __this->param->lcd_height / 8);
        /* write_map 是异步的(wait=0), 而下面立即 free(line_buffer) ——
         * 不等完 DMA 就把源内存还给堆, 会推出垃圾或碰上重分配。
         * 原代码只在 RGB565 分支等了, MONO 分支漏了。 */
        ui_lcd_wait_done();
    } else {
        ASSERT(0, "the color_format %d not support yet!", __this->param->in_format);
    }
    free(line_buffer);

    return 0;
}

/**************** 使用spi接口推屏所用接口 ********************/

/**************** OLED屏所用接口 ********************/
/**
 * @brief 整屏填充单色(点阵屏通路)
 * @param color 只取低 8 位, 0x00=全灭 0xff=全亮; 一次发一整行(page)
 *
 * @note buf 为什么是 static 而不是局部数组:
 *       1) 它是 DMA 的源地址。当前 spi_dma_send() 是同步的(发完才返回),
 *          栈上数组【碰巧】安全; 一旦哪天换成异步 DMA, 函数返回后栈帧
 *          被覆盖, DMA 就搬走垃圾数据 —— 这类问题只在上板时偶发。
 *       2) STM32F407 的 CCM RAM(0x10000000) 【DMA 访问不到】。UI 任务栈
 *          若分配在 CCM(F407 工程为省 SRAM 的常见做法), 栈上 buf 根本推
 *          不出去。static 落在 .bss/SRAM1, 无此坑。
 *       3) 省 128 字节栈 —— UI 任务栈 4KB 还要递归重绘控件树, 不算宽裕。
 *
 *       单例无重入风险: clear_screen 只由 ui_platform.c 在 UI 任务里调。
 */
static void oled_spi_clear_screen(u32 color)
{
    /* 一整行的行缓冲。尺寸跟随 ui_port_config.h 的屏宽, 不再写死 128 */
    static u8 buf[UI_PORT_LCD_WIDTH] ALIGNED(4);

    /* 屏驱 param_t 里的 lcd_width 与配置头是同一个真值源(见 lcd_drive/)。
     * 万一有人只改了屏驱没改配置头, 这里挡住越界 DMA 而不是发出垃圾 */
    ASSERT(__this->param->lcd_width <= sizeof(buf),
           "lcd_width %d > UI_PORT_LCD_WIDTH %d", __this->param->lcd_width, (int)sizeof(buf));

    memset(buf, (color & 0xff), sizeof(buf));  //color为0x00或0xff
    for (int i = 0; i < __this->param->lcd_height / 8; i++) {
        lcd_spi_write_cmd(0xb0 + i);
        lcd_spi_write_cmd(0x00);

        lcd_spi_write_cmd(0x10);
        lcd_cs_l();
        lcd_rs_h();
        ui_lcd_write_block(buf, __this->param->lcd_width, 1);
        lcd_cs_h();
    }
}

static void oled_spi_set_draw_area(u16 xs, u16 xe, u16 ys, u16 ye)
{
    return;
}

static void oled_spi_draw(u8 *buf, u32 len, u8 wait)
{
    for (int i = 0; i < __this->param->lcd_height / 8; i++) {
        lcd_spi_write_cmd(0xb0 + i);
        lcd_spi_write_cmd(0x00);
        lcd_spi_write_cmd(0x10);

        lcd_cs_l();
        lcd_rs_h();
        ui_lcd_write_block(buf, __this->param->lcd_width, 1);
        lcd_cs_h();
        buf += __this->param->lcd_width;
    }
}
/**************** OLED屏所用接口 ********************/

/* 根据配置选择接口注册 */
#if (LCD_PUSHSCREEN_MODE == PUSH_BY_IMD)
REGISTER_LCD_INTERFACE(lcd) = {
    .init				= (void *)lcd_drv_init,
    .draw				= (void *)lcd_drv_draw,
    .get_screen_info	= (void *)lcd_drv_get_screen_info,
    .buffer_malloc		= (void *)lcd_drv_buffer_malloc,
    .buffer_free		= (void *)lcd_drv_buffer_free,
    .backlight_ctrl		= (void *)lcd_drv_backlight_ctrl,
    .set_draw_area		= (void *)lcd_drv_set_draw_area,
    .clear_screen		= (void *)lcd_drv_clear_screen,
};
#elif (LCD_PUSHSCREEN_MODE == PUSH_BY_SPI)

#if TCFG_OLED_SPI_SSD1306_ENABLE
REGISTER_LCD_INTERFACE(lcd) = {
    .init				= (void *)lcd_spi_dev_init,
    .draw				= (void *)oled_spi_draw,
    .get_screen_info	= (void *)lcd_drv_get_screen_info,
    .buffer_malloc		= (void *)lcd_drv_buffer_malloc,
    .buffer_free		= (void *)lcd_drv_buffer_free,
    .backlight_ctrl		= (void *)lcd_drv_backlight_ctrl,
    .set_draw_area		= (void *)oled_spi_set_draw_area,
    .clear_screen		= (void *)oled_spi_clear_screen,
};
#else
REGISTER_LCD_INTERFACE(lcd) = {
    .init				= (void *)lcd_spi_dev_init,
    .draw				= (void *)lcd_spi_draw,
    .get_screen_info	= (void *)lcd_drv_get_screen_info,
    .buffer_malloc		= (void *)lcd_drv_buffer_malloc,
    .buffer_free		= (void *)lcd_drv_buffer_free,
    .backlight_ctrl		= (void *)lcd_drv_backlight_ctrl,
    .set_draw_area		= (void *)lcd_spi_set_draw_area,
    .clear_screen		= (void *)lcd_spi_clear_screen,
};
#endif

#else
#endif


static u8 lcd_idle_query(void)
{
    return !is_lcd_busy;
}

REGISTER_LP_TARGET(lcd_lp_target) = {
    .name = "lcd",
    .is_idle = lcd_idle_query,
};





/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/*                                                                */
/*                     以下为推屏功能测试代码                     */
/*                                                                */
/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#if (TCFG_SPI_LCD_ENABLE)	// 仅TFT LCD需要RGB色填充

#define COLOR_MODE_RGB888	3
#define COLOR_MODE_RGB565	2
#define COLOR_MODE_RGB666	1
static void fill_buffer_test(u8 *buf, int w, int h, u32 color_888, u8 color_mode)
{
    /* 根据颜色模式，将颜色填充到buf中 */
    int pos = 0;
    int size = 0;
    u8 r, g, b;
    u8 bit1, bit2;
    u32 color;

    /* 从rgb888中获取r, g, b颜色分量 */
    r = (color_888 >> 16) & 0xff;
    g = (color_888 >> 8) & 0xff;
    b = (color_888 >> 0) & 0xff;

    /* 计算buffer大小 */
    if (color_mode == COLOR_MODE_RGB888) {
        size = w * h * 3;
        for (pos = 0; pos < size; pos += 3) {
            buf[pos + 0] = r;
            buf[pos + 1] = g;
            buf[pos + 2] = b;
        }
    } else if (color_mode == COLOR_MODE_RGB565) {
        size = w * h * 2;
        color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        bit1 = (color >> 8) & 0xff;
        bit2 = (color >> 0) & 0xff;
        for (pos = 0; pos < size; pos += 2) {
            buf[pos + 0] = bit1;
            buf[pos + 1] = bit2;
        }
    } else if (color_mode == COLOR_MODE_RGB666) {
        size = w * h * 3;
        color = ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);
        r = (color_888 >> 16) & 0xff;
        g = (color_888 >> 8) & 0xff;
        b = (color_888 >> 0) & 0xff;
        for (pos = 0; pos < size; pos += 3) {
            buf[pos + 0] = r;
            buf[pos + 1] = g;
            buf[pos + 2] = b;
        }
    } else {
        printf("%s, %d: error!, unknow color mode!", __FUNCTION__, __LINE__);
    }
}
#endif


static void push_screen_test(void)
{
#define	INTERVAL_TIME		100	// 刷屏测试间隔时间
    extern void wdt_clr();

#if (TCFG_SPI_LCD_ENABLE)
    u32 color_tab[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff, 0xffffff, 0x000000};
    int color_num = sizeof(color_tab) / sizeof(color_tab[0]);
    static u8 i = 0;

    while (1) {
        log_debug("clear color:0x%x\n", color_tab[i]);
        lcd_drv_clear_screen(color_tab[i]);

        if (++i >= color_num) {
            i = 0;
        }
        os_time_dly(INTERVAL_TIME);
        wdt_clr();
    }

#endif
}

#endif


