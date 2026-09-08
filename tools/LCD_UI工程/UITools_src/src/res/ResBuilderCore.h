// ResBuilder 的全部产出逻辑。
//
// 输入 : Resbuilder.xml + 里面引用的图片 + 多国语言 .xls
// 输出 : result.bin(.res) / result.str / result.h / res_ver.h /
//        result_pic_index.h / result_str_index.h / result.csv / result.xml
#ifndef RESBUILDERCORE_H
#define RESBUILDERCORE_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ResConfig.h"
#include "ResFormat.h"

namespace res {

struct PictureItem {
    int        id = 0;              ///< 页内 1..N
    QString    symbol;              ///< 宏名（大写）
    QString    path;
    int        width = 0;
    int        height = 0;
    QByteArray data;                ///< OSD1 竖向分页
};

struct StringItem {
    int     id = 0;                 ///< 全局 1..N
    QString cell;                   ///< xls 里的 ResID，如 "m30"
    QString symbol;                 ///< 宏名（大写）
};

struct BuildResult {
    bool        ok = false;
    QString     error;
    QStringList warnings;
    QStringList written;            ///< 实际写出的文件
};

class ResBuilderCore
{
public:
    explicit ResBuilderCore(const ResConfig &cfg) : m_cfg(cfg) {}

    /**
     * @brief 全量构建并落盘到 outDir
     * @note  resver 原厂是每次随机的（空资源文件里也有非零值，且与内容无关），
     *        无法复现。这里改成 **内容的 CRC32**：确定性、可对比，
     *        同时保持 "内容变了版本号就变" 这一原意，固件那边照旧比对通过。
     */
    BuildResult build(const QString &outDir);

private:
    bool collectPictures(BuildResult &r);
    bool collectStrings(BuildResult &r);
    bool renderStrings(BuildResult &r);

    QByteArray buildRes() const;
    QByteArray buildStr() const;
    QVector<quint32> pagePalette(int pageIndex) const;

    QByteArray makeResultH() const;
    QByteArray makeResVerH(quint32 imgVer, quint32 strVer) const;
    QByteArray makePicIndexH() const;
    QByteArray makeStrIndexH() const;
    QByteArray makeCsv() const;
    QByteArray makeXml() const;

    const ResConfig &m_cfg;
    QVector<QVector<PictureItem> > m_pics;      ///< [页][项]
    QVector<StringItem>            m_strings;   ///< 全局，按名字排序，id = 下标+1
    QVector<int>                   m_langs;     ///< 生效语言下标
    QVector<QStringList>           m_xls;       ///< xls 原始表（含表头行）
    QMap<QString, int>             m_cellRow;   ///< 小写 cell -> xls 行号

    struct Raster { int w = 0; int h = 0; QByteArray data; };
    QVector<QVector<Raster> > m_strBits;        ///< [语言][字符串]
    QString m_timestamp;
};

} // namespace res

#endif // RESBUILDERCORE_H
