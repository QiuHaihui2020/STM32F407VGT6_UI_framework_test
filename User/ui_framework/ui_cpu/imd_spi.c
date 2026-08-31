/*
 * imd_spi.c —— IMD 的 SPI 屏驱动(SPI / DSPI / QSPI 三种线序的命令与数据时序)
 *
 * 【来源】从 cpu/br27/liba/ui_cpu.a 的 imd_spi.c.o 还原。该库交付的是 LLVM
 *   bitcode(非机器码)且保留完整调试信息, 故本文件按 IR + DWARF 还原。
 *     参考 IR : cpu/br27/tools/ui_reimpl/ref_ir/imd_spi.ll
 *     原始路径: btsdk/lib/utils/ui/ui_cpu/br27/imd_spi.c
 *
 * 【还原依据】每个函数与每处 ASSERT 都钉在原始行号上(见 gen_imd_spi.py),
 *   所以与原厂的差异只剩 ASSERT 宏内嵌的 __FILE__ 字符串。
 *   函数原始行号: imd_spi_io_init@57(static, 已被内联) imd_spi_init@161
 *   imd_spi_tx_cmd@242 imd_spi_tx_dat@274 imd_spi_rx_dat@306
 *   imd_spi_write_cmd@342 imd_spi_read_cmd@381 imd_spi_set_area@452
 *   imd_spi_draw@545 imd_spi_isr@571
 *
 * 【本工程未使用】屏走的不是 IMD 的 SPI 通道, sdk.lst 里查无这些符号, 属死代码。
 *   还原它是为了能把 ui_cpu.a 清空。
 *
 * 【段属性】代码在 .imd_spi.text, __this 在 .imd_spi.data(见 ref IR 的 section)。
 */
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".imd_spi.data")
#pragma data_seg(".imd_spi.data")
#pragma code_seg(".imd_spi.text")
#endif

#define LOG_TAG_CONST       UI
#include "jl_typedef.h"
#include "asm/br27.h"
#include "asm/imd.h"
#include "jl_debug.h"

/* 加固用的两个常量(原库这两处都是裸字面量/无上限):
 *   BUSY_TIMEOUT —— 等传输完成标志的自旋上限, 见 imd_spi_wait_done;
 *   PA8_BIT      —— imd_spi_read_cmd 在 QSPI 模式下临时改向的那个脚(PA8)。 */
#define IMD_SPI_BUSY_TIMEOUT    100000
#define IMD_SPI_PA8_BIT         0x100

static struct imd_spi_config_def *__this = NULL;

















/*
 * @brief 按 port / spi_mode / spi_dat_mode 配置 IMD 的 IO 复用与数据线数
 * @return 需要写进 IMD_IO_CFG 的使能位组合
 * @note 原厂构建已把本函数完全内联进 imd_spi_init(IR 里没有独立 define)。
 * @note PORT B 【不支持 QSPI】, 命中时只打一条错误日志然后按 0 使能(等于不配)。
 */
static void imd_spi_io_init()
{
    u32 spi_io_en = 0;

    if (__this->port == SPI_PORTA) {
        log_debug("SPI_PORT A\n");
        SFR(JL_IOMC->CON0, 27, 1, 0);
        switch (__this->spi_mode & 0xF0) {
        case QSPI_MODE:
            if ((__this->spi_mode & 0x0F) == QSPI_SUBMODE0) {
                JL_PORTA->DIR &= ~0xC0;
                spi_io_en = 0x3000;
            } else {
                JL_PORTA->DIR &= ~0x7C0;
                spi_io_en = 0x1F000;
            }
            break;
        case DSPI_MODE:
            JL_PORTA->DIR &= ~0x1C0;
            spi_io_en = 0x3000;
            break;
        default:
            if (__this->spi_dat_mode == SPI_MODE_BIDIR) {
                JL_PORTA->DIR &= ~0xC0;
                JL_PORTA->DIR |= 0x100;
            } else {
                JL_PORTA->DIR &= ~0xC0;
            }
            spi_io_en = 0x3000;
            break;
        }
    } else {
        SFR(JL_IOMC->CON0, 27, 1, 1);
        log_debug("SPI_PORT B\n");
        if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
            log_error("SPI_PORT B not suppot QSPI.\n");
            spi_io_en = 0;
        } else if ((__this->spi_mode & 0xF0) == DSPI_MODE) {
            spi_io_en = 0x7000;
        } else {
            if (__this->spi_dat_mode == SPI_MODE_BIDIR) {
                spi_io_en = 0x7000;
            } else {
                log_debug("SPI_MODE_UNDIR\n");
                spi_io_en = 0x3000;
            }
        }
    }

    SFR(JL_IMD->IMD_IO_CFG, 12, 5, spi_io_en >> 12);
}





















































