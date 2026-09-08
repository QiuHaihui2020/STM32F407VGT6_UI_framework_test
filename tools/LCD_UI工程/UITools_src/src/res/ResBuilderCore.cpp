#include "ResBuilderCore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>

#include "DefaultPalette.h"
#include "ImageMono.h"
#include "XlsReader.h"

namespace res {

namespace {

void putU16(QByteArray &b, quint16 v)
{
    char t[2];
    qToLittleEndian(v, reinterpret_cast<uchar *>(t));
    b.append(t, 2);
}

void putU32(QByteArray &b, quint32 v)
{
    char t[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(t));
    b.append(t, 4);
}

/// 生成 RES_BMP_T（20 字节），head_crc 自算
QByteArray bmpEntry(quint16 dataCrc, quint16 resType, quint16 typeId,
                    quint16 w, quint16 h, quint32 len, quint32 off)
{
    QByteArray rec;
    putU16(rec, dataCrc);
    putU16(rec, resType);
    putU16(rec, typeId);
    putU16(rec, w);
    putU16(rec, h);
    putU32(rec, len);
    putU32(rec, off);
    QByteArray out;
    putU16(out, crc16(rec));            // head_crc 覆盖后面 18 字节
    out += rec;
    return out;
}

/// 文件名 -> 宏名：去扩展名、转大写、非标识符字符换成下划线
QString symbolOf(const QString &fileName)
{
    QString s = QFileInfo(fileName).completeBaseName().toUpper();
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != QLatin1Char('_')) {
            s[i] = QLatin1Char('_');
        }
    }
    if (!s.isEmpty() && s.at(0).isDigit()) {
        s.prepend(QLatin1Char('_'));
    }
    return s;
}

QByteArray toAnsi(const QString &s)
{
    // 原厂这几个 .h / 路径注释是本地代码页（简中机器上就是 GBK）。
    // 保持一致，否则 Keil/GCC 读注释里的中文路径会花屏（虽然不影响编译）。
    return s.toLocal8Bit();
}

