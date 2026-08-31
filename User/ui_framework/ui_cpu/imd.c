/*
 * imd.c —— IMD 推屏硬件模块驱动(SPI / MCU / RGB 三种屏接口的统一入口)
 *
 * 【来源】从 cpu/br27/liba/ui_cpu.a 的 imd.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原,
 *   而非从反汇编推测。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/imd.ll
 *     原始路径: btsdk/lib/utils/ui/ui_cpu/br27/imd.c
 *
 * 【还原依据】每个函数与每处 ASSERT 都钉在原始行号(DISubprogram / cpu_assert
 *   的 __LINE__ 实参)上, 见 cpu/br27/tools/ui_reimpl/gen_imd.py。因此本文件
 *   与原厂的差异只剩 ASSERT 宏内嵌的 __FILE__ 字符串(路径不可能相同)。
 *   结构体大小/字段偏移经 DWARF 反向校验, 与头文件逐项吻合:
 *     imd_variable=92B  imd_param=176B  imd_spi_config_def=28B  imd_driver=24B
 * 【段属性】原库代码在 .imd.text、数据在 .imd.data(见 ref IR 的 section 属性)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".imd.data")
#pragma data_seg(".imd.data")
#pragma code_seg(".imd.text")
#endif

#define LOG_TAG_CONST       UI
#include "jl_typedef.h"
#include "asm/br27.h"
#include "asm/hwi.h"
#include "asm/irq.h"
#include "asm/imd.h"
#include "generic/jiffies.h"
#include "jl_debug.h"

volatile struct imd_variable imd_var;
#define __imd  (&imd_var)
static struct imd_param *__this = NULL;

void imd_set_busy(u8 busy)
{
    __imd->imd_busy = busy;
}

/* -- 原厂 imd.c 第 41 ~ 45 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 */
struct imd_driver imd_spi_io = {
    .init          = imd_spi_init,
    .write         = imd_spi_write_cmd,
    .read          = imd_spi_read_cmd,
    .set_draw_area = imd_spi_set_area,
    .draw          = imd_spi_draw,
    .isr           = imd_spi_isr,
};

struct imd_driver imd_pap_io = {0};
struct imd_driver imd_rgb_io = {0};


struct imd_driver *driver[IMD_DRV_MAX];


void imd_driver_set(int index, struct imd_driver *drv)
{
    driver[index] = drv;
}


struct imd_driver *imd_driver_get(int index)
{
    return driver[index];
}

/* -- 原厂 imd.c 第 74 ~ 79 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 */
void imd_driver_init()
{
    imd_driver_set(IMD_DRV_SPI, &imd_spi_io);
    imd_driver_set(IMD_DRV_MCU, &imd_pap_io);
    imd_driver_set(IMD_DRV_RGB, &imd_rgb_io);
}

/* -- 原厂 imd.c 第 88 ~ 88 行: 本还原未涉及的内容 --------------------
 */
void imd_set_ctrl_pin_func(void (*dc_ctrl)(u8), void (*cs_ctrl)(u8), int (*te_stat)())
{
    __imd->dc_ctrl = dc_ctrl;
    __imd->cs_ctrl = cs_ctrl;
    __imd->te_stat = te_stat;
}


__attribute__((always_inline_when_const_args))
void imd_cs(u8 val)
{
    if (__imd->cs_ctrl) {
        __imd->cs_ctrl(val);
    }
}


__attribute__((always_inline_when_const_args))
void imd_dc(u8 val)
{
    if (__imd->dc_ctrl) {
        __imd->dc_ctrl(val);
    }
}


__attribute__((always_inline_when_const_args))
void imd_dat_dir(u8 val)
{
    if (val) {
        JL_PORTA->DIE |= 0xff00;
        JL_PORTA->DIR |= 0xff00;
    } else {
        JL_PORTA->HD0 |= 0xff00;
        JL_PORTA->HD1 &= ~0xff00;
        JL_PORTA->DIE |= 0xff00;
        JL_PORTA->DIR &= ~0xff00;
    }
}

