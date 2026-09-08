// 最小 .xls（BIFF8）读取器：够读原厂那张"多国语言"表。
//
// 为什么自己写：原厂 ResBuilder 靠 Excel/OLE 读表，重建版不想拖一个 Excel 依赖，
// 也不想拖第三方库。BIFF8 里我们只需要 SST + LABELSST/LABEL，量很小。
//
// .xls 是 CFBF（复合文档）容器：512 字节头 -> DIFAT -> FAT -> 目录项，
// 目标是名为 "Workbook"（老版本 "Book"）的流。流里是 BIFF 记录：
// u16 type, u16 len, payload；超长记录用 CONTINUE(0x003C) 续写。
#ifndef XLSREADER_H
#define XLSREADER_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace res {

/// 一张工作表：行 x 列的字符串矩阵（数值单元格转成字符串）
struct XlsSheet {
    QString name;
    QVector<QStringList> rows;

    QString cell(int r, int c) const
    {
        if (r < 0 || r >= rows.size()) {
            return QString();
        }
        const QStringList &row = rows.at(r);
        return (c >= 0 && c < row.size()) ? row.at(c) : QString();
    }
};

class XlsReader
{
public:
    /// 读取整个工作簿；失败返回 false，原因在 errorString()
    bool load(const QString &path);

    const QVector<XlsSheet> &sheets() const { return m_sheets; }
    QString errorString() const { return m_error; }

private:
    QVector<XlsSheet> m_sheets;
    QString m_error;
};

} // namespace res

#endif // XLSREADER_H
