#include "TextRaster.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <QVarLengthArray>
#else
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include "ImageMono.h"
#endif

namespace res {

#ifdef Q_OS_WIN

namespace {

HFONT createFont(const LogFontSpec &f)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight         = f.height;
    lf.lfWidth          = f.width;
    lf.lfEscapement     = f.escapement;
    lf.lfOrientation    = f.orientation;
    lf.lfWeight         = f.weight;
    lf.lfItalic         = static_cast<BYTE>(f.italic);
    lf.lfUnderline      = static_cast<BYTE>(f.underline);
    lf.lfStrikeOut      = static_cast<BYTE>(f.strikeOut);
    lf.lfCharSet        = static_cast<BYTE>(f.charSet);
    lf.lfOutPrecision   = static_cast<BYTE>(f.outPrecision);
    lf.lfClipPrecision  = static_cast<BYTE>(f.clipPrecision);
    lf.lfQuality        = static_cast<BYTE>(f.quality);
    lf.lfPitchAndFamily = static_cast<BYTE>(f.pitchAndFamily);
    const int n = qMin(f.faceName.size(), LF_FACESIZE - 1);
    memcpy(lf.lfFaceName, f.faceName.utf16(), size_t(n) * sizeof(wchar_t));
    lf.lfFaceName[n] = 0;
    return CreateFontIndirectW(&lf);
}

} // namespace

int textExtent(const QString &text, const LogFontSpec &f)
{
    if (text.isEmpty()) {
        return 0;
    }
    HDC hdc = CreateCompatibleDC(NULL);
    HFONT hf = createFont(f);
    HGDIOBJ old = SelectObject(hdc, hf);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(hdc, reinterpret_cast<const wchar_t *>(text.utf16()),
                          text.size(), &sz);
    SelectObject(hdc, old);
    DeleteObject(hf);
    DeleteDC(hdc);
    return sz.cx;
}

TextBitmap rasterize(const QString &text, const LogFontSpec &f)
{
    TextBitmap out;
    out.height = qAbs(f.height);
    if (out.height <= 0) {
        return out;
    }

    HDC hdc = CreateCompatibleDC(NULL);
    HFONT hf = createFont(f);
    HGDIOBJ oldFont = SelectObject(hdc, hf);

    SIZE sz = {0, 0};
    if (!text.isEmpty()) {
        GetTextExtentPoint32W(hdc, reinterpret_cast<const wchar_t *>(text.utf16()),
                              text.size(), &sz);
    }
    out.width = (sz.cx + 7) / 8 * 8;
    if (out.width <= 0) {
        SelectObject(hdc, oldFont);
        DeleteObject(hf);
        DeleteDC(hdc);
        out.ok = true;                      // 空串 -> 0 宽，合法
        return out;
    }

    // 1bpp、自上而下的 DIB。调色板 index0=白(背景) index1=黑(前景)，
    // 于是 DIB 里的 bit 就直接是 "点亮" 语义，省一次取反。
    struct { BITMAPINFOHEADER h; DWORD pal[2]; } bi;
    memset(&bi, 0, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = out.width;
    bi.h.biHeight = -out.height;            // 负 = top-down
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 1;
    bi.pal[0] = 0x00FFFFFF;
    bi.pal[1] = 0x00000000;

    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO *>(&bi),
                                   DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        SelectObject(hdc, oldFont);
        DeleteObject(hf);
        DeleteDC(hdc);
        return out;
    }
    HGDIOBJ oldBm = SelectObject(hdc, hbm);

    const int stride = (out.width + 31) / 32 * 4;
    // 注意：不能用 PatBlt(WHITENESS/BLACKNESS) 清底——单色 DIB 下它按
    // "物理调色板索引" 填，方向和上面自定义的调色板相反，会把留白填成 1。
    memset(bits, 0, size_t(stride) * out.height);

    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, RGB(255, 255, 255));
    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutW(hdc, 0, 0, reinterpret_cast<const wchar_t *>(text.utf16()), text.size());
    GdiFlush();

    const int pages = (out.height + 7) / 8;
    out.data.fill('\0', out.width * pages);
    uchar *dst = reinterpret_cast<uchar *>(out.data.data());
    const uchar *src = static_cast<const uchar *>(bits);
    for (int y = 0; y < out.height; ++y) {
        const uchar *line = src + size_t(y) * stride;
        for (int x = 0; x < out.width; ++x) {
            if ((line[x >> 3] >> (7 - (x & 7))) & 1) {
                dst[(y / 8) * out.width + x] |= static_cast<uchar>(1u << (y % 8));
            }
        }
    }

    SelectObject(hdc, oldBm);
    DeleteObject(hbm);
    SelectObject(hdc, oldFont);
    DeleteObject(hf);
    DeleteDC(hdc);
    out.ok = true;
    return out;
}

#else   // 非 Windows：用 Qt 渲染，字形不保证与原厂一致

namespace {
QFont toQFont(const LogFontSpec &f)
{
    QFont qf(f.faceName);
    qf.setPixelSize(qAbs(f.height));
    qf.setWeight(f.weight / 8);             // GDI 100..900 -> Qt 0..99
    qf.setItalic(f.italic != 0);
    qf.setUnderline(f.underline != 0);
    qf.setStrikeOut(f.strikeOut != 0);
    qf.setStyleStrategy(QFont::NoAntialias);
    return qf;
}
}

int textExtent(const QString &text, const LogFontSpec &f)
{
    return QFontMetrics(toQFont(f)).horizontalAdvance(text);
}

TextBitmap rasterize(const QString &text, const LogFontSpec &f)
{
    TextBitmap out;
    out.height = qAbs(f.height);
    const int adv = textExtent(text, f);
    out.width = (adv + 7) / 8 * 8;
    if (out.width <= 0 || out.height <= 0) {
        out.ok = true;
        return out;
    }
    QImage img(out.width, out.height, QImage::Format_Mono);
    img.setColor(0, qRgb(255, 255, 255));
    img.setColor(1, qRgb(0, 0, 0));
    img.fill(0);
    QPainter p(&img);
    QFont qf = toQFont(f);
    p.setFont(qf);
    p.setPen(QColor(0, 0, 0));
    p.drawText(0, QFontMetrics(qf).ascent(), text);
    p.end();

    MonoImage m = toMono(img, 0x00FFFFFFu);
    out.data = m.data;
    out.ok = m.ok;
    return out;
}

#endif

} // namespace res