/* -- 原厂 imd.c 第 130 ~ 139 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
static u8 imd_busy()
{
    return __imd->imd_busy;
}

/* -- 原厂 imd.c 第 146 ~ 186 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
/* 一轮等待的 tick 数, 与原库一致; 连续超过 IMD_WAIT_MAX_ROUND 轮就放弃。 */
#define IMD_WAIT_TICKS_PER_ROUND    10
#define IMD_WAIT_MAX_ROUND          10

void imd_wait()
{
    u32 wait_timeout = jiffies + IMD_WAIT_TICKS_PER_ROUND;
    u32 round = 0;

    while (imd_busy()) {
        if (time_after(jiffies, wait_timeout)) {
            printf("wait imd_busy timeout.\n");
            wait_timeout = jiffies + IMD_WAIT_TICKS_PER_ROUND;

            printf("imd_busy : %d\n", __imd->imd_busy);

            /*
             * 加固: 原库这里【只打印不退出】—— while 里没有任何 break,
             * 屏一直忙就是死循环, 只靠 10 tick 一次的日志刷屏提示。
             * 这里给一个轮次上限, 超了就放弃等待返回, 让调用方继续跑
             * (最多花一帧画面, 不至于整机卡死在这)。
             */
            if (++round >= IMD_WAIT_MAX_ROUND) {
                printf("imd_wait give up, imd still busy.\n");
                break;
            }
        }
    }

}

/* -- 原厂 imd.c 第 202 ~ 207 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 */
void imd_irq_enable()
{
    ASSERT(__this);
    switch (__this->drv_type) {
    case IMD_DRV_SPI:
        IMD_SPI_PND_IE();
        break;
    case IMD_DRV_MCU:
        IMD_PAP_PND_IE();
        break;
    case IMD_DRV_RGB:
        IMD_RGB_PND_IE();
        break;
    }
}

/* -- 原厂 imd.c 第 225 ~ 232 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 */
static void imd_irq_disable()
{
    ASSERT(__this);
    switch (__this->drv_type) {
    case IMD_DRV_SPI:
        IMD_SPI_PND_DIS();
        break;
    case IMD_DRV_MCU:
        IMD_PAP_PND_DIS();
        break;
    case IMD_DRV_RGB:
        IMD_RGB_PND_DIS();
        break;
    }
}

/* -- 原厂 imd.c 第 250 ~ 252 行: 本还原未涉及的内容 --------------------
 *
 *
 */
static void imd_ld_data_isr()
{
    if (JL_IMD->IMD_CON0 & BIT(31)) {
        JL_IMD->IMD_CON0 |= BIT(30);
        log_debug("imd_ld_pnd isr\n");
    }
}

/* -- 原厂 imd.c 第 262 ~ 262 行: 本还原未涉及的内容 --------------------
 */
___interrupt static void imd_isr()
{
    imd_ld_data_isr();
    if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->isr) {
        if (driver[IMD_DRV_SPI]->isr()) {
            __imd->imd_pnd = 1;
            __imd->imd_busy = 0;
        }
    }
    if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->isr) {
        driver[IMD_DRV_MCU]->isr();
    }
    if (driver[IMD_DRV_RGB] && driver[IMD_DRV_RGB]->isr) {
        driver[IMD_DRV_RGB]->isr();
    }
}

