/* GlobalSettings.h —— [全局设置]，六项：界面尺寸 + 五条路径。
 *
 * 【存哪儿】**工程目录**下的 "Application Data/ui-config"（QSettings::IniFormat），
 * 键名如下：
 *     Project/Size  Project/Background  Project/LastOpen  Project/Dir
 *     Project/LangugeFile（这个键名就是这么拼的，别"顺手改对"）
 *     Project/Style  Project/TemplateJson  Project/CustomTemplateDir
 *     Project/ImageDir
 * 早先存在注册表 QSettings("QtProject","UITools") 里，那样这份配置就和工程
 * 脱钩了。现在按工程目录定位（见 setProjectDir 的说明），从哪儿起 exe 都一样。
 */
#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include <QDialog>
#include <QVector>
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
    /// 设置文件的绝对路径（<工程目录>/Application Data/ui-config）
    static QString  filePath();

    /**
     * 把设置文件挪到某个工程目录下。打开/新建/另存工程之后调。
     *
     * 【为什么必须跟着工程走】这个文件里存的全是**按工程**的东西：页面尺寸、
     * 图片目录、多国语言表、上次打开的工程、点阵屏预览配色，还有每个文字
     * 控件的「预览文字」。跟着工程走才有两个好处：
     *   · 复制一份 UI 工程，这些设置一起被复制过去，预览文字不用重配；
     *   · 两个工程互不干扰 —— 页面尺寸 128*64 和 240*240 不会互相冲掉。
     *
     * 原来锚的是**进程当前目录**：启动脚本里 `cd project` 之后再起 exe，
     * cwd 恰好等于工程目录，才碰巧落对地方；而从别处起 exe（脚本、自测）
     * 就会在那儿凭空拉出一个 Application Data 目录，里面还是一份和当前
     * 工程无关的空配置。
     *
     * @param dir 工程 json 所在目录；空串表示回到"按当前目录"的老行为
     */
    static void     setProjectDir(const QString &dir);

public slots:
    void onAccepted();

private:
    QTreeWidget     *m_tree = nullptr;
    QTreeWidgetItem *m_width = nullptr;    ///< 界面尺寸 -> 宽度:
    QTreeWidgetItem *m_height = nullptr;   ///< 界面尺寸 -> 高度:
    QTreeWidgetItem *m_lit = nullptr;      ///< 点阵屏预览 -> 像素点亮颜色:
    QTreeWidgetItem *m_dark = nullptr;     ///< 点阵屏预览 -> 像素熄灭颜色:
    QTreeWidgetItem *m_grid = nullptr;     ///< 点阵屏预览 -> 网格颜色:
    QTreeWidgetItem *m_uiPreset = nullptr;   ///< 界面配色 -> 方案:
    QTreeWidgetItem *m_uiBase = nullptr;     ///< 界面配色 -> 底色:（只在自定义时可改）
    QTreeWidgetItem *m_uiAccent = nullptr;   ///< 界面配色 -> 强调色:
};

#endif // GLOBALSETTINGS_H
