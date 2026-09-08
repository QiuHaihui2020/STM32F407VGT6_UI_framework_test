// 位图 -> OSD1（1bpp 竖向分页）转换。
//
// 固件取像素的方式（ui_synthesis_oled.c:488）：
//     offset = (y / 8) * width + x ;   bit = y % 8 ;   1 = 点亮
// 也就是标准的 OLED "页" 格式，行距 = width（不是 (w+7)/8）。
#ifndef IMAGEMONO_H
#define IMAGEMONO_H

#include <QByteArray>
#include <QImage>
#include <QString>

namespace res {

struct MonoImage {
    int        width = 0;
    int        height = 0;
    QByteArray data;                 ///< width * ((height+7)/8) 字节
    bool       ok = false;
    QString    error;
};

/**
 * @brief 读图并转成 OSD1 竖向分页位图
 * @param path            图片路径（BMP/PNG/JPG 都行，走 Qt 的图像插件）
 * @param transparentRgb  透明色（Resbuilder.xml 的 bmp_transparent_color，
 *                        典型值 0x00FFFFFF）。**颜色不等于它的像素才点亮**。
 * @param pngBackgroundRgb 带 alpha 的图先合成到这个底色上再判定
 * @note  实测 104/104 张原厂图片按这个规则转换后与 result.bin 里的字节完全一致。
 */
MonoImage toMono(const QString &path, quint32 transparentRgb = 0x00FFFFFFu,
                 quint32 pngBackgroundRgb = 0x00000000u);

/// 同上，但直接给 QImage（供文字光栅化复用）
MonoImage toMono(const QImage &img, quint32 transparentRgb = 0x00FFFFFFu,
                 quint32 pngBackgroundRgb = 0x00000000u);

} // namespace res

#endif // IMAGEMONO_H