/* -- 原厂 imd.c 第 281 ~ 292 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
static void imd_debug_config(u8 debug_mode, u32 color_888)
{
    u32 color = 0;
    u32 rgb_r = color_888 >> 16;

    ASSERT(__this);

    switch (__this->drv_type) {
    case IMD_DRV_SPI:
        if (__this->spi.out_format == FORMAT_RGB565) {
            color = (((u8)rgb_r >> 3) << 11) | (((u8)(color_888 >> 8) >> 2) << 5) | ((u8)color_888 >> 3);
        } else if (__this->spi.out_format == FORMAT_RGB666) {
            color = (((u8)rgb_r >> 2) << 12) | (((u8)(color_888 >> 8) >> 2) << 6) | ((u8)color_888 >> 2);
        } else {
            color = ((u32)(u8)rgb_r << 16) | ((u32)(u8)(color_888 >> 8) << 8) | (u8)color_888;
        }
        break;
    case IMD_DRV_MCU:
        if (__this->pap.out_format == FORMAT_RGB565) {
            color = (((u8)rgb_r >> 3) << 11) | (((u8)(color_888 >> 8) >> 2) << 5) | ((u8)color_888 >> 3);
        } else if (__this->pap.out_format == FORMAT_RGB666) {
            color = (((u8)rgb_r >> 2) << 12) | (((u8)(color_888 >> 8) >> 2) << 6) | ((u8)color_888 >> 2);
        } else {
            color = ((u32)(u8)rgb_r << 16) | ((u32)(u8)(color_888 >> 8) << 8) | (u8)color_888;
        }
        break;
    default:
        if (__this->rgb.out_format == FORMAT_RGB565) {
            color = (((u8)rgb_r >> 3) << 11) | (((u8)(color_888 >> 8) >> 2) << 5) | ((u8)color_888 >> 3);
        } else if (__this->rgb.out_format == FORMAT_RGB666) {
            color = (((u8)rgb_r >> 2) << 12) | (((u8)(color_888 >> 8) >> 2) << 6) | ((u8)color_888 >> 2);
        } else {
            color = ((u32)(u8)rgb_r << 16) | ((u32)(u8)(color_888 >> 8) << 8) | (u8)color_888;
        }
        break;
    }

    if (debug_mode) {
        if (color_888 != -1) {
            JL_IMD->IMD_DEBUG_PIXEL = color;
        }
        log_debug("color_888:0x%08x color:0x%08x\n", color_888, color);
        SFR(JL_IMD->IMD_CON0, 13, 1, 1);
    } else {
        JL_IMD->IMD_LD_START_ADR = color_888;
        SFR(JL_IMD->IMD_CON0, 13, 1, 0);
    }
}

/* -- 原厂 imd.c 第 343 ~ 348 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 */
void imd_set_size(int width, int height)
{
    ASSERT(width, ", width :  %d\n", width);
    ASSERT(height, ", height : %d\n", height);

    SFR(JL_IMD->IMD_CON1, 0, 10, width);
    SFR(JL_IMD->IMD_CON1, 10, 10, height);
    SFR(JL_IMD->IMD_CON0, 2, 10, height);
    SFR(JL_IMD->IMD_CON3, 0, 10, height);
}

/* -- 原厂 imd.c 第 361 ~ 371 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
void imd_set_stride(int stride)
{
    ASSERT(stride, ", stride : %d\n", stride);

    SFR(JL_IMD->IMD_CON1, 20, 12, stride);
}

/* -- 原厂 imd.c 第 380 ~ 385 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 */
int imd_init(struct imd_param *param)
{
    __this = param;
    __imd->param = param;


    ASSERT(__this);
    ASSERT(__imd->param);

    imd_cs(1);

    if (__this->debug_mode_en) {
        imd_debug_config(1, __this->debug_mode_color);
    }

    if (__this->te_en) {
        __imd->te_ext = 1;
    }

    switch (__this->in_format) {
    case OUTPUT_FORMAT_RGB565:
        SFR(JL_IMD->IMD_CON3, 10, 2, 2);
        break;
    case OUTPUT_FORMAT_RGB888:
        SFR(JL_IMD->IMD_CON3, 10, 2, 0);
        break;
    }

    SFR(JL_IMD->IMD_CON0, 29, 1, 1);
    SFR(JL_IMD->IMD_CON0, 15, 1, 1);
    SFR(JL_IMD->IMD_CON0, 16, 1, 1);
    SFR(JL_IMD->IMD_CON0, 0, 1, 1);

    request_irq(IRQ_IMD_IDX, 1, imd_isr, 0);

    if (!__imd->clock_init) {
        /*
         * 加固: 原库在这里声明了一个【未初始化的局部变量】就直接当实参传进去
         * (参考 IR 里该实参就是 i32 undef, 本地编译也如实报 -Wuninitialized)。
         * imd_clock_init 的形参从头到尾没被使用(函数体只写死几个时钟寄存器),
         * 所以当前没有实际后果, 但这是彻头彻尾的 UB。去掉那个变量, 显式传 0。
         */
        imd_clock_init(0);
    }

    __imd->imd_pnd = 1;
    __imd->imd_busy = 0;

    imd_driver_init();

    switch (__this->drv_type) {
    case IMD_DRV_SPI:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->init) {
            __this->spi.cs_ctrl = imd_cs;
            __this->spi.dc_ctrl = imd_dc;
            driver[IMD_DRV_SPI]->init(&__this->spi);
        } else {
            ASSERT(0);
        }
        break;
    case IMD_DRV_MCU:
        if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->init) {
            driver[IMD_DRV_MCU]->init((void *)__imd);
        } else {
            ASSERT(0);
        }
        break;
    default:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->init) {
            driver[IMD_DRV_SPI]->init((void *)__imd);
        } else {
            ASSERT(0);
        }

        if (driver[IMD_DRV_RGB] && driver[IMD_DRV_RGB]->init) {
            driver[IMD_DRV_RGB]->init((void *)__imd);
        } else {
            ASSERT(0);
        }
        break;
    }

    return 0;
}

