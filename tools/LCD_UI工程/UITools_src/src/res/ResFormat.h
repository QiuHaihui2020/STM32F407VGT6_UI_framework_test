// result.bin / result.str（固件里叫 JL.res / JL.str）的二进制格式定义。
//
// 权威来源是固件自己的解析器：
//   User/ui_framework/liba/res/resfile.c          —— 头 / 页表 / 条目表 / 取图
//   User/ui_framework/lcd_drive/middle/ui_synthesis_oled.c:488 —— 单色像素怎么读
//
// 所有验证脚本在 re/ 下，实测结论见 docs/FILE_FORMATS.md 第 10 节。
#ifndef RESFORMAT_H
#define RESFORMAT_H

#include <QByteArray>
#include <QtGlobal>

namespace res {

// 文件头 RES_HEAD_T（16 字节）
struct Head {
    char     magic[4];      // "RU21"
    quint16  version;       // 恒为 0x0101
    quint16  panelType;     // LCDPANEL=0 / OLEDPANEL=1（原厂 paneltype 字段）
    quint16  totalPage;     // .res 是页数；.str 是语言数
    quint16  reserved;      // 0
    quint32  resver;        // 资源版本，会和 res_ver.h 里的常量比对
};

// 页表项 RES_PAGE_T（8 字节，仅 .res 有）
struct PageEntry {
    quint32 pageNum;
    quint32 pageAddr;
};

// 索引表项 RES_ENTRY_T（12 字节）
struct Entry {
    quint32 dwOffset;
    quint16 wCount;
    quint8  bItemType;      // 'P'=0x50 图片/字符串表, 'T'=0x54 调色板表, 'S'=0x53 字符串表
    quint8  langsum;
    quint32 language;       // 语言位掩码
};

// 调色板项 RES_PAL_T（12 字节）
struct PalEntry {
    quint32 num;            // 恒 256
    quint32 dwOffset;
    quint32 dwLength;       // 恒 1024
};

// 图片项 RES_BMP_T（20 字节）
struct BmpEntry {
    quint16 head_crc;       // CRC16-XMODEM(本结构第 2..20 字节)
    quint16 data_crc;       // CRC16-XMODEM(像素数据)；原厂在 .res 里恒写 0
    quint16 res_type;       // 0=图片 1=字符串
    quint16 typeId;         // (compress<<13) | (format<<10) | id
    quint16 wWidth;
    quint16 wHeight;
    quint32 dwLength;
    quint32 dwOffset;
};

enum ItemType {
    ITEM_PICTURE = 0x50,    // 'P'
    ITEM_STRING  = 0x53,    // 'S'
    ITEM_PALETTE = 0x54     // 'T'
};

enum ResType { RES_PICTURE = 0, RES_STRING = 1 };

// typeId 里的 format 字段。原厂工程用的是 OSD1（1bpp 单色），编码为 0。
enum PixelFormat { FMT_OSD1 = 0, FMT_RGB565 = 2, FMT_L8 = 3, FMT_AL88 = 4 };

enum Compress { CMP_NONE = 0, CMP_RLE = 1, CMP_QUICKLZ = 2 };

const int HEAD_SZ  = 16;
const int PAGE_SZ  = 8;
const int ENTRY_SZ = 12;
const int PAL_SZ   = 12;
const int BMP_SZ   = 20;
const int PALETTE_COLORS = 256;
const int PALETTE_BYTES  = PALETTE_COLORS * 4;

/**
 * @brief CRC16-XMODEM（多项式 0x1021，初值 0，不反转不异或）
 * @note  与固件 liba/res/resfile.c 里的 CRC16() 完全一致，务必保持同一实现。
 */
quint16 crc16(const void *data, int len, quint16 init = 0);

inline quint16 crc16(const QByteArray &b, quint16 init = 0)
{
    return crc16(b.constData(), b.size(), init);
}

/**
 * @brief 计算 1bpp 竖向分页位图的字节数
 * @note  这是**真实**长度。原厂写进 dwLength 的是 (w+7)*pages ——
 *        那是把 "((w+7)/8)*h" 的括号打错成 "(w+7)*(h/8)" 的历史 bug。
 *        固件读单色图时根本不看 dwLength（按 offset 逐行读），所以一直没暴雷。
 */
inline int monoSize(int w, int h) { return w * ((h + 7) / 8); }

/**
 * @brief 原厂写进 dwLength 的值（保留这个 bug 以便和原厂输出逐字节对齐）
 * @note  括号打错的位置是 **h 那一边**：正确写法 ((w+7)/8)*h，原厂写成
 *        (w+7)*h/8。当 h 是 8 的倍数时两种理解（(w+7)*(h/8) 与 (w+7)*h/8）
 *        结果相同，全工程 314 张图里只有 26x26 那 4 张 h 不是 8 的倍数，
 *        原厂给的是 (26+7)*26/8 = 107 而不是 (26+7)*4 = 132 —— 以此定案。
 *        存进文件的仍然是 monoSize() 那么多字节，dwLength 只是个错的声明。
 */
inline int legacyLength(int w, int h) { return (w + 7) * h / 8; }

} // namespace res

#endif // RESFORMAT_H
