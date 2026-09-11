#include "ResConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace res {

namespace {

quint32 parseNum(const QString &s, quint32 def)
{
    QString t = s.trimmed();
    if (t.isEmpty() || t.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0) {
        return def;
    }
    bool ok = false;
    const quint32 v = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                      ? t.mid(2).toUInt(&ok, 16) : t.toUInt(&ok, 10);
    return ok ? v : def;
}

int attrInt(const QXmlStreamAttributes &a, const char *name, int def)
{
    const QString v = a.value(QLatin1String(name)).toString();
    bool ok = false;
    const int r = v.toInt(&ok);
    return ok ? r : def;
}

} // namespace

QVector<int> ResConfig::activeLanguages() const
{
    QVector<int> out;
    for (int i = 0; i < 32; ++i) {
        if (language & (1u << i)) {
            out.append(i);
        }
    }
    return out;
}

quint16 ResConfig::panelTypeCode() const
{
    // 实测：LCDPANEL 落盘为 0。空工程里出现过 1（OLEDPANEL）。
    return panelType.compare(QLatin1String("OLEDPANEL"), Qt::CaseInsensitive) == 0 ? 1 : 0;
}

bool ResConfig::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("打不开 %1").arg(path);
        }
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();
    baseDir = QFileInfo(path).absolutePath();

    // 编码：写的确实是 UTF-8（现成的 UITools/Resbuilder.xml 里
    // "多"是 E5 A4 9A、"宋体"是 E5 AE 8B E4 BD 93），和 XML 头声明的一致。
    // 但历史上本工具自己写出过 GBK 的版本（toLocal8Bit，见 StyBuilder.cpp 里
    // 那段说明），所以读的时候仍然两种都认：先按 UTF-8 试，解出替换字符或者
    // 回写对不上原字节就改按本地代码页。然后把 QString 直接喂给 reader
    // （给 QString 时 QXmlStreamReader 不再看编码声明）。
    QString text = QString::fromUtf8(raw);
    if (text.contains(QChar(0xFFFD)) || text.toUtf8() != raw) {
        text = QString::fromLocal8Bit(raw);
    }
    // XML 头写的是 version='2.0'（世上并没有 XML 2.0），QXmlStreamReader
    // 会直接报 "Unsupported XML version"。改成 1.0 再解析。
    if (text.startsWith(QLatin1String("<?xml"))) {
        const int end = text.indexOf(QLatin1String("?>"));
        if (end > 0) {
            QString decl = text.left(end);
            decl.replace(QLatin1String("version='2.0'"), QLatin1String("version='1.0'"));
            decl.replace(QLatin1String("version=\"2.0\""), QLatin1String("version=\"1.0\""));
            text = decl + text.mid(end);
        }
    }
    QXmlStreamReader xr(text);
    PageConfig page;
    bool inPage = false;
    QString lastPicFmt;

    while (!xr.atEnd()) {
        const QXmlStreamReader::TokenType t = xr.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const QString name = xr.name().toString();
            const QXmlStreamAttributes at = xr.attributes();

            if (name == QLatin1String("language_name")) {
                langKeys.append(at.value(QLatin1String("LANG")).toString());
                langNames.append(xr.readElementText());
            } else if (name.startsWith(QLatin1String("font"))
                       && name.size() == 6 && name.at(4).isDigit()) {
                LogFontSpec lf;
                lf.faceName       = at.value(QLatin1String("lfFaceName")).toString();
                lf.height         = attrInt(at, "lfHeight", -16);
                lf.width          = attrInt(at, "lfWidth", 0);
                lf.escapement     = attrInt(at, "lfEscapement", 0);
                lf.orientation    = attrInt(at, "lfOrientation", 0);
                lf.weight         = attrInt(at, "lfWeight", 400);
                lf.italic         = attrInt(at, "lfItalic", 0);
                lf.underline      = attrInt(at, "lfUnderline", 0);
                lf.strikeOut      = attrInt(at, "lfStrikeOut", 0);
                lf.charSet        = attrInt(at, "lfCharSet", 134);
                lf.outPrecision   = attrInt(at, "lfOutPrecision", 0);
                lf.clipPrecision  = attrInt(at, "lfClipPrecision", 0);
                lf.quality        = attrInt(at, "lfQuality", 0);
                lf.pitchAndFamily = attrInt(at, "lfPitchAndFamily", 2);
                fonts.append(lf);
            } else if (name == QLatin1String("Page")) {
                page = PageConfig();
                page.id = attrInt(at, "id", pages.size());
                inPage = true;
            } else if (name == QLatin1String("Color") && inPage) {
                page.colors.append(xr.readElementText().trimmed().toUpper());
            } else if (name == QLatin1String("Picture") && inPage) {
                lastPicFmt = at.value(QLatin1String("fmt")).toString();
                page.pictures.append(xr.readElementText().trimmed());
                page.pictureFmts.append(lastPicFmt.isEmpty() ? QStringLiteral("OSD1")
                                                             : lastPicFmt);
            } else if (name == QLatin1String("Cell") && inPage) {
                page.cells.append(xr.readElementText().trimmed());
            } else if (name == QLatin1String("endian")) {
                endian = xr.readElementText().trimmed();
            } else if (name == QLatin1String("paneltype")) {
                panelType = xr.readElementText().trimmed();
            } else if (name == QLatin1String("picture_path")) {
                picturePath = xr.readElementText().trimmed();
            } else if (name == QLatin1String("excel_path")) {
                excelPath = xr.readElementText().trimmed();
            } else if (name == QLatin1String("language")) {
                language = parseNum(xr.readElementText(), 0);
            } else if (name == QLatin1String("bmp_transparent_color")) {
                bmpTransparentColor = parseNum(xr.readElementText(), 0x00FFFFFFu);
            } else if (name == QLatin1String("png_background_color")) {
                pngBackgroundColor = parseNum(xr.readElementText(), 0);
            } else if (name == QLatin1String("spec_color_list")) {
                specColorList = xr.readElementText().trimmed();
            } else if (name == QLatin1String("png2jpg_list")) {
                png2jpgList = xr.readElementText().trimmed();
            } else if (name == QLatin1String("res")) {
                res = xr.readElementText().trimmed();
            } else if (name == QLatin1String("resfilename")) {
                resFileName = xr.readElementText().trimmed();
            } else if (name == QLatin1String("headerfilename")) {
                headerFileName = xr.readElementText().trimmed();
            } else if (name == QLatin1String("image_compress_method")) {
                imageCompress = xr.readElementText().trimmed();
            } else if (name == QLatin1String("string_compress_method")) {
                stringCompress = xr.readElementText().trimmed();
            } else if (name == QLatin1String("palette_type")) {
                paletteType = xr.readElementText().trimmed();
            } else if (name == QLatin1String("percent")) {
                percent = xr.readElementText().trimmed();
            } else if (name == QLatin1String("excel_crc")) {
                excelCrc = xr.readElementText().trimmed();
            } else if (name == QLatin1String("excel_row")) {
                excelRow = xr.readElementText().trimmed();
            } else if (name == QLatin1String("rotate")) {
                rotate = xr.readElementText().trimmed().toInt();
            }
        } else if (t == QXmlStreamReader::EndElement
                   && xr.name() == QLatin1String("Page")) {
            pages.append(page);
            inPage = false;
        }
    }
    if (xr.hasError()) {
        if (error) {
            *error = QStringLiteral("XML 解析失败: %1").arg(xr.errorString());
        }
        return false;
    }
    return true;
}

} // namespace res
