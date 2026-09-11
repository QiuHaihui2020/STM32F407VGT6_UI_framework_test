/*
 * ProjectFile.h —— 工程文件认哪些后缀。
 *
 * 本工具链自己产出的工程用 .uiproj，既有工程用 .json，**两种都认**。
 *
 * 【后缀只是文件名】工程文件的内容是 json，加载走 QJsonDocument::fromJson()，
 * 不看后缀。改后缀不动内容一个字节，json 往返和标脏重建那两项逐字节验收照过。
 * 换后缀纯粹是为了能把它关联到本工具上 —— 关联 .json 会抢走系统里所有
 * json 文件的双击，那是不能干的事。
 *
 * 【真正选中工程的不是后缀】主链路（step 脚本 / 双击 exe / QtToolBin）读的是
 * config\ini\project.ini 里的 projectfilename，那是个文件名，写什么就是什么。
 * 所以下面这些只用在"列目录"和"文件对话框过滤器"这类地方。
 *
 * 头文件里内联：UITools 和 QtToolBin 是两个各自独立的可执行文件，
 * 没有共用的静态库可挂（和 BuildDate.h / AssetPaths.h 一样的理由）。
 */
#ifndef PROJECTFILE_H
#define PROJECTFILE_H

#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace projectfile {

/** 本工具链新建工程时用的后缀（不带点）。 */
inline QString preferredSuffix()
{
    return QStringLiteral("uiproj");
}

/** 列目录用的通配符，新后缀在前。 */
inline QStringList nameFilters()
{
    return QStringList{ QStringLiteral("*.uiproj"), QStringLiteral("*.json") };
}

/** QFileDialog 的过滤器。 */
inline QString dialogFilter()
{
    return QStringLiteral("UI 工程 (*.uiproj *.json)");
}

/** 这个路径看起来是不是一个工程文件（只看后缀，不读内容）。 */
inline bool isProjectFile(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("uiproj") || s == QLatin1String("json");
}

/**
 * 编辑器的自动存档，不是工程。
 *
 * 【为什么要单独挡】它就躺在工程目录里、后缀也是 json，列表里混进来的话
 * 用户会选到它，下游再拿它去生成 —— 生成出来的是上一次没保存的样子。
 */
inline bool isAutosave(const QString &fileName)
{
    return QFileInfo(fileName).fileName()
           .compare(QLatin1String("autosave.json"), Qt::CaseInsensitive) == 0;
}

} // namespace projectfile

#endif // PROJECTFILE_H
