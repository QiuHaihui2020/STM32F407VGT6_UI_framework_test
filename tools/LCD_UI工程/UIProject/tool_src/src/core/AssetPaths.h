/*
 * AssetPaths.h —— 工具目录里那几份资产在哪。
 *
 * 布局：
 *     <工具目录>/assets/widgets.json       控件模板库
 *     <工具目录>/assets/widgets.d/*.json   自定义控件（"保存成控件"落这儿）
 *     <工具目录>/assets/typecodes.ini      控件类型码表
 *     <工具目录>/assets/canvas/*.jpg       画布背景图
 *     <工具目录>/assets/*.xls              多国语言表
 *
 * 【为什么每一项都带一个旧位置】这些东西以前是散在工具目录根上的
 * （control/control.json、config/ini/option.ini、backgrounds/、根上的 *.xls
 * 和 Resbuilder.xml）。把新 exe 丢进一个老工具目录、或者手上的工程旁边只有
 * 老目录时，只按新路径找会一个都找不到 —— 界面上的表现是"控件库空了"
 * "每个节点都认不出类型码"，还不报错，最难查。所以两套都认：新的在就用新的，
 * 否则退回旧的。两边都没有时仍然返回新路径，让报错信息里出现的是新布局。
 *
 * 【为什么放在头文件里内联】UITools 和 QtToolBin 是两个各自独立的可执行文件，
 * 没有共用的静态库可挂（和 BuildDate.h 一样的理由）。
 */
#ifndef ASSETPATHS_H
#define ASSETPATHS_H

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace assets {

/** 新位置存在就用新的，否则退回旧位置；都没有时返回新位置。 */
inline QString pick(const QString &root, const QString &modern, const QString &legacy)
{
    const QDir d(root);
    const QString m = d.filePath(modern);
    if (QFileInfo::exists(m)) {
        return m;
    }
    const QString l = d.filePath(legacy);
    if (QFileInfo::exists(l)) {
        return l;
    }
    return m;
}

/** 控件模板库。 */
inline QString widgetsJson(const QString &root)
{
    return pick(root, QStringLiteral("assets/widgets.json"),
                QStringLiteral("control/control.json"));
}

/** 自定义控件目录：里面的 *.json 全部当扩展控件加载，文件名随意。 */
inline QString widgetsDir(const QString &root)
{
    return pick(root, QStringLiteral("assets/widgets.d"),
                QStringLiteral("control/ex"));
}

/** 控件类型码表（[Control] 段：控件名 = 类型码）。 */
inline QString typeCodes(const QString &root)
{
    return pick(root, QStringLiteral("assets/typecodes.ini"),
                QStringLiteral("config/ini/option.ini"));
}

/** 画布背景图目录（只认 *.jpg）。 */
inline QString canvasDir(const QString &root)
{
    return pick(root, QStringLiteral("assets/canvas"),
                QStringLiteral("backgrounds"));
}

/**
 * 多国语言表。
 *
 * 【为什么按后缀找而不是写死文件名】表名跟着屏尺寸走（128_64 / 240x240 都有），
 * 写死一个名字换块屏就找不到了。工具目录里就该只有一张。
 *
 * @return 绝对路径；空表示新旧两个位置都没有。
 */
inline QString i18nXls(const QString &root)
{
    const QStringList filter{ QStringLiteral("*.xls") };
    const QStringList dirs{ QDir(root).filePath(QStringLiteral("assets")), root };
    for (const QString &d : dirs) {
        const QStringList f = QDir(d).entryList(filter, QDir::Files, QDir::Name);
        if (!f.isEmpty()) {
            return QDir(d).absoluteFilePath(f.first());
        }
    }
    return QString();
}

/** 这个目录是不是工具目录 —— 以控件库在不在为准。 */
inline bool isToolRoot(const QString &root)
{
    return QFileInfo::exists(widgetsJson(root));
}

} // namespace assets

#endif // ASSETPATHS_H
