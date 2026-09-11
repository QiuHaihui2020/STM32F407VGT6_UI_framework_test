/*
 * BuildDate.h —— 标题栏上那个"(Build:YYYY-MM-DD)"。
 *
 * 标题形如 "UI编辑工具(Build:2020-06-09) SmallColorTFT"，
 * 三个工具都跟着来，这样用户报问题时报的标题能直接对上是哪个版本。
 *
 * 【为什么用 __DATE__ 而不是运行时的今天】它标的是"你手上这个 exe 是哪天编的"。
 * 用当天日期的话每天都在变，就失去版本标识的意义了。
 *
 * 头文件里内联，是因为 UITools / QtToolBin / ResBuilder 是三个各自独立的
 * 可执行文件，没有共用的静态库可挂。
 */
#ifndef BUILDDATE_H
#define BUILDDATE_H

#include <QByteArray>
#include <QLatin1Char>
#include <QString>

namespace common {

/** 编译日期，形如 2026-09-09。__DATE__ 是 "Sep  9 2026" 这种格式。 */
inline QString buildDate()
{
    static const char kMon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const QString d = QString::fromLatin1(__DATE__);        // "MMM DD YYYY"
    const int m = (QByteArray(kMon).indexOf(d.left(3).toLatin1()) / 3) + 1;
    const int day = d.mid(4, 2).trimmed().toInt();
    const int year = d.right(4).toInt();
    if (m < 1 || m > 12 || day < 1 || year < 2000) {
        return d;                                            // 格式不认就原样显示
    }
    return QStringLiteral("%1-%2-%3")
           .arg(year, 4, 10, QLatin1Char('0'))
           .arg(m, 2, 10, QLatin1Char('0'))
           .arg(day, 2, 10, QLatin1Char('0'));
}

} // namespace common

#endif // BUILDDATE_H