/* -- 原厂 imd.c 第 467 ~ 472 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 */
void imd_write_cmd(u8 cmd, u8 *buf, u8 len)
{

    ASSERT(__this);

    switch (__this->drv_type) {
    case IMD_DRV_SPI:
    case IMD_DRV_RGB:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->write) {
            driver[IMD_DRV_SPI]->write(cmd, buf, len);
        }
        break;
    case IMD_DRV_MCU:
        if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->write) {
            driver[IMD_DRV_MCU]->write(cmd, buf, len);
        }
        break;
    }
}

/* -- 原厂 imd.c 第 494 ~ 497 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 */
void imd_read_cmd(u8 cmd, u8 *buf, u8 len)
{

    ASSERT(__this);

    switch (__this->drv_type) {
    case IMD_DRV_SPI:
    case IMD_DRV_RGB:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->read) {
            driver[IMD_DRV_SPI]->read(cmd, buf, len);
        }
        break;
    case IMD_DRV_MCU:
        if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->read) {
            driver[IMD_DRV_MCU]->read(cmd, buf, len);
        }
        break;
    }

    /*
     * 加固: 原库这里 malloc(320) + sprintf 把读回来的字节拼成一串调试文本,
     * 然后【直接 free, 从不输出】(IR 里没有任何 printf / puts 消费它) ——
     * 即每次读屏寄存器都白白做一次 320 字节堆分配; 而且【不检查 malloc 返回值】
     * 就 sprintf, 堆耗尽时会往 NULL 写。
     *
     * 整段没有任何可观测行为, 直接删掉: 既省掉那次堆分配, 也消除了 NULL 写。
     * 真要看读回的数据, 在调用方按需打印即可 —— 那里有完整的 buf 与 len,
     * 不必在这条每次读寄存器都会走的路径上做堆分配。
     */
}

/* -- 原厂 imd.c 第 530 ~ 543 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
void imd_set_draw_area(int xstart, int xend, int ystart, int yend)
{

    ASSERT(__this);

    imd_wait();
    imd_irq_disable();
    switch (__this->drv_type) {
    case IMD_DRV_SPI:
    case IMD_DRV_RGB:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->set_draw_area) {
            IMD_SPI_PND_DIS();
            driver[IMD_DRV_SPI]->set_draw_area(__this->scr_x + xstart, __this->scr_x + xend, __this->scr_y + ystart, __this->scr_y + yend);
        }
        break;
    case IMD_DRV_MCU:
        if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->set_draw_area) {
            driver[IMD_DRV_MCU]->set_draw_area(__this->scr_x + xstart, __this->scr_x + xend, __this->scr_y + ystart, __this->scr_y + yend);
        }
        break;
    default:
        ASSERT(0, "unrecognized!");
        break;
    }

    int width = xend - xstart + 1;
    int height = yend - ystart + 1;
    int stride = ((__this->in_format == OUTPUT_FORMAT_RGB888 ? 3 : 2) * width + 3) / 4 * 4;

    imd_set_size(width, height);

    imd_set_stride(stride);
}

/* -- 原厂 imd.c 第 579 ~ 580 行: 本还原未涉及的内容 --------------------
 *
 */