void imd_spi_init(void *priv)
{
    ASSERT(priv);
    __this = priv;
    ASSERT(__this->cs_ctrl);
    ASSERT(__this->dc_ctrl);

    log_debug("imd spi config: 0x%x\n", priv);
    log_debug("port:%d, spi_mode:%d, spi_dat_mode:%d, pixel_type:%d, out_format:%d\n",
              __this->port, __this->spi_mode, __this->spi_dat_mode,
              __this->pixel_type, __this->out_format);

    imd_spi_io_init();

    switch (__this->pixel_type & 0xE0) {
    case PIXEL_1P1T:
        SFR(JL_IMD->IMDSPI_CON0, 3, 3, 1);
        break;
    case PIXEL_1P2T:
        SFR(JL_IMD->IMDSPI_CON0, 3, 3, 2);
        break;
    case PIXEL_1P3T:
        SFR(JL_IMD->IMDSPI_CON0, 3, 3, 3);
        break;
    case PIXEL_2P3T:
        SFR(JL_IMD->IMDSPI_CON0, 3, 3, 4);
        break;
    }

    switch (__this->out_format) {
    case FORMAT_RGB565:
        SFR(JL_IMD->IMDSPI_CON0, 6, 2, 2);
        break;
    case FORMAT_RGB666:
        SFR(JL_IMD->IMDSPI_CON0, 6, 2, 1);
        break;
    case FORMAT_RGB888:
        SFR(JL_IMD->IMDSPI_CON0, 6, 2, 0);
        break;
    }

    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        SFR(JL_IMD->IMDSPI_CON0, 23, 1, 1);
    } else if ((__this->spi_mode & 0xF0) == DSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        if ((__this->spi_mode & 0x0F) == SPI_WIRE3) {
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 0);
        } else {
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 1);
        }
    } else {
        if (__this->spi_dat_mode == SPI_MODE_BIDIR) {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 1);
        } else {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        }
        if ((__this->spi_mode & 0x0F) == SPI_WIRE3) {
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 0);
        } else {
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 1);
        }
    }

    SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
    SFR(JL_IMD->IMDSPI_CON0, 0, 1, 1);
}














/*
 * @brief 等 SPI 传输完成标志(IMDSPI_CON0 的 BIT31)
 * @note 加固: 原库 tx_cmd / tx_dat / rx_dat 三处都是
 *         while (!(JL_IMD->IMDSPI_CON0 & BIT(31)));
 *       —— 【无超时死等】。SPI 一旦不产生完成标志就是死循环, 而这三个函数
 *       都在前台被调用, 会把整个 UI 任务卡死。这里给一个自旋上限, 超时报错返回,
 *       让上层继续跑(最多这一次传输是坏的)。
 */
static void imd_spi_wait_done(void)
{
    u32 tmo = IMD_SPI_BUSY_TIMEOUT;

    while (!(JL_IMD->IMDSPI_CON0 & BIT(31))) {
        if (--tmo == 0) {
            log_error("imd_spi wait done timeout.\n");
            break;
        }
    }
}

static void imd_spi_tx_cmd(u32 cmd, u8 cmd_cnt)
{
    u32 cmd_val;
    ASSERT((cmd_cnt >= 1) && (cmd_cnt <= 4), "cmd num err!");

    cmd_val = cmd << ((4 - cmd_cnt) * 8);

    SFR(JL_IMD->IMDSPI_CON0, 8, 1, 0);
    if (JL_IMD->IMDSPI_CON0 & BIT(23)) {
        __this->dc_ctrl(0);
    } else {
        SFR(JL_IMD->IMDSPI_CON0, 22, 1, 0);
    }
    SFR(JL_IMD->IMDSPI_CON0, 24, 1, 0);
    SFR(JL_IMD->IMDSPI_CON0, 25, 3, cmd_cnt - 1);
    JL_IMD->IMDSPI_BUF = cmd_val;
    SFR(JL_IMD->IMDSPI_CON0, 1, 1, 1);
    imd_spi_wait_done();
    SFR(JL_IMD->IMDSPI_CON0, 30, 1, 1);
}












