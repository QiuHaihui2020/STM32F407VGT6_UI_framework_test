#include "mp3.h"

    
// 比特率和采样率查表（简化版，仅支持 MPEG-1 Layer III）
static const uint16_t bitrate_table[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const uint16_t sample_rate_table[] = {44100, 48000, 32000, 0};

// 解析 ID3v2 标签头
int parse_id3v2_header(ID3v2Header *header) {

    int tag_size;
    // 检查标识符是否为 "ID3"
    if (memcmp(header->identifier, "ID3", 3) != 0) {
        return -1;
    }

    // 解析标签大小（同步安全整数，大端序）
    tag_size = ((header->size & 0x7F) << 21) |
                (((header->size >> 8) & 0x7F) << 14) |
                (((header->size >> 16) & 0x7F) << 7) |
                ((header->size >> 24) & 0x7F);

    return tag_size;
}

//检查是否是MP3帧头
int mp3_frame_header_check(const uint8_t *data, MP3FrameHeader *header) {
    // 检查同步字 0xFFF
    if ((data[0] != 0xFF) || ((data[1] & 0xE0) != 0xE0)) {
        return -1;
    }

    // 提取字段
    header->sync_word = ((data[0] << 3) | (data[1] >> 5)) & 0x7FF;
    header->mpeg_version = (data[1] >> 3) & 0x03;
    header->layer = (data[1] >> 1) & 0x03;

    
    header->bitrate = bitrate_table[(data[2] >> 4) & 0x0F];
    header->sample_rate = sample_rate_table[(data[2] >> 2) & 0x03];
    header->channel_mode = (data[3] >> 6) & 0x03;

    return 0;
}