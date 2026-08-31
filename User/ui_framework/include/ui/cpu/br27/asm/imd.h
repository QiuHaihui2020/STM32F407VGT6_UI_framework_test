#ifndef __IMD_H__
#define __IMD_H__


#include "jl_typedef.h"
#include "jl_list.h"
#include "asm/imd_spi.h"


/* AT_UI_RAM 已由 jl_typedef.h 统一给出(本移植是空宏), 这里不再重复定义 */


extern const int ENABLE_JL_UI_FRAME;
extern const int ENABLE_PSRAM_UI_FRAME;
extern u8 curr_index;



// IMD驱动类型定义
typedef enum {
    IMD_DRV_SPI,
    IMD_DRV_MCU,
    IMD_DRV_RGB,
    IMD_DRV_MAX,
} IMD_DRV_TYPE;



// IMD draw模式定义
enum {
    LCD_COLOR_MODE,
    LCD_DATA_MODE,
};



// spi mcu rgb模式的接口定义
typedef struct imd_driver {
    void (*init)(void *priv);
    void (*write)(u8, u8 *, u8);
    void (*read)(u8, u8 *, u8);
    void (*set_draw_area)(int, int, int, int);
    void (*draw)();
    int (*isr)();
} imd_drive_cfg;



// imd私有变量定义
struct imd_variable {
    u8 imd_pnd;		// imd中断标志
    u8 imd_busy;	// imd忙碌标志
    u8 te_ext;		// te中断标志
    u8 clock_init;	// imd时钟初始化标志
    u8 sfr_save;	// 寄存器保存标志
    u32  imd_sfr[17];
    void (*dc_ctrl)(u8 val);
    void (*cs_ctrl)(u8 val);
    int (*te_stat)();
    struct imd_param *param;
};



// imd输入数据格式
enum OUTPUT_FORMAT {
    OUTPUT_FORMAT_RGB888,
    OUTPUT_FORMAT_RGB565,
};



// lcd配置结构体
struct imd_param {
    // 显示区域相关
    int scr_x;
    int scr_y;
    int scr_w;
    int scr_h;

    // lcd配置
    int lcd_width;
    int lcd_height;

    // 驱动类型
    IMD_DRV_TYPE drv_type;

    // 显存配置
    int buffer_num;
    int buffer_size;

    // 帧率配置
    int fps;

    // imd模块的输入，与imb模块一起用时，也是imb模块的输出
    int in_width;
    int in_height;
    int in_format;
    int in_stride;

    // debug模式
    int debug_mode_en;
    int debug_mode_color;

    // te功能
    int te_en;		//TE信号使能
    int te_port;	// te的io

    struct imd_spi_config_def spi;	// SPI屏

    struct pap_param {	// MCU屏
        int out_format;
        int wr_rd_swap_en;
        int right_shift_2bit;
        int dat_l2h_en;
    } pap;

    struct rgb_param {	// RGB屏
        int dat_l2h;
        int dat_sf;
        int hv_mode;
        int xor_edge;
        int out_format;
        int continue_frames;

        int hsync_out;
        int vsync_out;
        int dsync_out;

        int hpw_prd;
        int hbw_prd;
        int hfw_prd;
        int vpw_prd;
        int vbw_prd;
        int vfw_prd;
    } rgb;
};


void imd_set_size(int width, int height);
void imd_set_stride(int stride);

#define imd_set_buf_size(width, height, stride) \
{ \
	imd_set_size(width, height); \
	imd_set_stride(stride); \
}


#define IMD_PAP_PND_IE()	SFR(JL_IMD->IMDPAP_CON0, 29, 1, 1)
#define IMD_PAP_PND_DIS()	SFR(JL_IMD->IMDPAP_CON0, 29, 1, 0)
#define IMD_RGB_PND_IE()	SFR(JL_IMD->IMDRGB_CON0, 29, 1, 1)
#define IMD_RGB_PND_DIS()	SFR(JL_IMD->IMDRGB_CON0, 29, 1, 0)



int imd_init(struct imd_param *param);

void imd_clock_init(unsigned int imd_target_clk_kHz);

void imd_write_cmd(u8 cmd, u8 *buf, u8 len);

void imd_read_cmd(u8 cmd, u8 *buf, u8 len);

void imd_set_draw_area(int xstart, int xend, int ystart, int yend);

void imd_draw(u8 mode, u32 priv);

void imd_full_clear(u32 color);

void imd_fill_rect(int draw_mode, u32 buf_or_rgb888, int x, int w, int y, int h);

void imd_wait();

void imd_set_ctrl_pin_func(void (*dc_ctrl)(u8), void (*cs_ctrl)(u8), int (*te_stat)());

#endif





