#include "ImageMono.h"

#include <QFileInfo>

namespace res {

MonoImage toMono(const QImage &src, quint32 transparentRgb, quint32 pngBackgroundRgb)
{
    MonoImage m;
    if (src.isNull()) {
        m.error = QStringLiteral("图像为空");
        return m;
    }

    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    m.width = img.width();
    m.height = img.height();
    const int pages = (m.height + 7) / 8;
    m.data.fill('\0', m.width * pages);
    uchar *out = reinterpret_cast<uchar *>(m.data.data());

    const int bgR = (pngBackgroundRgb >> 16) & 0xFF;
    const int bgG = (pngBackgroundRgb >> 8) & 0xFF;
    const int bgB = pngBackgroundRgb & 0xFF;
    const quint32 key = transparentRgb & 0x00FFFFFFu;

    for (int y = 0; y < m.height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < m.width; ++x) {
            const QRgb c = line[x];
            const int a = qAlpha(c);
            int r = qRed(c), g = qGreen(c), b = qBlue(c);
            if (a != 255) {
                // 半透明先合到 png_background_color 上，再按普通像素判定
                r = (r * a + bgR * (255 - a)) / 255;
                g = (g * a + bgG * (255 - a)) / 255;
                b = (b * a + bgB * (255 - a)) / 255;
            }
            const quint32 rgb = (quint32(r) << 16) | (quint32(g) << 8) | quint32(b);
            if (rgb != key) {
                out[(y / 8) * m.width + x] |= static_cast<uchar>(1u << (y % 8));
            }
        }
    }
    m.ok = true;
    return m;
}

MonoImage toMono(const QString &path, quint32 transparentRgb, quint32 pngBackgroundRgb)
{
    MonoImage m;
    QImage img;
    if (!img.load(path)) {
        m.error = QStringLiteral("打不开图片: %1").arg(QFileInfo(path).fileName());
        return m;
    }
    return toMono(img, transparentRgb, pngBackgroundRgb);
}

} // namespace res
