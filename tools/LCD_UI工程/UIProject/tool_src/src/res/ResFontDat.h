// Resbuilder.dat —— 字符串表的**逐格字体**。
//
// ResBuilder 是个表格界面：行是 ResID、列是语言，用户可以选中某一格
// 单独改字体。这份选择存在工程目录的 Resbuilder.dat 里，**不**写进
// Resbuilder.xml —— xml 的 <Fonts> 只有 font00..21 那 22 条按语言的默认字体。
//
// 证据（jl701n SDK 那份 oled 工程）：result.str 210 条里有 27 条宽度和本版对不上，
// 位置正好是 m46..m54 这 9 个 ResID × 简体/繁体/英文 3 列；而 Resbuilder.dat 里
// 恰好有 27 条 lfHeight=-11（其余全是 -16），行列是 47..55 × 2/3/6，逐格吻合。
//
// 格式（全部小端）：
//     +0   "1.2\0"          版本
//     +4   记录数组，每条 132 字节：
//              +0    6 字节保留（多数为 0）
//              +6    LOGFONTW，92 字节
//              +98   u32 行号（1 起，**第 1 行是表头**，第 2 行才是 xls 的首行数据）
//              +102  u32 列号（1 起，第 1 列是 ResID，第 2 列起是各语言）
//              +106  u16 该格是否有内容
//              +108  24 字节其余界面状态（列宽、颜色…），与光栅化无关
//     末尾 6 字节 0
#ifndef RESFONTDAT_H
#define RESFONTDAT_H

#include <QHash>
#include <QString>

#include "TextRaster.h"

namespace res {

class ResFontDat
{
public:
    /**
     * @brief 读一份 Resbuilder.dat
     * @param path 文件路径；不存在或格式不认识都返回 false（调用方退回默认字体）
     * @return 读成功
     */
    bool load(const QString &path);

    /// 有没有读到可用的逐格字体
    bool isValid() const
    {
        return !m_cells.isEmpty();
    }

    /**
     * @brief 取某一格的字体
     * @param row 行号，1 起，1 是表头
     * @param col 列号，1 起，1 是 ResID 列
     * @param out 读到就写入
     * @return 这一格在文件里存在
     */
    bool fontAt(int row, int col, LogFontSpec *out) const;

private:
    static quint32 key(int row, int col)
    {
        return (quint32(row) << 8) | quint32(col & 0xFF);
    }

    QHash<quint32, LogFontSpec> m_cells;
};

} // namespace res

#endif // RESFONTDAT_H