QString pad(const QString &s, int width)
{
    return s.leftJustified(width, QLatin1Char(' '));
}

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i) {
        c = table[(c ^ quint8(data.at(i))) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

} // namespace

// ---------------------------------------------------------------- 收集 ----

bool ResBuilderCore::collectPictures(BuildResult &r)
{
    m_pics.clear();
    for (const PageConfig &pg : m_cfg.pages) {
        QVector<PictureItem> items;
        for (int i = 0; i < pg.pictures.size(); ++i) {
            PictureItem it;
            it.path = pg.pictures.at(i);
            it.symbol = symbolOf(it.path);
            items.append(it);
        }
        // 原厂按宏名（大写）ASCII 升序分配 id，实测 104/104 吻合
        std::sort(items.begin(), items.end(),
                  [](const PictureItem &a, const PictureItem &b) {
                      return a.symbol < b.symbol;
                  });
        for (int i = 0; i < items.size(); ++i) {
            PictureItem &it = items[i];
            it.id = i + 1;
            QString path = it.path;
            if (!QFileInfo(path).isAbsolute()) {
                path = QDir(m_cfg.baseDir).absoluteFilePath(path);
            }
            if (!QFile::exists(path)) {
                r.error = QStringLiteral("图片不存在: %1").arg(path);
                return false;
            }
            const MonoImage m = toMono(path, m_cfg.bmpTransparentColor,
                                       m_cfg.pngBackgroundColor);
            if (!m.ok) {
                r.error = m.error;
                return false;
            }
            it.width = m.width;
            it.height = m.height;
            it.data = m.data;
        }
        m_pics.append(items);
    }
    return true;
}

bool ResBuilderCore::collectStrings(BuildResult &r)
{
    m_strings.clear();
    QSet<QString> seen;
    QStringList names;
    for (const PageConfig &pg : m_cfg.pages) {
        for (const QString &c : pg.cells) {
            if (c.isEmpty() || seen.contains(c)) {
                continue;
            }
            seen.insert(c);
            names.append(c);
        }
    }
    names.sort();                       // 与图片同一规则：按名字排序后编号
    for (int i = 0; i < names.size(); ++i) {
        StringItem s;
        s.id = i + 1;
        s.cell = names.at(i);
        s.symbol = names.at(i).toUpper();
        m_strings.append(s);
    }

    if (m_cfg.excelPath.isEmpty()
        || m_cfg.excelPath.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0) {
        if (!m_strings.isEmpty()) {
            r.warnings.append(QStringLiteral("有 %1 条字符串但没有 excel_path")
                              .arg(m_strings.size()));
        }
        return true;
    }
    QString xlsPath = m_cfg.excelPath;
    if (!QFileInfo(xlsPath).isAbsolute()) {
        xlsPath = QDir(m_cfg.baseDir).absoluteFilePath(xlsPath);
    }
    XlsReader xls;
    if (!xls.load(xlsPath)) {
        r.error = QStringLiteral("读 %1 失败: %2").arg(xlsPath, xls.errorString());
        return false;
    }
    m_xls = xls.sheets().first().rows;
    for (int i = 1; i < m_xls.size(); ++i) {
        const QString id = m_xls.at(i).value(0).trimmed().toLower();
        if (!id.isEmpty()) {
            m_cellRow.insert(id, i);
        }
    }
    for (const StringItem &s : m_strings) {
        if (!m_cellRow.contains(s.cell.toLower())) {
            r.warnings.append(QStringLiteral("xls 里没有 %1，按空串处理").arg(s.cell));
        }
    }
    return true;
}

bool ResBuilderCore::renderStrings(BuildResult &r)
{
    Q_UNUSED(r)
    m_langs = m_cfg.activeLanguages();
    m_strBits.clear();
    for (int li = 0; li < m_langs.size(); ++li) {
        const int lang = m_langs.at(li);
        // 字体：原厂 Resbuilder.xml 的 <Fonts> 里 font00..05 是 -32，
        // 但产出的 result.str 全是 16 px 宋体 —— 也就是说原厂**没有**按语言下标
        // 去用那张表（详见 docs/FILE_FORMATS.md 10.5）。这里以宋体 -16 为默认，
        // 只有当 <Fonts> 明确给出**同样高度**的项时才采用它，避免莫名其妙换字体。
        LogFontSpec f;
        if (lang < m_cfg.fonts.size()) {
            const LogFontSpec &x = m_cfg.fonts.at(lang);
            if (qAbs(x.height) == qAbs(f.height)) {
                f = x;
            }
        }
        QVector<Raster> row;
        for (const StringItem &s : m_strings) {
            QString text;
            const int rowIdx = m_cellRow.value(s.cell.toLower(), -1);
            if (rowIdx >= 0) {
                text = m_xls.at(rowIdx).value(1 + lang);
            }
            const TextBitmap tb = rasterize(text, f);
            Raster ras;
            ras.w = tb.width;
            ras.h = tb.height;
            ras.data = tb.data;
            row.append(ras);
        }
        m_strBits.append(row);
    }
    return true;
}

// -------------------------------------------------------------- 调色板 ----

QVector<quint32> ResBuilderCore::pagePalette(int pageIndex) const
{
    const QStringList &used = m_cfg.pages.at(pageIndex).colors;
    QVector<quint32> out;
    out.append(PALETTE_FIRST);
    QSet<QString> usedSet;
    for (const QString &c : used) {
        bool ok = false;
        out.append(c.toUInt(&ok, 16));
        usedSet.insert(c.toUpper());
    }
    const int room = PALETTE_COLORS - out.size();
    int taken = 0;
    for (int i = 0; i < DEFAULT_PALETTE_COUNT && taken < room; ++i) {
        const quint32 c = DEFAULT_PALETTE[i];
        const QString hex = QString::asprintf("%06X", c);
        if (usedSet.contains(hex)) {
            continue;               // 已经排在前面了，不重复
        }
        out.append(c);
        ++taken;
    }
    while (out.size() < PALETTE_COLORS) {
        out.append(0);
    }
    out.resize(PALETTE_COLORS);
    return out;
}

// ------------------------------------------------------------ 二进制区 ----

QByteArray ResBuilderCore::buildRes() const
{
    const int nPage = m_pics.size();

    // 先算每页的起始地址：布局是完全确定的，没有对齐/填充
    QVector<quint32> pageAddr(nPage);
    quint32 cur = quint32(HEAD_SZ + PAGE_SZ * nPage);
    for (int p = 0; p < nPage; ++p) {
        pageAddr[p] = cur;
        const int n = m_pics.at(p).size();
        quint32 sz = ENTRY_SZ * 2 + PAL_SZ + quint32(BMP_SZ) * n + PALETTE_BYTES;
        for (const PictureItem &it : m_pics.at(p)) {
            sz += quint32(it.data.size());
        }
        cur += sz;
    }

    QByteArray out;
    out.reserve(int(cur));
    out.append("RU21", 4);
    putU16(out, 0x0101);
    putU16(out, m_cfg.panelTypeCode());
    putU16(out, quint16(nPage));
    putU16(out, 0);
    putU32(out, 0);                             // resver 稍后回填
    for (int p = 0; p < nPage; ++p) {
        putU32(out, quint32(m_cfg.pages.at(p).id));
        putU32(out, pageAddr.at(p));
    }

    for (int p = 0; p < nPage; ++p) {
        const QVector<PictureItem> &items = m_pics.at(p);
        const quint32 base = pageAddr.at(p);
        const quint32 palTabOff = base + ENTRY_SZ * 2;
        const quint32 bmpTabOff = palTabOff + PAL_SZ;
        const quint32 palDataOff = bmpTabOff + quint32(BMP_SZ) * items.size();
        const quint32 pixOff = palDataOff + PALETTE_BYTES;

        // 调色板索引项
        putU32(out, palTabOff);
        putU16(out, 1);
        out.append(char(ITEM_PALETTE));
        out.append(char(0));
        putU32(out, 0);
        // 图片索引项
        putU32(out, bmpTabOff);
        putU16(out, quint16(items.size()));
        out.append(char(ITEM_PICTURE));
        out.append(char(0));
        putU32(out, 0);
        // RES_PAL_T
        putU32(out, PALETTE_COLORS);
        putU32(out, palDataOff);
        putU32(out, PALETTE_BYTES);

        quint32 off = pixOff;
        for (const PictureItem &it : items) {
            const quint16 typeId = quint16((CMP_NONE << 13) | (FMT_OSD1 << 10)
                                           | (it.id & 0x3FF));
            // data_crc：原厂在 .res 里恒写 0（固件读单色图时不校验），照抄
            out += bmpEntry(0, RES_PICTURE, typeId, quint16(it.width),
                            quint16(it.height),
                            quint32(legacyLength(it.width, it.height)), off);
            off += quint32(it.data.size());
        }

        const QVector<quint32> pal = pagePalette(p);
        for (int i = 0; i < PALETTE_COLORS; ++i) {
            const quint32 c = pal.at(i);
            out.append(char(c & 0xFF));             // B
            out.append(char((c >> 8) & 0xFF));      // G
            out.append(char((c >> 16) & 0xFF));     // R
            out.append(char(0));
        }
        for (const PictureItem &it : items) {
            out += it.data;
        }
    }

    const quint32 ver = crc32(out.mid(HEAD_SZ));
    qToLittleEndian(ver, reinterpret_cast<uchar *>(out.data()) + 12);
    return out;
}

QByteArray ResBuilderCore::buildStr() const
{
    const int nLang = m_langs.size();
    const int perLang = m_strings.size();
    const int total = nLang * perLang;

    QByteArray out;
    out.append("RU21", 4);
    putU16(out, 0x0101);
    putU16(out, m_cfg.panelTypeCode());
    putU16(out, quint16(nLang));                // .str 这里放语言数
    putU16(out, 0);
    putU32(out, 0);                             // resver 回填

    const quint32 tabOff = HEAD_SZ + ENTRY_SZ;
    putU32(out, tabOff);
    putU16(out, quint16(total));
    out.append(char(ITEM_STRING));
    out.append(char(nLang));
    putU32(out, m_cfg.language);

    quint32 off = tabOff + quint32(BMP_SZ) * total;
    QByteArray table, pixels;
    int seq = 0;
    for (int li = 0; li < nLang; ++li) {
        for (int k = 0; k < perLang; ++k) {
            const Raster &ras = m_strBits.at(li).at(k);
            // typeId 里的 id 是**跨语言的流水号** 1..wCount，不是每种语言各自从 1 开始。
            // 固件 open_string_pic 用 (wCount/langsum)*(lang-1)+id 算表下标，
            // 压根不读这个字段，它只是个计数器——但要和原厂一致就得这么写。
            ++seq;
            const quint16 typeId = quint16((CMP_NONE << 13) | (FMT_OSD1 << 10)
                                           | (seq & 0x3FF));
            table += bmpEntry(crc16(ras.data), RES_STRING, typeId,
                              quint16(ras.w), quint16(ras.h),
                              quint32(ras.data.size()), off);
            pixels += ras.data;
            off += quint32(ras.data.size());
        }
    }
    out += table;
    out += pixels;

    const quint32 ver = crc32(out.mid(HEAD_SZ));
    qToLittleEndian(ver, reinterpret_cast<uchar *>(out.data()) + 12);
    return out;
}

// ------------------------------------------------------------ 文本产出 ----

QByteArray ResBuilderCore::makeResultH() const
{
    QString s;
    s += QLatin1String("//generated by ResBuilder in 2017//\r\n");
    s += QLatin1String("#ifndef __RESULT_H__\r\n#define __RESULT_H__\r\n\r\n");
    for (int i = 0; i < m_cfg.langKeys.size(); ++i) {
        s += QLatin1String("#define  ") + pad(m_cfg.langKeys.at(i), 25)
             + pad(QString::number(i + 1), 4)
             + QLatin1String("//") + m_cfg.langNames.value(i) + QLatin1String("\r\n");
    }
    s += QLatin1String("\r\n#define  ") + pad(QStringLiteral("LANGUAGEID_SUM"), 25)
         + QString::number(m_cfg.langKeys.size()) + QLatin1String("\r\n\r\n\r\n#endif\r\n");
    return toAnsi(s);
}

QByteArray ResBuilderCore::makeResVerH(quint32 imgVer, quint32 strVer) const
{
    QString s;
    s += QLatin1String("#ifndef __RES_VER_H__\r\n#define __RES_VER_H__\r\n\r\n");
    s += QStringLiteral("/* Generated By ResBuilder In %1 */\r\n\r\n").arg(m_timestamp);
    s += QStringLiteral("#define IMAGE_VERION  0x%1\r\n").arg(imgVer, 8, 16, QLatin1Char('0'));
    s += QStringLiteral("#define STRING_VERION 0x%1\r\n").arg(strVer, 8, 16, QLatin1Char('0'));
    s += QLatin1String("\r\n#endif\r\n");
    return toAnsi(s);
}

QByteArray ResBuilderCore::makePicIndexH() const
{
    QString s;
    s += QStringLiteral("//generated by ResBuilder in %1\r\n").arg(m_timestamp);
    s += QLatin1String("#ifndef _RESULT_PIC_INDEX_H_  \r\n#define _RESULT_PIC_INDEX_H_  \r\n\r\n");
    s += QLatin1String("////BmpResID Define Table////\r\n");
    for (int p = 0; p < m_pics.size(); ++p) {
        s += QStringLiteral("\r\n//PAGE %1\r\n").arg(m_cfg.pages.at(p).id);
        for (const PictureItem &it : m_pics.at(p)) {
            s += QLatin1String("#define  ") + pad(it.symbol, 26)
                 + pad(QString::number(it.id), 8)
                 + QLatin1String("//") + QDir::toNativeSeparators(it.path)
                 + QLatin1String("\r\n");
        }
    }
    s += QLatin1String("\r\n#endif\r\n");
    return toAnsi(s);
}

QByteArray ResBuilderCore::makeStrIndexH() const
{
    QString s;
    s += QStringLiteral("//generated by ResBuilder in %1\r\n").arg(m_timestamp);
    s += QLatin1String("#ifndef _RESULT_STR_INDEX_H_  \r\n#define _RESULT_STR_INDEX_H_  \r\n\r\n");
    s += QLatin1String("////StrResID Define Table////\r\n");
    // 原厂是**按页**输出的：每页把自己用到的 cell 排序后各打一遍，
    // 所以跨页共用的 cell 会重复 #define（值相同，编译器不报错）。
    // id 则是全局的（全部 cell 去重排序后 1..N），见 result.xml 里的 <Cell id>。
    QMap<QString, int> idOf;
    for (const StringItem &it : m_strings) {
        idOf.insert(it.cell, it.id);
    }
    for (const PageConfig &pg : m_cfg.pages) {
        QStringList names = pg.cells;
        names.sort();
        for (const QString &c : names) {
            s += QLatin1String("#define  ") + pad(c.toUpper(), 26)
                 + pad(QString::number(idOf.value(c)), 4) + QLatin1String("\r\n");
        }
    }
    s += QLatin1String("\r\n#endif\r\n");
    return toAnsi(s);
}

QByteArray ResBuilderCore::makeCsv() const
{
    // UTF-16LE + BOM，字段分隔是 ",\t"，每行末尾也有一个 ",\t"，行尾 CRLF
    QString s;
    const QString sep = QStringLiteral(",\t");
    if (!m_xls.isEmpty()) {
        for (int r = 0; r < m_xls.size(); ++r) {
            const QStringList &row = m_xls.at(r);
            if (row.value(0).trimmed().isEmpty()) {
                continue;
            }
            const int cols = 1 + m_cfg.langKeys.size();
            for (int c = 0; c < cols; ++c) {
                s += row.value(c) + sep;
            }
            s += QLatin1String("\r\n");
        }
    }
    QByteArray out;
    out.append(char(0xFF));
    out.append(char(0xFE));
    const ushort *u = s.utf16();
    for (int i = 0; i < s.size(); ++i) {
        out.append(char(u[i] & 0xFF));
        out.append(char((u[i] >> 8) & 0xFF));
    }
    return out;
}

QByteArray ResBuilderCore::makeXml() const
{
    QString s;
    s += QLatin1String("<?xml version=\"2.0\" encoding=\"UTF-8\"?>\r\n");
    s += QLatin1String("<Resbuilder>\r\n    <PageList>\r\n");
    for (int p = 0; p < m_cfg.pages.size(); ++p) {
        s += QStringLiteral("        <Page id=\"%1\">\r\n").arg(m_cfg.pages.at(p).id);
        s += QLatin1String("            <ColorList>\r\n");
        const QVector<quint32> pal = pagePalette(p);
        for (int i = 0; i < pal.size(); ++i) {
            const QString hex = QString::asprintf("%06X", pal.at(i));
            s += QStringLiteral("                <Color id=\"%1\">%2</Color>\r\n")
                 .arg(i).arg(hex);
        }
        s += QLatin1String("            </ColorList>\r\n            <PictureList>\r\n");
        for (const PictureItem &it : m_pics.at(p)) {
            s += QStringLiteral("                <Picture id=\"%1\">%2</Picture>\r\n")
                 .arg(it.id).arg(QDir::toNativeSeparators(it.path));
        }
        s += QLatin1String("            </PictureList>\r\n            <CellList>\r\n");
        for (const StringItem &it : m_strings) {
            if (!m_cfg.pages.at(p).cells.contains(it.cell)) {
                continue;
            }
            s += QStringLiteral("                <Cell id=\"%1\">%2</Cell>\r\n")
                 .arg(it.id).arg(it.cell);
        }
        s += QLatin1String("            </CellList>\r\n        </Page>\r\n");
    }
    s += QLatin1String("    </PageList>\r\n</Resbuilder>\r\n");

    QByteArray out;
    out.append("\xEF\xBB\xBF", 3);
    out += s.toUtf8();
    return out;
}

// ------------------------------------------------------------------ 主 ----

BuildResult ResBuilderCore::build(const QString &outDir)
{
    BuildResult r;
    m_timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    if (!collectPictures(r) || !collectStrings(r) || !renderStrings(r)) {
        return r;
    }

    const QByteArray resBin = buildRes();
    const QByteArray strBin = buildStr();
    const quint32 imgVer = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(resBin.constData()) + 12);
    const quint32 strVer = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(strBin.constData()) + 12);

    struct Out { QString name; QByteArray data; };
    const QVector<Out> files = {
        { m_cfg.resFileName.isEmpty() ? QStringLiteral("result.bin") : m_cfg.resFileName,
          resBin },
        { QStringLiteral("result.str"),          strBin },
        { m_cfg.headerFileName.isEmpty() ? QStringLiteral("result.h") : m_cfg.headerFileName,
          makeResultH() },
        { QStringLiteral("res_ver.h"),           makeResVerH(imgVer, strVer) },
        { QStringLiteral("result_pic_index.h"),  makePicIndexH() },
        { QStringLiteral("result_str_index.h"),  makeStrIndexH() },
        { QStringLiteral("result.csv"),          makeCsv() },
        { QStringLiteral("result.xml"),          makeXml() },
    };

    QDir dir(outDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        r.error = QStringLiteral("建不了输出目录 %1").arg(outDir);
        return r;
    }
    for (const Out &o : files) {
        const QString path = dir.absoluteFilePath(o.name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            r.error = QStringLiteral("写不了 %1").arg(path);
            return r;
        }
        f.write(o.data);
        f.close();
        r.written.append(path);
    }
    r.ok = true;
    return r;
}

} // namespace res
