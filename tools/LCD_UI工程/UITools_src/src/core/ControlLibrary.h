/*
 * ControlLibrary.h —— 控件库（左侧"组件"面板的数据源）
 *
 * 【好消息】这一块不在程序里，是外部数据文件：
 *   UITools/control/control.json      10 个内置控件模板
 *   UITools/control/ex/*.json         扩展控件（slider / vslider）
 *   UITools/backgrounds/*.json        背景模板
 * 所以控件定义直接读这些文件即可，和既有工程完全兼容。
 *
 * 结构（compat/json_schema.py 归纳）：
 *   { "compoents": [ { -class, -name, -type, caption, icon,
 *                      property:[ { -name, -type, caption, ename, id,
 *                                   default, enum[], list[], struct[],
 *                                   min, max, maxlength, info } ],
 *                      widget: [], tip, version } ] }
 *   注意键名就是拼错的 "compoents"，不是 components。
 */
#ifndef CONTROLLIBRARY_H
#define CONTROLLIBRARY_H

#include <QString>
#include <QVector>
#include <QIcon>
#include <QJsonObject>

/** 一个控件模板。往画布上拖一次就按它克隆出一个 UiNode。 */
struct ControlTemplate {
    QString     cls;        ///< "-class"：NewFrame / NewList / NewGrid ...
    QString     type;       ///< "-type" ：Battery / Text / ImageList / Number ...
    QString     name;       ///< "-name"
    QString     caption;    ///< 面板上显示的中文名
    QString     iconPath;   ///< 相对 UITools 根目录
    QIcon       icon;
    bool        isExtension = false;  ///< 来自 control/ex/ —— 单独放在"自定义控件"组
    QJsonObject raw;        ///< 原始模板，克隆时直接喂给 ProjectModel
};

class ControlLibrary
{
public:
    /** root = UITools 目录（含 control/ backgrounds/ 的那一级）。 */
    bool load(const QString &uiToolsRoot, QString *err = nullptr);

    const QVector<ControlTemplate> &controls() const { return m_controls; }
    const ControlTemplate *byType(const QString &type) const;

    QString root() const { return m_root; }

private:
    bool loadOne(const QString &jsonPath, QString *err);

    QString m_root;
    QVector<ControlTemplate> m_controls;
};

#endif // CONTROLLIBRARY_H
