#ifndef __MP3_H_
#define __MP3_H_
#include "typedef.h"

// ID3v2 标签头结构10 字节
typedef struct {
    char identifier[3];      // 应为 "ID3"
    uint8_t version_major;   // 主版本号（如 3 表示 ID3v2.3）
    uint8_t version_minor;   // 次版本号
    uint8_t flags;           // 标志位
    uint32_t size;           // 标签大小（需解析同步安全整数）
} ID3v2Header;

// MP3 帧头结构
typedef struct {
    uint32_t sync_word : 11;      // 同步字，固定为0xFFF（二进制11111111111），用于标识帧起始
    uint32_t mpeg_version : 2;    // MPEG 版本 ，00=MPEG 2.5（非标准），01=保留，10=MPEG-2，11=MPEG-1
    uint32_t layer : 2;           // 层，01=Layer III（MP3），其他值用于MP1/MP2。
    uint32_t crc_en : 1;            // 0=启用CRC校验（帧头后跟2字节校验码），1=无校验
    uint32_t bitrate : 4;        // 比特率 (查表)
    uint32_t sample_rate : 2;    // 采样率 (查表)
    uint32_t padding : 1;        // 填充位 (1 bit)
    uint32_t private_bit : 1;     // 私有位 (1 bit)
    uint32_t channel_mode : 2;    // 声道模式00=立体声，01=联合立体声，10=双声道，11=单声道
    uint32_t mode_extension : 2; // 模式扩展 (2 bits)
    uint32_t copyright : 1;      // 版权标志，0=无版权，1=受版权保护
    uint32_t original : 1;       // 原始标志，0=复制，1=原始
    uint32_t emphasis : 2;       // 强调模式，00=无，01=50/15ms，10=保留，11=CCIT J.17。
} MP3FrameHeader;


#endif // !__MP3_H_