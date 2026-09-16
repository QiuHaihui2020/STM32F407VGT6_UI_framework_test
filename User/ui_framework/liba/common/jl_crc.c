/**
 * @file    jl_crc.c
 * @brief   CRC-16/XMODEM 计算
 *
 * 【纯软件查表实现】STM32F4 的 CRC 外设是固定的 CRC-32 以太网多项式,
 *   接不上这里要的 CRC-16/XMODEM, 所以只做软件查表 —— 它是纯函数, 可重入,
 *   中断里也能直接调, 不需要互斥锁。
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
 * @note 表值是多项式 0x1021 的标准半字节表(每次处理 4 位)。
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
 * @brief 资源文件的解扰。本工程的资源不加扰, 故为空实现
 *
 * @note 这个函数【不属于 CRC 模块】, 是否加扰由资源打包工具侧决定;
 *       框架只在读到"已加扰"标志时才调它。本工程用 PC 工具直出的未加扰资源,
 *       所以留空。若换成加扰资源, 必须在这里补上对应算法, 否则数据是乱的。
 */
void CrcDecode(void *buf, u16 len)
{
    (void)buf;
    (void)len;
}
