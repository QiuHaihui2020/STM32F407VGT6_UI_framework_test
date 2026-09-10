/* GlobalSettings.h —— 类名/基类/槽签名来自 ui-tools.exe 的 moc 元数据；
 * 控件名（treeWidget / label_4 / buttonBox，label_4 的样式表是
 * "background-color: rgb(223, 28, 28);" 那条红色警示）来自 uic 的 QStringLiteral。
 *
 * 用途：原厂[全局设置]，六项 —— 界面尺寸 + 五条路径。
 *
 * 【存哪儿】原厂存在**工程目录**下的 "Application Data/ui-config"
 * （QSettings::IniFormat，相对启动时的当前目录；启动脚本是 cd project 之后
 * 再起 exe，所以就落在工程目录里）。证据：
 *   - ui-tools.exe 0xc94ae8 有字面量 "Application Data/ui-config"
 *   - 0xc94467 起是一整排键名：
 *       Project/Size  Project/Background  Project/LastOpen  Project/Dir
 *       Project/LangugeFile（原厂把 Language 拼错了，照抄）
 *       Project/Style  Project/TemplateJson  Project/CustomTemplateDir
 *       Project/ImageDir
 *   - 实机文件 ui_128_64_JL02/模式界面/project/Application Data/ui-config 里
 *     的内容与上面逐项对得上（Size=128*160、ImageDir=config …）
 * 重建版早先存在注册表 QSettings("QtProject","UITools")，和原厂完全不通 ——
 * 在原厂工具里配好的路径，重建版看不见，反过来也一样。现在改成同一个文件，
 * 两边可以互换着用。
 */
#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QDialog>
#include <QVariant>

class QTreeWidget;
class QTreeWidgetItem;

class GlobalSettings : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalSettings(QWidget *parent = nullptr);
    ~GlobalSettings() override;

    /// 供别处读同一份设置，键名与本对话框里一致
    static QVariant value(const QString &key, const QVariant &def = QVariant());
    static void     setValue(const QString &key, const QVariant &v);
    /// 设置文件的绝对路径（<启动目录>/Application Data/ui-config）
    static QString  filePath();

public slots:
    void onAccepted();

private:
    QTreeWidget     *m_tree = nullptr;
    QTreeWidgetItem *m_width = nullptr;    ///< 界面尺寸 -> 宽度:
    QTreeWidgetItem *m_height = nullptr;   ///< 界面尺寸 -> 高度:
    QTreeWidgetItem *m_lit = nullptr;      ///< 点阵屏预览 -> 像素点亮颜色:
    QTreeWidgetItem *m_dark = nullptr;     ///< 点阵屏预览 -> 像素熄灭颜色:
    QTreeWidgetItem *m_grid = nullptr;     ///< 点阵屏预览 -> 网格颜色:
};

#endif // GLOBALSETTINGS_H
