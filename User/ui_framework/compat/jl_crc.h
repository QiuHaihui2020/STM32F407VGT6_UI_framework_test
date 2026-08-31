/**
 * @file    jl_crc.h
 * @brief   杰理 crc.h 的等价声明。实现在 port/ui_port_misc.c
 */
#ifndef __JL_CRC_H__
#define __JL_CRC_H__

#include "jl_typedef.h"

/** 资源头 / 数据校验、资源缓存指纹都用它, 必须与制作资源的 PC 工具算法一致 */
u16 CRC16(const void *ptr, u32 len);
u16 CRC16_with_initval(const void *ptr, u32 len, u16 i_val);
void CrcDecode(void *buf, u16 len);

#endif /* __JL_CRC_H__ */
