#include "Preview.h"

#include "GlobalSettings.h"
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
 * 按 format 拼数字。规则**逐条对着固件抄**，不是照手册猜的：
 *
 * 时间（ui_time.c:65-124 time_vsprintf）
 *   Y -> 4 位，M/D/h/m/s -> 各 2 位，**其余字符一律原样进串**；
 *   然后逐字符换图：数字取 number[d]，非数字取 delimiter[j++]。
 *   分隔符用完（原厂是取到 0xffff）就 **停止渲染后面全部内容**。
 *
 *   【'/' 不是结束符】以前这里把 '/' 特判成"结束符，不画"。固件里它就是个
 *   普通字面字符，一样吃一张分隔符图 —— 工程里 "Y/M/D" 配了 2 张分隔符，
 *   两个 '/' 都是画出来的。"m:s/" 看着像结束符，只是因为它只配了 1 张
 *   分隔符，走到 '/' 时正好用完了才停 —— 结果对，理由错。
 *
 * 数字（ui_number.c:76-143 number_vsprintf）
 *   占位符只认 %0Nd / %Nd / %d 三种（N=1..9），**最多两个**，出现第三个或
 *   写成别的（如 %0x）整个控件就不画了（原厂打印 "not support yet" 后返回）。
 *   %0Nd 补 0，%Nd 补空格（空格取 space[] 那张图），%d 不补。
 *
 * 预览摆的是 0，目的是让人看清"几位数字、多宽、什么字形"，不是显示实时值。
 */