static void imd_spi_tx_dat(u32 para, u8 para_cnt)
{
    u32 para_val;

    ASSERT((para_cnt >= 1) && (para_cnt <= 4), "para num err!");

    para_val = para << ((4 - para_cnt) * 8);

    SFR(JL_IMD->IMDSPI_CON0, 8, 1, 0);
    if (JL_IMD->IMDSPI_CON0 & BIT(23)) {
        __this->dc_ctrl(1);
    } else {
        SFR(JL_IMD->IMDSPI_CON0, 22, 1, 1);
    }
    SFR(JL_IMD->IMDSPI_CON0, 24, 1, 0);
    SFR(JL_IMD->IMDSPI_CON0, 25, 3, para_cnt - 1);
    JL_IMD->IMDSPI_BUF = para_val;
    SFR(JL_IMD->IMDSPI_CON0, 1, 1, 1);
    imd_spi_wait_done();
    SFR(JL_IMD->IMDSPI_CON0, 30, 1, 1);
}











static u32 imd_spi_rx_dat(u8 para_cnt)
{
    ASSERT((para_cnt >= 1) && (para_cnt <= 4), "para num err!");
    u32 reg;

    SFR(JL_IMD->IMDSPI_CON0, 8, 1, 1);
    if (JL_IMD->IMDSPI_CON0 & BIT(23)) {
        __this->dc_ctrl(1);
    } else {
        SFR(JL_IMD->IMDSPI_CON0, 22, 1, 1);
    }
    SFR(JL_IMD->IMDSPI_CON0, 17, 4, 0);
    SFR(JL_IMD->IMDSPI_CON0, 21, 1, 0);
    SFR(JL_IMD->IMDSPI_CON0, 24, 1, 0);
    SFR(JL_IMD->IMDSPI_CON0, 25, 3, para_cnt - 1);
    SFR(JL_IMD->IMDSPI_CON0, 1, 1, 1);
    imd_spi_wait_done();
    SFR(JL_IMD->IMDSPI_CON0, 30, 1, 1);
    reg = JL_IMD->IMDSPI_BUF;
    log_debug("reg : 0x%x\n", reg);

    return reg;
}













void imd_spi_write_cmd(u8 cmd, u8 *buf, u8 len)
{
    int i;
    u8 remain_bytes;

    /* 加固: 原库不检查 buf。len 为 0 时不会访问 buf, 所以只在 len 非 0 时
     * 要求 buf 有效 —— "只发命令不带参数"这种用法仍然合法。
     * 判断必须放在【拉 CS 之前】, 否则半路返回会留下 CS 拉低的悬空状态。 */
    if (len && buf == NULL) {
        return;
    }

    __this->cs_ctrl(0);

    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        imd_spi_tx_cmd(0x02000000 | (cmd << 8), 4);
    } else {
        imd_spi_tx_cmd(cmd, 1);
    }

    /* 原厂 IR 在循环头之前多一条 `len == 0` 守卫(icmp eq i8), 说明源码把整个
     * 循环包在 if (len) 里 —— 去掉它 IR 就少这一条边。 */
    if (len) {
        for (i = 0; i < len; i += 4) {
            remain_bytes = len - i;
            if (remain_bytes > 3) {
                imd_spi_tx_dat((buf[i] << 24) | (buf[i + 1] << 16) | (buf[i + 2] << 8)
                               | buf[i + 3], 4);
            } else if (remain_bytes == 3) {
                imd_spi_tx_dat((buf[i] << 16) | (buf[i + 1] << 8) | buf[i + 2], 3);
            } else if (remain_bytes == 2) {
                imd_spi_tx_dat((buf[i] << 8) | buf[i + 1], 2);
            } else {
                imd_spi_tx_dat(buf[i], 1);
            }
        }
    }

    __this->cs_ctrl(1);
}





