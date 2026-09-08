// Resbuilder.xml —— ResBuilder 的唯一输入描述文件（由 QtToolBin 生成）。
#ifndef RESCONFIG_H
#define RESCONFIG_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "TextRaster.h"

namespace res {

struct PageConfig {
    int         id = 0;
    QStringList colors;         ///< "RRGGBB" 大写，顺序即调色板顺序
    QStringList pictures;       ///< 图片绝对路径
    QStringList pictureFmts;    ///< 与 pictures 等长，"OSD1"/"RGB565"…
    QStringList cells;          ///< xls 里的 ResID（如 "m37"）
};

struct ResConfig {
    QStringList          langKeys;       ///< LANG 属性，如 "Chinese_Simplified"
    QStringList          langNames;      ///< 中文显示名
    QVector<LogFontSpec> fonts;          ///< font00..font21
    QString              endian = QStringLiteral("LITTLEENDIAN");
    QString              panelType = QStringLiteral("LCDPANEL");
    QString              picturePath;
    QString              excelPath;
    QVector<PageConfig>  pages;
    quint32              language = 0;              ///< 语言位掩码
    quint32              bmpTransparentColor = 0x00FFFFFFu;
    quint32              pngBackgroundColor = 0x00000000u;
    QString              specColorList;
    QString              png2jpgList;
    QString              res = QStringLiteral("result");
    QString              resFileName = QStringLiteral("result.bin");
    QString              headerFileName = QStringLiteral("result.h");
    QString              imageCompress = QStringLiteral("none");
    QString              stringCompress = QStringLiteral("none");
    QString              paletteType = QStringLiteral("rgb");
    QString              percent = QStringLiteral("100%");
    QString              excelCrc;
    QString              excelRow;
    int                  rotate = 0;

    /// 语言位掩码 -> 语言下标（0 基，对应 langKeys / xls 列 1+i）
    QVector<int> activeLanguages() const;

    quint16 panelTypeCode() const;

    /**
     * @brief 读 Resbuilder.xml
     * @param path 文件路径；相对路径按该文件所在目录解析
     */
    bool load(const QString &path, QString *error = nullptr);

    QString baseDir;            ///< Resbuilder.xml 所在目录
};

} // namespace res

#endif // RESCONFIG_H