QPixmap composeDigits(UiNode *n, const QColor &lit, bool isTime)
{
    const QStringList digits = listOf(n, QStringLiteral("number"));
    if (digits.isEmpty()) {
        /* number[0] 为空 = 没有字模表，固件走"直接输出 ASCII"那条路，
         * 由业务层/字库决定长什么样，工具画不出来 */
        return QPixmap();
    }
    const QStringList delim = listOf(n, QStringLiteral("delimiter"));
    const QStringList space = listOf(n, QStringLiteral("space"));
    const QString fmt = strOf(n, QStringLiteral("format"), QStringLiteral("default"));

    /* 第一步：按 format 拼出**字符串**（和固件一样先拼串再换图） */
    QString str;
    if (isTime) {
        for (const QChar c : fmt) {
            if (c == QLatin1Char('Y')) {
                str += QStringLiteral("0000");
            } else if (c == QLatin1Char('M') || c == QLatin1Char('D')
                       || c == QLatin1Char('h') || c == QLatin1Char('m')
                       || c == QLatin1Char('s')) {
                str += QStringLiteral("00");
            } else {
                str += c;
            }
        }
    } else {
        int placeholders = 0;
        for (int i = 0; i < fmt.size(); ) {
            if (fmt.at(i) != QLatin1Char('%')) {
                str += fmt.at(i);
                ++i;
                continue;
            }
            if (++placeholders > 2) {
                return QPixmap();               // 第三个占位符 -> 固件不画
            }
            const QChar c1 = (i + 1 < fmt.size()) ? fmt.at(i + 1) : QChar();
            if (c1 == QLatin1Char('0')) {
                const QChar c2 = (i + 2 < fmt.size()) ? fmt.at(i + 2) : QChar();
                const QChar c3 = (i + 3 < fmt.size()) ? fmt.at(i + 3) : QChar();
                if (c2 < QLatin1Char('1') || c2 > QLatin1Char('9')
                    || c3 != QLatin1Char('d')) {
                    return QPixmap();           // %0x 之类 -> 不画
                }
                str += QString(c2.digitValue(), QLatin1Char('0'));
                i += 4;
            } else if (c1 >= QLatin1Char('1') && c1 <= QLatin1Char('9')) {
                if (((i + 2 < fmt.size()) ? fmt.at(i + 2) : QChar()) != QLatin1Char('d')) {
                    return QPixmap();
                }
                /* %Nd 是空格补位：1 位数字 + (N-1) 个空格 */
                str += QString(c1.digitValue() - 1, QLatin1Char(' '))
                       + QLatin1Char('0');
                i += 3;
            } else if (c1 == QLatin1Char('d')) {
                str += QLatin1Char('0');
                i += 2;
            } else {
                return QPixmap();
            }
        }
    }

    /* 第二步：逐字符换成图片。任何一张取不到就**就地截断**，和固件一致。 */
    QVector<QPixmap> parts;
    int j = 0;
    for (const QChar c : str) {
        if (c == QLatin1Char(' ')) {
            if (space.isEmpty()) {
                break;
            }
            parts << litPixmap(abs(space.first()), lit);
        } else if (c.isDigit()) {
            const int d = c.digitValue();
            if (d >= digits.size()) {
                break;
            }
            parts << litPixmap(abs(digits.at(d)), lit);
        } else {
            if (j >= delim.size()) {
                break;                          // 分隔符用完 -> 后面全不画
            }
            parts << litPixmap(abs(delim.at(j)), lit);
            ++j;
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

QString previewKey(const UiNode *n)
{
    if (!n) {
        return QString();
    }
    /* ID号（ename）最稳：它是用户自己起的、面板和树上都看得见，改结构也不会变。
     * 没填 ID号的就退回"在树里的位置"，形如 p0/2/1/3。 */
    for (const UiProperty &p : n->props) {
        if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
            return p.ename;
        }
    }
    QStringList path;
    for (const UiNode *x = n; x && x->parent; x = x->parent) {
        int idx = 0;
        for (const auto &c : x->parent->children) {
            if (c.second == x) {
                break;
            }
            ++idx;
        }
        path.prepend(QString::number(idx));
    }
    return QStringLiteral("@/") + path.join(QLatin1Char('/'));
}

bool needsPresetText(const UiNode *n)
{
    if (!n || n->type != QLatin1String("Text")) {
        return false;
    }
    for (const UiProperty &p : n->props) {
        if (p.name == QLatin1String("code")) {
            const QString c = p.raw.value(QStringLiteral("default")).toString();
            return c == QLatin1String("text") || c == QLatin1String("ascii");
        }
    }
    return false;
}

/** 配置里的键：工程文件名 + 控件标识。不同工程互不干扰。 */
static QString presetSettingsKey(const UiNode *n)
{
    const QString k = previewKey(n);
    if (k.isEmpty()) {
        return QString();
    }
    const QString proj = g_projectDir.isEmpty()
                         ? QStringLiteral("_")
                         : QFileInfo(g_projectDir).fileName();
    return QStringLiteral("previewText/%1/%2").arg(proj, k);
}

QString presetText(const UiNode *n)
{
    const QString key = presetSettingsKey(n);
    if (key.isEmpty()) {
        return QString();
    }
    return GlobalSettings::value(key).toString();
}

void setPresetText(const UiNode *n, const QString &text)
{
    const QString key = presetSettingsKey(n);
    if (key.isEmpty()) {
        return;
    }
    GlobalSettings::setValue(key, text);
    g_cache.clear();          // 文字变了，画好的那张要作废
}

/* ---- 点阵屏预览配色 ----------------------------------------------------
 * 缓存起来：paintEvent 调得很频，每次去翻一遍 QSettings 太亏。
 * 默认值就是改这个功能之前写死在代码里的那两个颜色，所以不配也不会变样。 */
QColor g_lit(0xFF, 0xFF, 0xFF);
QColor g_dark(0x10, 0x10, 0x10);

QColor monoLit()
{
    return g_lit;
}

QColor monoDark()
{
    return g_dark;
}

void reloadMonoColors()
{
    const QColor lit(GlobalSettings::value(QStringLiteral("Preview/LitColor"),
                                           QStringLiteral("#ffffff")).toString());
    const QColor dark(GlobalSettings::value(QStringLiteral("Preview/DarkColor"),
                                            QStringLiteral("#101010")).toString());
    if (lit.isValid()) {
        g_lit = lit;
    }
    if (dark.isValid()) {
        g_dark = dark;
    }
    /* 画好的位图是按 (内容|颜色) 缓存的，颜色换了键就不同，本来不会串。
     * 还是清一次 —— 免得改上几轮之后缓存里堆着一堆再也用不到的配色。 */
    invalidate();
}

QPixmap pictureOf(const QString &path, const QColor &lit)
{
    return path.isEmpty() ? QPixmap() : litPixmap(abs(path), lit);
}

QPixmap contentOf(UiNode *n, const QColor &lit)
{
    if (!n) {
        return QPixmap();
    }
    /* 【点亮就是点亮，不看控件自己的"文字颜色"】
     *
     * 以前这里是：控件配了"文字颜色"就用那个颜色画，没配才用 lit。那是彩屏的
     * 想法 —— 点阵屏上固件只往显存里写"亮/灭"一个 bit（ui_synthesis_oled.c
     * 的 DC_DATA_FORMAT_MONO 分支），屏上每个点亮的像素颜色完全一样，
     * 不可能一个控件白一个控件绿。照工程里的颜色画等于骗人。
     *
     * 而且"文字颜色"那几个值本来就不是颜色，是**魔数**：0x555aaa = 不显示、
     * 0xaaa555 = 反显。语义已经在 textModeOf() 里判过了；再拿它当颜色画，
     * 反显的字就被画成 #AAA555 那个灰紫，而不是"灭"。
     *
     * 现在一律用调用方给的 lit —— 正常是[全局设置]里的点亮色，反显时调用方
     * 传的是熄灭色。 */
    const QColor use = lit;
    const QString type = n->type;

    if (type == QLatin1String("ImageList")) {
        /* 图片列表里可以有多张（切换用），预览画"默认高亮"那一张。
         * 【不能拿 normal_image 的 default 当条目】它几乎都不在 list 里
         * （实测 196/202 是控件模板里的残留，如 config/images/xxx.png），
         * 真正指定条目的是"默认高亮"这个 int8 参数。 */
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
        /* 【取第一条，不要自作聪明取中间】电量图是 BATTLVL1..5 一串，运行时
         * 按真实电量选。工程里没有任何参数指定"预览该显示哪一档"
         * （image 的 default 是 config/images/battery0.png，12/12 都不在
         * list 里），所以只能按列表顺序取第 1 条 —— 这样面板上那个条目
         * 下拉框停在第 1 条，和画布上看到的就是同一张。
         * 以前这里取 pics[size/2]，属性面板上找不到任何依据说明为什么是那张。 */
        return litPixmap(abs(pics.first()), use);
    }

    if (type == QLatin1String("Text")) {
        /* 【只有 strpic 画得出资源里的文字】三个编码格式在固件里是三条完全
         * 不同的路（ui_synthesis_oled.c 764 / 922 / 966）：
         *
         *   strpic  u16 id = text->str[0]; open_string_pic(id) —— 从 result.str
         *           里取**预先光栅化好的图片串**贴上去。内容来自资源。
         *   text    font_open(NULL, language) 打开字库，把 str 当**字符**渲染。
         *           内容由业务层运行时写（ui_text_set_text_by_id 那几个 API）。
         *   ascii   走 ASCII 字模；初始化时 attrs.str 就是 NULL，不写就不画。
         *
         * 【text 下配了文字列表也不会显示那句话】init 时 attrs.str 指向的是
         * _str[]，也就是**u16 的 ResID 数组**，不是字符串。字库把那几个字节
         * 当字符渲染出来是乱码，不是"蓝牙"。所以这里一样不画 —— 画了就是骗人。
         * 原厂工程里 19 个 text、4 个 ascii 控件的文字列表**全是空的**，
         * 正好印证这条。
         *
         * 别的取值（含空串）固件三个分支都不匹配，屏上什么都没有，这里同理。 */
        QString show;               // 最终要画的那句话
        if (strOf(n, QStringLiteral("code"), QStringLiteral("default"))
            != QLatin1String("strpic")) {
            /* text / ascii：资源里没有内容，但可以配一句**只用于预览**的
             * 假文字（存在工具配置里，不进工程文件）—— 排版时能看出这个框
             * 够不够宽、对齐对不对。没配就还是空的。 */
            show = presetText(n);
            if (show.isEmpty()) {
                return QPixmap();
            }
        } else {
            const QStringList ids = listOf(n, QStringLiteral("str"));
            if (ids.isEmpty()) {
                return QPixmap();
            }
            /* 【按 default 取，不是无脑取第一条】str 的 default 一定在 list 里
             * （实测 209/209），它就是"当前显示的那一条" —— 面板上的条目下拉框
             * 也停在它上面。列表有多条时（本工程有 3 个控件是 2~4 条），
             * 取第一条就会和面板显示的对不上。 */
            QString id;
            for (const UiProperty &p : n->props) {
                if (p.name == QLatin1String("str")) {
                    id = p.raw.value(QStringLiteral("default")).toString();
                    break;
                }
            }
            if (id.isEmpty() || !ids.contains(id)) {
                id = ids.first();
            }
            ensureConfig();
            const QVector<int> langsForText = g_cfg.activeLanguages();
            show = stringOf(id, langsForText.isEmpty() ? 0 : langsForText.first());
            if (show.isEmpty()) {
                return QPixmap();
            }
        }

        ensureConfig();
        const QString text = show;
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
