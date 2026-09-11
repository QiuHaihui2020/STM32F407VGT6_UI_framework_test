// 字符串 -> 1bpp 点阵。
//
// 字符串图是 GDI 光栅化出来的
// （Resbuilder.xml 里存的就是一整套 LOGFONT 字段，摆明了直接喂 CreateFontIndirect）。
// 这里在 Windows 上就调同一套 GDI，所以点阵和既有资源**逐字节相同**——
// 实测 141/141 条（简中/繁中/英文三语）全中，见 compat/verify_str.py。
//
// 非 Windows 平台退化为 QPainter 渲染：能用，但字形不保证一致。
#ifndef TEXTRASTER_H
#define TEXTRASTER_H

#include <QByteArray>
#include <QString>

namespace res {

/// 对应 Resbuilder.xml <Fonts> 里的一项，字段名与 Win32 LOGFONT 一一对应
struct LogFontSpec {
    QString faceName = QStringLiteral("宋体");
    int  height = -16;          ///< lfHeight，负值表示字符高度（非单元格高度）
    int  width = 0;
    int  escapement = 0;
    int  orientation = 0;
    int  weight = 400;
    int  italic = 0;
    int  underline = 0;
    int  strikeOut = 0;
    int  charSet = 134;         ///< GB2312_CHARSET
    int  outPrecision = 0;
    int  clipPrecision = 0;
    int  quality = 0;           ///< 0 = DEFAULT_QUALITY，务必别改成 ANTIALIASED
    int  pitchAndFamily = 2;
};

struct TextBitmap {
    int        width = 0;       ///< GetTextExtentPoint32 的 cx，不取整
    int        height = 0;      ///< |lfHeight| 向上取到 8 的倍数（竖向分页）
    QByteArray data;            ///< width * ((height+7)/8)，竖向分页
    bool       ok = false;
};

/**
 * @brief 把一行文字渲染成 OSD1 竖向分页点阵
 * @param text 原文；**不要 trim**，尾随空格会真实占宽（有 5 条英文就是这样多出 8 px）
 * @param f    字体
 * @note 宽度 = GetTextExtentPoint32W(text).cx 向上取整到 8；文字画在 (0,0)，
 *       背景清 0、前景置 1。
 */
TextBitmap rasterize(const QString &text, const LogFontSpec &f);

/// 只量宽度（不渲染），生成 result.xml 之类时要用
int textExtent(const QString &text, const LogFontSpec &f);

} // namespace res

#endif // TEXTRASTER_H
