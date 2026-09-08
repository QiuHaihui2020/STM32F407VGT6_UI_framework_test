#include "Preview.h"

#include "ProjectModel.h"
#include "ResConfig.h"
#include "TextRaster.h"
#include "XlsReader.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

namespace {

QString g_projectDir;
QString g_excelPath;

/** ResID -> 各语言文字。第 0 列是 ResID，第 1 列起是各语言。 */
QHash<QString, QStringList> g_strings;
bool g_stringsLoaded = false;

res::ResConfig g_cfg;
bool g_cfgLoaded = false;

/** 缓存键 = 路径/文字 + 颜色，值是画好的 pixmap。 */
QHash<QString, QPixmap> g_cache;

void ensureConfig()
{
    if (g_cfgLoaded) {
        return;
    }
    g_cfgLoaded = true;
    if (g_projectDir.isEmpty()) {
        return;
    }
    const QString xml = QDir(g_projectDir).filePath(QStringLiteral("Resbuilder.xml"));
    if (QFileInfo::exists(xml)) {
        g_cfg.load(xml, nullptr);       // 读不出来就用结构体里的默认值
    }
}

void ensureStrings()
{
    if (g_stringsLoaded) {
        return;
    }
    g_stringsLoaded = true;
    if (g_excelPath.isEmpty() || !QFileInfo::exists(g_excelPath)) {
        return;
    }
    res::XlsReader xls;
    if (!xls.load(g_excelPath) || xls.sheets().isEmpty()) {
        return;
    }
    const res::XlsSheet &sh = xls.sheets().first();
    /* 第 0 行是表头（ResID, Chinese_Simplified, …），数据从第 1 行起。
     * 【不要 trim】尾随空格在这套资源里是有意义的（原厂 5 条英文就靠它多占
     * 8 px 宽），见 re/verify_str.py。 */
    for (int r = 1; r < sh.rows.size(); ++r) {
        const QString id = sh.cell(r, 0).trimmed();     // ResID 本身可以 trim
        if (id.isEmpty()) {
            continue;
        }
        QStringList langs;
        for (int c = 1; c < sh.rows.at(r).size(); ++c) {
            langs << sh.cell(r, c);
        }
        g_strings.insert(id, langs);
    }
}

/** 工程里的相对路径 -> 绝对路径。 */
QString abs(const QString &rel)
{
    if (rel.isEmpty()) {
        return QString();
    }
    if (QFileInfo(rel).isAbsolute()) {
        return rel;
    }
    return QDir(g_projectDir).filePath(rel);
}

/**
 * 位图 -> 只有"点亮"像素的 pixmap。
 *
 * 判定和 res::toMono() 一模一样：**颜色 != 透明色就算点亮**。透明色取
 * Resbuilder.xml 的 <bmp_transparent_color>（本工程 0x00FFFFFF = 白）。
 * 半透明像素先合到 png_background_color 上再判，也和那边一致。
 */
QPixmap litPixmap(const QString &absPath, const QColor &lit)
{
    ensureConfig();
    const QString key = QStringLiteral("P|%1|%2").arg(absPath, lit.name());
    auto it = g_cache.constFind(key);
    if (it != g_cache.constEnd()) {
        return it.value();
    }

    QImage src;
    QPixmap out;
    if (!absPath.isEmpty() && src.load(absPath)) {
        src = src.convertToFormat(QImage::Format_ARGB32);
        QImage dst(src.size(), QImage::Format_ARGB32_Premultiplied);
        dst.fill(Qt::transparent);
        const quint32 keyRgb = g_cfg.bmpTransparentColor & 0x00FFFFFFu;
        const int bgR = int((g_cfg.pngBackgroundColor >> 16) & 0xFF);
        const int bgG = int((g_cfg.pngBackgroundColor >> 8) & 0xFF);
        const int bgB = int(g_cfg.pngBackgroundColor & 0xFF);
        const QRgb litRgb = lit.rgba();
        for (int y = 0; y < src.height(); ++y) {
            const QRgb *in = reinterpret_cast<const QRgb *>(src.constScanLine(y));
            QRgb *o = reinterpret_cast<QRgb *>(dst.scanLine(y));
            for (int x = 0; x < src.width(); ++x) {
                const QRgb c = in[x];
                const int a = qAlpha(c);
                int r = qRed(c), g = qGreen(c), b = qBlue(c);
                if (a != 255) {
                    r = (r * a + bgR * (255 - a)) / 255;
                    g = (g * a + bgG * (255 - a)) / 255;
                    b = (b * a + bgB * (255 - a)) / 255;
                }
                const quint32 rgb = (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
                o[x] = (rgb != keyRgb) ? litRgb : 0u;
            }
        }
        out = QPixmap::fromImage(dst);
    }
    g_cache.insert(key, out);
    return out;
}

/** 竖向分页的 1bpp 点阵 -> pixmap。位序和 res::toMono() 写出来的一致。 */
QPixmap monoToPixmap(const res::TextBitmap &bm, const QColor &lit)
{
    if (!bm.ok || bm.width <= 0 || bm.height <= 0) {
        return QPixmap();
    }
    QImage img(bm.width, bm.height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const uchar *d = reinterpret_cast<const uchar *>(bm.data.constData());
    const QRgb litRgb = lit.rgba();
    for (int y = 0; y < bm.height; ++y) {
        QRgb *o = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < bm.width; ++x) {
            const int idx = (y / 8) * bm.width + x;
            if (idx < bm.data.size() && (d[idx] & (1u << (y % 8)))) {
                o[x] = litRgb;
            }
        }
    }
    return QPixmap::fromImage(img);
}

/** 取节点某条属性的 list[]。 */
QStringList listOf(UiNode *n, const QString &propName)
{
    QStringList out;
    for (const UiProperty &p : n->props) {
        if (p.name != propName) {
            continue;
        }
        for (const QJsonValue &v : p.raw.value(QStringLiteral("list")).toArray()) {
            out << v.toString();
        }
        break;
    }
    return out;
}

/** 取节点某条属性的字符串值（default / 指定键）。 */
QString strOf(UiNode *n, const QString &propName, const QString &key)
{
    for (const UiProperty &p : n->props) {
        if (p.name == propName) {
            return p.raw.value(key).toString();
        }
    }
    return QString();
}

/** 取"文字颜色"这类颜色属性；取不到返回无效色。 */
QColor colorOf(UiNode *n, const QString &caption)
{
    for (const UiProperty &p : n->props) {
        if (p.caption != caption) {
            continue;
        }
        /* 这类属性在 json 里有两种写法：-type=color 存 "color"，
         * -type=background-color 存 "background-color"。两种都认。 */
        QString s = p.raw.value(QStringLiteral("color")).toString();
        if (s.isEmpty()) {
            s = p.raw.value(QStringLiteral("background-color")).toString();
        }
        if (s.isEmpty()) {
            continue;
        }
        /* 工程里是 #AARRGGBB（如 #ffffffff）。QColor 认 #AARRGGBB，
         * 但 8 位十六进制它按 #RRGGBBAA 解 —— 手动拆一下免得颜色错位。 */
        if (s.size() == 9 && s.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint v = s.mid(1).toUInt(&ok, 16);
            if (ok) {
                return QColor(int((v >> 16) & 0xFF), int((v >> 8) & 0xFF),
                              int(v & 0xFF), int((v >> 24) & 0xFF));
            }
        }
        const QColor c(s);
        if (c.isValid()) {
            return c;
        }
    }
    return QColor();
}

/** 横向把几张图拼起来（数字、时间就是这么组的）。 */
QPixmap hcat(const QVector<QPixmap> &parts)
{
    int w = 0, h = 0;
    for (const QPixmap &p : parts) {
        if (p.isNull()) {
            continue;
        }
        w += p.width();
        h = qMax(h, p.height());
    }
    if (w <= 0 || h <= 0) {
        return QPixmap();
    }
    QPixmap out(w, h);
    out.fill(Qt::transparent);
    QPainter p(&out);
    int x = 0;
    for (const QPixmap &one : parts) {
        if (one.isNull()) {
            continue;
        }
        p.drawPixmap(x, 0, one);
        x += one.width();
    }
    return out;
}

/**
 * 按 format 拼数字。
 *
 * 时间的 format 关键字是 Y M D h m s（手册 2.8），数字是 printf 那套
 * "%02d"（手册 2.9）。预览用不着真值，摆一串 0 就行 —— 目的是让人看清
 * "这里有几位数字、多宽、什么字形"，不是显示实时数据。
 */
QPixmap composeDigits(UiNode *n, const QColor &lit, bool isTime)
{
    const QStringList digits = listOf(n, QStringLiteral("number"));
    if (digits.isEmpty()) {
        return QPixmap();
    }
    const QStringList delim = listOf(n, QStringLiteral("delimiter"));
    const QString fmt = strOf(n, QStringLiteral("format"), QStringLiteral("default"));

    QVector<QPixmap> parts;
    int delimUsed = 0;
    auto digit = [&](int d) {
        return litPixmap(abs(digits.value(qBound(0, d, digits.size() - 1))), lit);
    };
    auto nextDelim = [&]() {
        const QPixmap p = litPixmap(abs(delim.value(delimUsed)), lit);
        if (delimUsed + 1 < delim.size()) {
            ++delimUsed;
        }
        return p;
    };

    if (isTime) {
        /* "m:s/" —— Y/M/D/h/m/s 各占两位，其余字符走分隔符图片 */
        for (int i = 0; i < fmt.size(); ++i) {
            const QChar c = fmt.at(i);
            if (c == QLatin1Char('Y') || c == QLatin1Char('M') || c == QLatin1Char('D')
                || c == QLatin1Char('h') || c == QLatin1Char('m') || c == QLatin1Char('s')) {
                parts << digit(0) << digit(0);
            } else if (c == QLatin1Char('/')) {
                continue;                       // 结束符，不画
            } else {
                parts << nextDelim();
            }
        }
    } else {
        /* "%02d" / "%4d" —— 取宽度，不足补 0 */
        int i = 0;
        while (i < fmt.size()) {
            if (fmt.at(i) != QLatin1Char('%')) {
                parts << nextDelim();
                ++i;
                continue;
            }
            ++i;
            QString wide;
            while (i < fmt.size() && fmt.at(i).isDigit()) {
                wide.append(fmt.at(i));
                ++i;
            }
            if (i < fmt.size() && fmt.at(i) == QLatin1Char('d')) {
                ++i;
            }
            int cnt = wide.isEmpty() ? 1 : wide.toInt();
            /* "%02d" 的前导 0 是补位标志不是位数，去掉它再算 */
            if (wide.startsWith(QLatin1Char('0')) && wide.size() > 1) {
                cnt = wide.mid(1).toInt();
            }
            for (int k = 0; k < qMax(1, cnt); ++k) {
                parts << digit(0);
            }
        }
    }
    return hcat(parts);
}

} // namespace

namespace {

/** "#AARRGGBB"/"#RRGGBB" -> RGB565；空串或解不出来返回 -1。 */
int to565(const QString &css)
{
    if (css.isEmpty()) {
        return -1;
    }
    QString h = css;
    if (h.startsWith(QLatin1Char('#'))) {
        h = h.mid(1);
    }
    bool ok = false;
    const uint v = h.toUInt(&ok, 16);
    if (!ok) {
        return -1;
    }
    const uint rgb = (h.size() == 8) ? (v & 0x00FFFFFFu) : v;
    const int r = int((rgb >> 16) & 0xFF);
    const int g = int((rgb >> 8) & 0xFF);
    const int b = int(rgb & 0xFF);
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* 固件 ui_synthesis_oled.c 里的魔数，降到 565 之后再比 —— 和固件一致 */
const int k565_555AAA = to565(QStringLiteral("555AAA"));
const int k565_AAA555 = to565(QStringLiteral("AAA555"));

} // namespace

namespace Preview {

const char *const kMonoFillOn = "#ff555aaa";
const char *const kMonoInvert = "#ffaaa555";
const char *const kMonoLit    = "#ffffffff";

bool isMonoLayer(const UiNode *n)
{
    for (const UiNode *a = n; a; a = a->parent) {
        if (a->cls != QLatin1String("NewLayer")) {
            continue;
        }
        for (const UiProperty &p : a->props) {
            if (p.name == QLatin1String("color_format")) {
                return p.raw.value(QStringLiteral("default")).toString()
                       != QLatin1String("OSD16");
            }
        }
        return true;                    // 图层上没写就按 OSD1 算
    }
    return true;                        // 找不到图层同上
}

MonoFill fillOf(const QString &cssColor)
{
    /* jlui_fill_rect: color == BGC_MONO_SET ? 点亮 : 清除
     * 注意空串（"没设背景色"）也是清除。 */
    return to565(cssColor) == k565_555AAA ? MonoFill::Set : MonoFill::None;
}

MonoText textModeOf(const QString &cssColor)
{
    const int c = to565(cssColor);
    if (c == k565_AAA555) {
        return MonoText::Invert;
    }
    if (c == k565_555AAA) {
        return MonoText::Hidden;
    }
    return MonoText::Normal;
}

bool borderVisible(const QString &cssColor)
{
    /* jlui_draw_rect: color != RECT_MONO_CLR 才画 —— 和背景的方向相反 */
    return to565(cssColor) != k565_555AAA;
}

void setProject(const QString &projectDir, const QString &excelPath)
{
    if (g_projectDir == projectDir && g_excelPath == excelPath) {
        return;
    }
    g_projectDir = projectDir;
    g_excelPath = excelPath;
    invalidate();
}

void invalidate()
{
    g_cache.clear();
    g_strings.clear();
    g_stringsLoaded = false;
    g_cfg = res::ResConfig();
    g_cfgLoaded = false;
}

QString stringOf(const QString &resId, int langIndex)
{
    ensureStrings();
    const QStringList langs = g_strings.value(resId);
    return langs.value(langIndex);
}

int cacheCount()
{
    return g_cache.size();
}

QPixmap contentOf(UiNode *n, const QColor &lit)
{
    if (!n) {
        return QPixmap();
    }
    const QColor own = colorOf(n, QStringLiteral("文字颜色"));
    const QColor use = own.isValid() && own.alpha() > 0 ? own : lit;
    const QString type = n->type;

    if (type == QLatin1String("ImageList")) {
        /* 图片列表里可以有多张（切换用），预览画"默认高亮"那一张 */
        const QStringList pics = listOf(n, QStringLiteral("normal_image"));
        if (pics.isEmpty()) {
            return QPixmap();
        }
        int idx = 0;
        for (const UiProperty &p : n->props) {
            if (p.name == QLatin1String("highlight")) {
                idx = p.raw.value(QStringLiteral("default")).toInt();
                break;
            }
        }
        return litPixmap(abs(pics.value(qBound(0, idx, pics.size() - 1))), use);
    }

    if (type == QLatin1String("Battery")) {
        const QStringList pics = listOf(n, QStringLiteral("image"));
        if (pics.isEmpty()) {
            return QPixmap();
        }
        /* 电量图是 0%..100% 一串，预览取中间那档最有代表性 */
        return litPixmap(abs(pics.value(pics.size() / 2)), use);
    }

    if (type == QLatin1String("Text")) {
        const QStringList ids = listOf(n, QStringLiteral("str"));
        if (ids.isEmpty()) {
            return QPixmap();
        }
        ensureConfig();
        const QVector<int> langsForText = g_cfg.activeLanguages();
        const QString text = stringOf(ids.first(),
                                      langsForText.isEmpty() ? 0 : langsForText.first());
        if (text.isEmpty()) {
            return QPixmap();
        }
        ensureConfig();
        const QString key = QStringLiteral("T|%1|%2").arg(text, use.name(QColor::HexArgb));
        auto it = g_cache.constFind(key);
        if (it != g_cache.constEnd()) {
            return it.value();
        }
        /* 用生成 result.str 的那同一个光栅化器 —— 画布上看到的字形就是烧进
         * 设备里的字形。
         *
         * 【字体不能直接取 fonts[0]】Resbuilder.xml 的 <Fonts> 里 font00..05
         * 写的是 -32，但原厂产出的 result.str 全是 **16px 宋体** —— 也就是说
         * 原厂根本没按语言下标去用那张表（见 ResBuilderCore::renderStrings()
         * 和 docs/FILE_FORMATS.md 10.5）。照搬 fonts[0] 的话，「蓝牙」会渲成
         * 64x32，塞进 32x16 的控件里只能看到一角。
         * 这里和 ResBuilderCore 用同一条规则：默认宋体 -16，只有当表里那项
         * **高度也一样**时才采用它。 */
        res::LogFontSpec f;
        const QVector<int> langs = g_cfg.activeLanguages();
        const int lang = langs.isEmpty() ? 0 : langs.first();
        if (lang >= 0 && lang < g_cfg.fonts.size()
            && qAbs(g_cfg.fonts.at(lang).height) == qAbs(f.height)) {
            f = g_cfg.fonts.at(lang);
        }
        const QPixmap pm = monoToPixmap(res::rasterize(text, f), use);
        g_cache.insert(key, pm);
        return pm;
    }

    if (type == QLatin1String("Time")) {
        return composeDigits(n, use, true);
    }
    if (type.compare(QLatin1String("number"), Qt::CaseInsensitive) == 0) {
        return composeDigits(n, use, false);
    }
    return QPixmap();
}

} // namespace Preview
