/**
 * @file    jl_crc.h
 * @brief   杰理 crc.h 的等价声明。实现在 liba/common/jl_crc.c
 */
#ifndef __JL_CRC_H__
#define __JL_CRC_H__

#include "jl_typedef.h"

/** 带初值的底层实现(CRC-16/XMODEM), 下面两个都是它的薄包装 */
u16 crc16_xmodem(const void *buff, u32 len, u16 crc);

/** 资源头 / 数据校验、资源缓存指纹都用它, 必须与制作资源的 PC 工具算法一致 */
u16 CRC16(const void *ptr, u32 len);

/** 分段续算用: 传上一段的结果做初值 */
u16 CRC16_with_initval(const void *ptr, u32 len, u16 i_val);

/** 资源解扰。本移植资源不加扰, 为空实现 */
void CrcDecode(void *buf, u16 len);

#endif /* __JL_CRC_H__ */