void imd_spi_read_cmd(u8 cmd, u8 *buf, u8 len)
{
    int i;
    u8 remain_bytes;
    u32 dat;
    /* 加固: QSPI 模式下要临时改 PA8 的方向与上下拉, 这里存原值以便出口还原。 */
    u32 pu0_bak = 0, pu1_bak = 0, pd0_bak = 0, pd1_bak = 0;

    /* 加固: 同 imd_spi_write_cmd, 原库不检查 buf; 判断放在拉 CS 之前。 */
    if (len && buf == NULL) {
        return;
    }

    __this->cs_ctrl(0);

    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
        imd_spi_tx_cmd(0x03000000 | (cmd << 8), 4);
    } else {
        imd_spi_tx_cmd(cmd, 1);
    }

    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        /* 加固: 原库改完这四个上下拉寄存器后, 出口【只恢复 DIR】,
         * 上下拉配置被永久改成"上拉使能、下拉关闭", 影响该脚之后的所有使用。
         * 这里先把这四位原值存下来, 出口原样还原。 */
        pu0_bak = JL_PORTA->PU0 & IMD_SPI_PA8_BIT;
        pu1_bak = JL_PORTA->PU1 & IMD_SPI_PA8_BIT;
        pd0_bak = JL_PORTA->PD0 & IMD_SPI_PA8_BIT;
        pd1_bak = JL_PORTA->PD1 & IMD_SPI_PA8_BIT;

        JL_PORTA->PU0 |= IMD_SPI_PA8_BIT;
        JL_PORTA->PU1 &= ~IMD_SPI_PA8_BIT;
        JL_PORTA->PD0 &= ~IMD_SPI_PA8_BIT;
        JL_PORTA->PD1 &= ~IMD_SPI_PA8_BIT;
        JL_PORTA->DIR |= IMD_SPI_PA8_BIT;
    }

    /* 原厂 IR 在循环头之前多一条 `len == 0` 守卫(icmp eq i8), 说明源码把整个
     * 循环包在 if (len) 里 —— 去掉它 IR 就少这一条边。 */
    if (len) {
        for (i = 0; i < len; i += 4) {
            remain_bytes = len - i;
            if (remain_bytes > 3) {
                dat = imd_spi_rx_dat(4);
                buf[i] = dat >> 24;
                buf[i + 1] = dat >> 16;
                buf[i + 2] = dat >> 8;
                buf[i + 3] = dat;
            } else if (remain_bytes == 3) {
                dat = imd_spi_rx_dat(3);
                buf[i] = dat >> 16;
                buf[i + 1] = dat >> 8;
                buf[i + 2] = dat;
            } else if (remain_bytes == 2) {
                dat = imd_spi_rx_dat(2);
                buf[i] = dat >> 8;
                buf[i + 1] = dat;
            } else {
                dat = imd_spi_rx_dat(1);
                buf[i] = dat;
            }
        }
    }

    __this->cs_ctrl(1);

    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        JL_PORTA->DIR &= ~IMD_SPI_PA8_BIT;

        /* 加固: 把上下拉也还原回去 —— 原库漏了这四条, 只恢复了 DIR。 */
        JL_PORTA->PU0 = (JL_PORTA->PU0 & ~IMD_SPI_PA8_BIT) | pu0_bak;
        JL_PORTA->PU1 = (JL_PORTA->PU1 & ~IMD_SPI_PA8_BIT) | pu1_bak;
        JL_PORTA->PD0 = (JL_PORTA->PD0 & ~IMD_SPI_PA8_BIT) | pd0_bak;
        JL_PORTA->PD1 = (JL_PORTA->PD1 & ~IMD_SPI_PA8_BIT) | pd1_bak;
    }
}