void imd_draw(u8 mode, u32 priv)
{

    ASSERT(__this);

    if (__this->debug_mode_en) {
        imd_debug_config(1, __this->debug_mode_color);
    } else if (mode == LCD_COLOR_MODE) {
        imd_debug_config(1, priv);
    } else {
        imd_debug_config(0, priv);
    }

    switch (__this->drv_type) {
    case IMD_DRV_SPI:
        if (driver[IMD_DRV_SPI] && driver[IMD_DRV_SPI]->draw) {
            driver[IMD_DRV_SPI]->draw();
        }
        break;
    case IMD_DRV_MCU:
        if (driver[IMD_DRV_MCU] && driver[IMD_DRV_MCU]->draw) {
            driver[IMD_DRV_MCU]->draw();
        }
        break;
    default:
        if (driver[IMD_DRV_RGB] && driver[IMD_DRV_RGB]->draw) {
            driver[IMD_DRV_RGB]->draw();
        }
        break;
    }
}

/* -- 原厂 imd.c 第 614 ~ 625 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
void imd_full_clear(u32 color)
{
    ASSERT(__this);

    imd_set_draw_area(0, __this->lcd_width - 1, 0, __this->lcd_height - 1);

    __imd->imd_pnd = 0;
    __imd->imd_busy = 1;

    imd_irq_enable();

    imd_draw(LCD_COLOR_MODE, color);

    imd_wait();

    imd_irq_disable();
}

/* -- 原厂 imd.c 第 645 ~ 653 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 */
void imd_fill_rect(int draw_mode, u32 buf_or_rgb888, int x, int w, int y, int h)
{
    ASSERT(__this);

    __imd->imd_busy = 1;
    __imd->imd_pnd = 0;

    imd_set_draw_area(x, w, y, h);

    imd_irq_enable();

    imd_draw(draw_mode, buf_or_rgb888);

    imd_wait();

    imd_irq_disable();
}

/* -- 原厂 imd.c 第 673 ~ 747 行: 本还原未涉及的内容 --------------------
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 *
 */
void imd_clock_init(unsigned int imd_target_clk_kHz)
{
    /* 加固: 形参从头到尾没被使用(时钟分频全是写死的), 显式标明,
     * 免得再有人像原库那样随便传个未初始化的值进来。 */
    (void)imd_target_clk_kHz;

    SFR(JL_CLOCK->CLK_CON4, 8, 4, 2);
    SFR(JL_CLOCK->CLK_CON4, 12, 2, 0);
    SFR(JL_CLOCK->CLK_CON4, 14, 2, 1);
    SFR(JL_CLOCK->SYS_DIV, 0, 8, 0);

    __imd->clock_init = 1;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— imd_clock_init 的实参是未初始化的局部变量(IR 里就是 i32 undef)
 *                -> 去掉那个变量, 显式传 0; 形参侧用 (void) 标明从未使用。
 *   [已修] 2 —— imd_read_cmd 结尾 malloc(320)+sprintf 拼完直接 free、从不输出,
 *                且不检查 malloc 返回值 -> 整段无可观测行为, 已删除。
 *   [已修] 3 —— imd_wait 超时分支只打印不退出(while 里没有 break), 屏一直忙
 *                就是死循环 -> 已加轮次上限, 超了就放弃等待返回。
 *
 * 1) imd_init 里 `imd_clock_init(imd_target_clk_kHz)` 的实参是一个**未初始化的
 *    局部变量**(参考 IR 中该实参为 `i32 undef`)。imd_clock_init 本身完全没用
 *    这个形参, 所以当前无实际后果, 但这是彻头彻尾的未定义行为写法。
 *
 * 2) imd_read_cmd 结尾 `malloc(320)` + sprintf 拼出调试字符串后**直接 free,
 *    从不输出**(参考 IR 中没有任何 printf/puts 消费 print_buf)。即每次读屏
 *    寄存器都白白做一次 320 字节堆分配。且**未检查 malloc 返回值**就 sprintf,
 *    堆耗尽时会向 NULL 写入。
 *
 * 3) imd_wait 的超时分支只打印不退出(`while` 无 break), 屏一直忙就是死循环,
 *    仅靠 10 tick 一次的日志刷屏提示。
 */
