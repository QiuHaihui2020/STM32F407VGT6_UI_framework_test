/*
 * AssetPaths.h —— 工具要用的那几份数据在哪。
 *
 * 【只读的编进了 exe，会被写的留在磁盘上】判据只有一条：UI 工作过程中会不会
 * 被改。编进 exe 的东西改不了，所以但凡人要动它、或者程序自己要往里写的，
 * 都必须是磁盘上的真文件。
 *
 *   编进 exe（见 resources/assets.qrc）
 *     :/assets/widgets.json        控件模板库
 *     :/assets/widgets.d/*.json    内置的扩展控件（slider / vslider）
 *     :/assets/typecodes.ini       控件类型码表（值要和固件对齐，不该随手改）
 *
 *   留在磁盘上
 *     <工程>/i18n_*.xls              多国语言表 —— 要改文案；而且它的路径要写进
 *                                    工程的 Resbuilder.xml，由 ResBuilder 这个
 *                                    **另一个进程**按路径打开，塞 :/ 路径没用
 *     <工具目录>/assets/widgets.d/   「保存成控件」往这里写
 *     <工具目录>/assets/canvas/      画布背景图，是给人往里放 jpg 的口子
 *
 * 【每一项磁盘文件都能盖掉内置的】外部有同名文件就用外部的，没有才回落到
 * 内置资源。所以想定制就把文件丢出来改，不定制就一个都不用摆在目录里。
 * 再往前还认一层更老的位置（control/ config/ini/ backgrounds/），这样新 exe
 * 丢进一个老工具目录、或者手上的工程旁边只有老目录时不会静默失效 —— 那种
 * 失效的样子是"控件库空了""每个节点都认不出类型码"，还不报错，最难查。
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

/**
 * 按"外部新位置 -> 外部旧位置 -> 内置资源"挑一个存在的。
 *
 * @param builtin 内置资源路径（:/ 开头）；空表示这一项没有内置版本，
 *                那就退回新位置，让报错信息里出现的是新布局。
 */
inline QString pick(const QString &root, const QString &modern,
                    const QString &legacy, const QString &builtin = QString())
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
    if (!builtin.isEmpty() && QFileInfo::exists(builtin)) {
        return builtin;
    }
    return m;
}

/** 控件模板库。 */
inline QString widgetsJson(const QString &root)
{
    return pick(root, QStringLiteral("assets/widgets.json"),
                QStringLiteral("control/control.json"),
                QStringLiteral(":/assets/widgets.json"));
}

/** 控件类型码表（[Control] 段：控件名 = 类型码）。 */
inline QString typeCodes(const QString &root)
{
    return pick(root, QStringLiteral("assets/typecodes.ini"),
                QStringLiteral("config/ini/option.ini"),
                QStringLiteral(":/assets/typecodes.ini"));
}

/**
 * 自定义控件目录 ——「保存成控件」的落点，**可写**，所以只能是磁盘路径。
 *
 * 目录不存在也照样返回：写的时候会 mkpath 建出来（见 Forms.cpp）。
 * 内置的那两个扩展控件不在这儿，在 builtinWidgetsDir()，两边合并加载。
 */
inline QString widgetsDir(const QString &root)
{
    const QDir d(root);
    const QString legacy = d.filePath(QStringLiteral("control/ex"));
    if (QFileInfo::exists(legacy)) {
        return legacy;
    }
    return d.filePath(QStringLiteral("assets/widgets.d"));
}

/** 内置的扩展控件，编在 exe 里。 */
inline QString builtinWidgetsDir()
{
    return QStringLiteral(":/assets/widgets.d");
}

/** 画布背景图目录（只认 *.jpg），**用户往里放图**，只能是磁盘路径。 */
inline QString canvasDir(const QString &root)
{
    const QDir d(root);
    const QString legacy = d.filePath(QStringLiteral("backgrounds"));
    if (QFileInfo::exists(legacy)) {
        return legacy;
    }
    return d.filePath(QStringLiteral("assets/canvas"));
}

/**
 * 多国语言表。**不内嵌**（要改文案，而且路径要跨进程传给 ResBuilder）。
 *
 * 【为什么按后缀找而不是写死文件名】表名跟着屏尺寸走（128_64 / 240x240 都有），
 * 写死一个名字换块屏就找不到了。
 *
 * @param dir 工程目录或工具目录都行：先看 dir 本身，再看 dir/assets（老布局
 *            把它放在工具目录的 assets 下）。
 * @return 绝对路径；空表示没找到。
 */
inline QString i18nXls(const QString &dir)
{
    const QStringList filter{ QStringLiteral("*.xls") };
    const QStringList dirs{ dir, QDir(dir).filePath(QStringLiteral("assets")) };
    for (const QString &d : dirs) {
        const QStringList f = QDir(d).entryList(filter, QDir::Files, QDir::Name);
        if (!f.isEmpty()) {
            return QDir(d).absoluteFilePath(f.first());
        }
    }
    return QString();
}

/**
 * 这个目录是不是工具目录。
 *
 * 【不能拿控件库在不在来判断】控件库现在编在 exe 里，widgetsJson() 对任何
 * 目录都返回得出东西。改用"三个 exe 在不在"—— 那才是工具目录的定义。
 */
inline bool isToolRoot(const QString &root)
{
    const QDir d(root);
    return QFileInfo::exists(d.filePath(QStringLiteral("UITools.exe")))
        || QFileInfo::exists(d.filePath(QStringLiteral("QtToolBin.exe")))
        || QFileInfo::exists(d.filePath(QStringLiteral("assets/widgets.json")))
        || QFileInfo::exists(d.filePath(QStringLiteral("control/control.json")));
}

} // namespace assets

#endif // ASSETPATHS_H