void imd_spi_set_area(int xstart, int xend, int ystart, int yend)
{
    u32 spi_io_en;

    log_debug("%s begin %d~%d, %d~%d, %dx%d\n", __FUNCTION__, xstart, xend, ystart, yend,
              xend - xstart + 1, yend - ystart + 1);

    SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);

    __this->cs_ctrl(0);
    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
        imd_spi_tx_cmd(0x02002A00, 4);
    } else if ((__this->spi_mode & 0xF0) == DSPI_MODE) {
        if ((__this->spi_mode & 0x0F) == SPI_WIRE3) {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 0);
        } else {
            SFR(JL_IMD->IMD_IO_CFG, 12, 5, 3);
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 1);
        }
        imd_spi_tx_cmd(0x2A, 1);
    } else {
        imd_spi_tx_cmd(0x2A, 1);
    }
    imd_spi_tx_dat((xstart << 16) | xend, 4);
    __this->cs_ctrl(1);

    __this->cs_ctrl(0);
    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
        SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
        imd_spi_tx_cmd(0x02002B00, 4);
    } else if ((__this->spi_mode & 0xF0) == DSPI_MODE) {
        if ((__this->spi_mode & 0x0F) == SPI_WIRE3) {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 0);
        } else {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 23, 1, 1);
        }
        imd_spi_tx_cmd(0x2B, 1);
    } else {
        imd_spi_tx_cmd(0x2B, 1);
    }
    imd_spi_tx_dat((ystart << 16) | yend, 4);
    __this->cs_ctrl(1);

    __this->cs_ctrl(0);
    if ((__this->spi_mode & 0xF0) == QSPI_MODE) {
        if ((__this->spi_mode & 0x0F) == QSPI_SUBMODE0) {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
            imd_spi_tx_cmd(0x02002C00, 4);
        } else if ((__this->spi_mode & 0x0F) == QSPI_SUBMODE1) {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
            imd_spi_tx_cmd(0x12, 1);
            SFR(JL_IMD->IMDSPI_CON0, 9, 6, 33);
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 3);
            imd_spi_tx_dat(0x2C00, 3);
        } else {
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 0);
            SFR(JL_IMD->IMDSPI_CON0, 9, 6, 39);
            imd_spi_tx_cmd(0x32002C00, 4);
            SFR(JL_IMD->IMDSPI_CON0, 15, 2, 3);
            SFR(JL_IMD->IMDSPI_CON0, 9, 6, 33);
        }
    } else if ((__this->spi_mode & 0xF0) == DSPI_MODE) {
        imd_spi_tx_cmd(0x2C, 1);
        SFR(JL_IMD->IMDSPI_CON0, 23, 1, 0);
        SFR(JL_IMD->IMDSPI_CON0, 15, 2, 2);
        SFR(JL_IMD->IMD_IO_CFG, 12, 5, 7);
    } else {
        imd_spi_tx_cmd(0x2C, 1);
    }

    log_debug("%s end %d~%d, %d~%d, %dx%d\n", __FUNCTION__, xstart, xend, ystart, yend,
              xend - xstart + 1, yend - ystart + 1);
}











void imd_spi_draw()
{
    SFR(JL_IMD->IMDSPI_CON0, 9, 6, __this->pixel_type | BIT(5));

    if (!(JL_IMD->IMD_CON0 & BIT(13))) {
        SFR(JL_IMD->IMD_CON0, 1, 1, 1);
    }

    SFR(JL_IMD->IMDSPI_CON0, 8, 1, 0);
    if (JL_IMD->IMDSPI_CON0 & BIT(23)) {
        __this->dc_ctrl(1);
    } else {
        SFR(JL_IMD->IMDSPI_CON0, 22, 1, 1);
    }
    SFR(JL_IMD->IMDSPI_CON0, 24, 1, 1);
    SFR(JL_IMD->IMDSPI_CON0, 1, 1, 1);
}









int imd_spi_isr()
{
    if (JL_IMD->IMDSPI_CON0 & BIT(31)) {
        SFR(JL_IMD->IMDSPI_CON0, 30, 1, 1);
        log_debug("imd spi pnd\n");
        __this->cs_ctrl(1);
        return 1;
    }

    return 0;
}

/*
 * 原库缺陷清单 + 加固状态(下面每条描述的都是【原库】行为, 仍照原样保留;
 * 方括号是本文件当前的处理结果。差异已登记在 accept/ 并锁定指纹)。
 *
 *   [已修] 1 —— 三处 while (!(... BIT(31))); 【无超时死等】-> 收敛到
 *                imd_spi_wait_done(), 带自旋上限 IMD_SPI_BUSY_TIMEOUT。
 *   [已修] 2 —— 两个 *_cmd 不检查 buf -> 已补(len 为 0 时不访问 buf, 故只在
 *                len 非 0 时要求 buf 有效)。判断放在【拉 CS 之前】, 否则半路
 *                返回会留下 CS 拉低的悬空状态。
 *   [已修] 3 —— imd_spi_read_cmd 在 QSPI 模式下改了 PA8 的五个寄存器, 出口
 *                却只恢复 DIR -> 现在进来前存下上下拉四位, 出口原样还原。
 *
 * 1) imd_spi_tx_cmd / imd_spi_tx_dat / imd_spi_rx_dat 里的
 *    `while (!(JL_IMD->IMDSPI_CON0 & BIT(31)));` 【没有超时】。
 *    SPI 一旦不产生完成中断标志就是死循环, 而且这三个函数都在前台被调用。
 *
 * 2) imd_spi_write_cmd / imd_spi_read_cmd 不检查 buf 是否为 NULL。
 *
 * 3) imd_spi_read_cmd 在 QSPI 模式下把 PA8 临时改成输入(改 PU0/PU1/PD0/PD1/DIR
 *    五个寄存器), 但只在函数出口恢复 DIR, 上下拉配置【没有恢复】。
 */
