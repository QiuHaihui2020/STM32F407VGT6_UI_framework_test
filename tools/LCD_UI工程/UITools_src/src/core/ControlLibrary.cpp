#include "ControlLibrary.h"
#include "AppIcon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

bool ControlLibrary::load(const QString &uiToolsRoot, QString *err)
{
    m_root = uiToolsRoot;
    m_controls.clear();

    const QString mainJson = QDir(uiToolsRoot).filePath(QStringLiteral("control/control.json"));
    if (!loadOne(mainJson, err)) {
        return false;
    }
    const int builtinCount = m_controls.size();

    /* 扩展控件：control/ex/*.json。往这个目录丢文件就能加控件，
     * 缺目录不算错误。 */
    QDir exDir(QDir(uiToolsRoot).filePath(QStringLiteral("control/ex")));
    if (exDir.exists()) {
        const QStringList files = exDir.entryList(QStringList() << QStringLiteral("*.json"),
                                                  QDir::Files, QDir::Name);
        for (const QString &fn : files) {
            loadOne(exDir.filePath(fn), nullptr);   // 单个扩展坏了不拖垮整体
        }
    }
    for (int i = builtinCount; i < m_controls.size(); ++i) {
        m_controls[i].isExtension = true;
    }
    return true;
}

bool ControlLibrary::loadOne(const QString &jsonPath, QString *err)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) {
            /* [全局设置]里那一项"控件文件:"指的就是它 */
            *err = QStringLiteral("找不到控件文件,请查看[全局设置]里的路径目录是否正确.");
        }
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) {
            /* 三个占位分别是 错误、偏移、文件 */
            *err = QStringLiteral("读取控件文件遇到错误<%1> 偏移<%2>,请查看检查文件<%3> 格式.")
                   .arg(pe.errorString()).arg(pe.offset).arg(jsonPath);
        }
        return false;
    }

    /* 顶层键名就是 compoents（少了个 n）；两种都认，优先这个写法 */
    QJsonArray arr = doc.object().value(QStringLiteral("compoents")).toArray();
    if (arr.isEmpty()) {
        arr = doc.object().value(QStringLiteral("components")).toArray();
    }
    /* ex/*.json 有时顶层直接就是一个控件对象 */
    if (arr.isEmpty() && doc.object().contains(QStringLiteral("-type"))) {
        arr.append(doc.object());
    }

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ControlTemplate t;
        t.raw      = o;
        t.cls      = o.value(QStringLiteral("-class")).toString();
        t.type     = o.value(QStringLiteral("-type")).toString();
        t.name     = o.value(QStringLiteral("-name")).toString();
        t.caption  = o.value(QStringLiteral("caption")).toString();
        /* json 里用的是 Windows 反斜杠，转成 QDir 能吃的形式 */
        t.iconPath = o.value(QStringLiteral("icon")).toString().replace(QLatin1Char('\\'),
                                                                       QLatin1Char('/'));
        if (!t.iconPath.isEmpty()) {
            const QString p = QDir(m_root).filePath(t.iconPath);
            if (QFileInfo::exists(p)) {
                t.icon = QIcon(p);
            }
        }
        if (t.icon.isNull()) {
            t.icon = AppIcon::get(QStringLiteral("page-new.png"));
        }
        if (!t.type.isEmpty()) {
            m_controls.append(t);
        }
    }
    return true;
}

const ControlTemplate *ControlLibrary::byType(const QString &type) const
{
    /* 【必须大小写不敏感】两份文件对不上：control.json 里数字控件是
     * `-type: "Number"`，而工程文件和 option.ini 里都是小写 `number`
     * （两个工程 15 个数字控件全是小写）。按大小写敏感查，这 15 个节点
     * 取不到模板 —— 补样式、补 ID 号、属性面板全部落空，而且不报错。
     * StyBuilder 早就绕过这一点了（认不出 -type 就退回按中文 caption 查，
     * option.ini 里 `数字=15`）。 */
    for (const ControlTemplate &t : m_controls) {
        if (t.type.compare(type, Qt::CaseInsensitive) == 0) {
            return &t;
        }
    }
    return nullptr;
}
