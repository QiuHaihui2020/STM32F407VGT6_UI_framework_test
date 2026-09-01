/**
 * @file    jl_crc.c
 * @brief   杰理 CRC16
 *
 * 【来源】从 cpu/br27/liba/cpu.a 的 crc16.c.o 还原。该库交付的是 LLVM bitcode
 *   (非机器码)且保留完整调试信息, 故按 IR + DWARF 还原, 不是按语义猜。
 *     原始路径: btsdk/lib/driver/cpu/periph/crc16.c
 *
 * 【原厂有硬件与软件两条路】原厂 CRC16() 先判 cpu_in_irq() || cpu_irq_disabled():
 *   在中断里或关中断时走软件版 crc16_xmodem(), 否则拿互斥锁后用 CRC 外设
 *   (0xFD3004 写初值/读结果, 0xFD3000 逐字节喂数据, csync 后取回)。
 *   两条路的初值都是 0x0000, 结果等价。
 *
 *   STM32F4 的 CRC 外设是固定的 CRC-32 以太网多项式, 接不上这里要的
 *   CRC-16/XMODEM, 所以本移植只保留【软件路径】, 不再判中断上下文,
 *   也就不需要互斥锁 —— 软件版是纯函数, 本身可重入。
 *
 * 【为什么必须一致】liba/res/resfile.c 的 open_image_by_id 拿
 *   CRC16(&res_pic.data_crc, sizeof(res_pic) - 2) 与文件里的 head_crc 比,
 *   算法不同则每张图都加载失败, 表现为界面整块空白。
 */
#include "jl_crc.h"


/*
 * CRC-16/XMODEM 的半字节查表: 多项式 0x1021, 初值由入参给, MSB first,
 * 输入输出都不反转, 结果不异或。
 *
 * @note 表值直接取自原厂 IR 的 @crc16_xmodem.crc_ta 常量数组。
 */
static const u16 crc_ta[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
};

/*
 * @brief CRC-16/XMODEM, 每字节分高低两个半字节各查表一次
 * @param crc 初值(接着上一段继续算时传上次的结果)
 */
u16 crc16_xmodem(const void *buff, u32 len, u16 crc)
{
    const u8 *ptr = (const u8 *)buff;

    while (len--) {
        u8 da;

        da  = (u8)(crc >> 12);
        crc = (u16)(crc << 4);
        crc ^= crc_ta[da ^ (u8)(*ptr >> 4)];

        da  = (u8)(crc >> 12);
        crc = (u16)(crc << 4);
        crc ^= crc_ta[da ^ (u8)(*ptr & 0x0FU)];

        ptr++;
    }

    return crc;
}

u16 CRC16(const void *ptr, u32 len)
{
    return crc16_xmodem(ptr, len, 0);
}

u16 CRC16_with_initval(const void *ptr, u32 len, u16 i_val)
{
    return crc16_xmodem(ptr, len, i_val);
}

/*
 * @brief 资源文件的解扰。本移植的资源不加扰, 故为空实现
 *
 * @note 这个函数【不在 crc16.c 里】, 原厂由资源打包工具侧决定是否加扰;
 *       框架只在读到"已加扰"标志时才调它。本工程用 PC 工具直出的未加扰资源,
 *       所以留空。若换成加扰资源, 必须在这里补上对应算法, 否则数据是乱的。
 */
void CrcDecode(void *buf, u16 len)
{
    (void)buf;
    (void)len;
}
