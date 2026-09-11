#include "AppIcon.h"

#include <QAbstractButton>
#include <QAction>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QWidget>

namespace {

/** 记名字用的动态属性键。refresh() 靠它找出哪些图标该跟着主题换。 */
const char *const kProp = "appIconName";

/** 深色主题下主色墨换成这个。比纯白柔一点，长时间看不刺眼。 */
const QRgb kLightInk = qRgb(0xC8, 0xD1, 0xDA);

bool g_dark = false;
QHash<QString, QIcon> g_cache;

/** 这两个是带底色的完整图形，换色反而不对。 */
bool keepAsIs(const QString &file)
{
    return file == QLatin1String("app.png") || file == QLatin1String("logo.png");
}

/**
 * 把"近中性的深色"像素换成浅墨，别的一概不动。
 *
 * 判据是 HSV 里的饱和度和明度：
 *   · 主色墨 #37474F  饱和度 77、明度 79   -> 命中，换掉
 *   · 强调蓝 #1E88E5  饱和度 221           -> 不动
 *   · 新增绿 / 删除红 饱和度 148 / 196      -> 不动
 *   · 白色（徽标里的加号）明度 255          -> 不动
 * 边缘的半透明像素颜色也接近主色墨，一起换掉，所以描边不会留一圈脏边。
 */
QImage recolored(const QImage &src)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            const QColor c = QColor::fromRgb(qRed(px), qGreen(px), qBlue(px));
            if (c.saturation() < 110 && c.value() < 160) {
                line[x] = qRgba(qRed(kLightInk), qGreen(kLightInk),
                                qBlue(kLightInk), a);
            }
        }
    }
    return img;
}

} // namespace

namespace AppIcon {

bool isDark()
{
    return g_dark;
}

void setDark(bool dark)
{
    if (g_dark == dark) {
        return;
    }
    g_dark = dark;
    g_cache.clear();
}

QIcon get(const QString &file)
{
    const QString key = (g_dark ? QStringLiteral("d|") : QStringLiteral("l|")) + file;
    const auto it = g_cache.constFind(key);
    if (it != g_cache.constEnd()) {
        return it.value();
    }
    const QString path = QStringLiteral(":/icons/") + file;
    QIcon ic;
    if (g_dark && !keepAsIs(file)) {
        QImage img(path);
        ic = img.isNull() ? QIcon(path) : QIcon(QPixmap::fromImage(recolored(img)));
    } else {
        ic = QIcon(path);
    }
    g_cache.insert(key, ic);
    return ic;
}

void apply(QAction *act, const QString &file)
{
    if (!act) {
        return;
    }
    act->setProperty(kProp, file);
    act->setIcon(get(file));
}

void apply(QAbstractButton *btn, const QString &file)
{
    if (!btn) {
        return;
    }
    btn->setProperty(kProp, file);
    btn->setIcon(get(file));
}

void refresh(QWidget *root)
{
    if (!root) {
        return;
    }
    for (QAction *a : root->findChildren<QAction *>()) {
        const QVariant v = a->property(kProp);
        if (v.isValid()) {
            a->setIcon(get(v.toString()));
        }
    }
    for (QAbstractButton *b : root->findChildren<QAbstractButton *>()) {
        const QVariant v = b->property(kProp);
        if (v.isValid()) {
            b->setIcon(get(v.toString()));
        }
    }
}

} // namespace AppIcon
