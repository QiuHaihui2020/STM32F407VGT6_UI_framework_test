/* GlobalSettings.h —— 类名/基类/槽签名来自 ui-tools.exe 的 moc 元数据；
 * 控件名（treeWidget / label_4 / buttonBox，label_4 的样式表是
 * "background-color: rgb(223, 28, 28);" 那条红色警示）来自 uic 的 QStringLiteral。
 *
 * 用途：编辑器的全局偏好。存在 QSettings("QtProject", "UITools")，
 * 和原厂一样是进程级的，不进工程文件。
 */
#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QDialog>
#include <QVariant>

class QTreeWidget;

class GlobalSettings : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalSettings(QWidget *parent = nullptr);
    ~GlobalSettings() override;

    /// 供别处读同一份设置，键名与本对话框里一致
    static QVariant value(const QString &key, const QVariant &def = QVariant());
    static void     setValue(const QString &key, const QVariant &v);

public slots:
    void onAccepted();

private:
    QTreeWidget *m_tree = nullptr;
};

#endif // GLOBALSETTINGS_H
