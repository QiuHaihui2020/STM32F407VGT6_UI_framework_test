/* ToolBinWindow.h —— QtToolBin 的界面。
 *
 * QtToolBin（标题 "UIToolBin工具(V1.10.1)"）是**弹窗**用的：
 * 双击 step2 弹出来，在界面上点「生成资源文件(F5)」才开始干活。行是这么排的：
 *
 *     运行:      [ 生成资源文件(F5) ]
 *     进度条:    [                    ] 0%
 *     JSON文件:  [ SmallColorTFT.json ]        工程ID: [0]
 *     资源设置:  [ 功能设置 ]
 *     资源文件:  ☐ 不重新生成资源文件
 *     调用脚本:  [ copy_file.bat ]
 *     芯片平台:  [ 全平台 ▼ ]
 *     旋转:      [ 0 ▼ ]
 *     版本配置:  [ ]
 *
 * 界面上的值就是 project\config\ini\project.ini 里那几项，点生成时写回去
 * —— 命令行模式读的也是同一份配置。
 */
#ifndef TOOLBINWINDOW_H
#define TOOLBINWINDOW_H

#include <QWidget>

#include "ResbuilderOptions.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QProgressBar;
class QPushButton;
class QPlainTextEdit;
class QLabel;

class ToolBinWindow : public QWidget
{
    Q_OBJECT

public:
    /// projectDir 是工程目录（含 config\ini\project.ini），一般就是启动时的当前目录
    explicit ToolBinWindow(const QString &projectDir, QWidget *parent = nullptr);
    ~ToolBinWindow() override;

    /** 自测用：不跑收尾脚本（免得往固件工程里拷东西）。 */
    void setRunScriptEnabled(bool on) { m_allowScript = on; }
    /** 自测用：把弹框改成只写日志。模态框在无人值守下会把进程挂死。 */
    void setSilent(bool on) { m_silent = on; }
    /** 自测用：等同于点一下「生成资源文件」，返回是否全程成功。 */
    bool generateForTest();

    /**
     * 不显示界面，直接跑一遍完整生成链 —— 编辑器工具栏上的「资源导出」走这条。
     *
     * 和点「生成资源文件」是同一个 onGenerate()，只是把过程中的弹框压成日志，
     * 由调用方统一报结果；产物、配置来源、收尾脚本一律不变。
     *
     * @param[out] log 全过程输出，失败时拿去给用户看
     * @return 全程成功
     */
    bool runHeadless(QString *log);

private slots:
    void onGenerate();          ///< 「生成资源文件(F5)」
    void onPickJson();
    void onFeatureSettings();   ///< 「功能设置」

private:
    void loadIni();
    void saveIni();
    void log(const QString &s);
    void notify(bool fatal, const QString &title, const QString &text);
    void setBusy(bool on);
    bool runResBuilder();
    bool runScript();

    QString         m_projectDir;
    QComboBox      *m_json = nullptr;
    QSpinBox       *m_pjId = nullptr;
    QCheckBox      *m_skipRes = nullptr;
    QLineEdit      *m_script = nullptr;
    QComboBox      *m_platform = nullptr;
    QComboBox      *m_rotate = nullptr;
    QLineEdit      *m_version = nullptr;
    QProgressBar   *m_progress = nullptr;
    QPushButton    *m_run = nullptr;
    QPlainTextEdit *m_log = nullptr;

    // 「功能设置」里的东西，不在主界面上
    QString  m_excelPath;
    quint32  m_language = 0x13;
    QString  m_panelType = QStringLiteral("LCDPANEL");
    bool     m_allowScript = true;
    bool     m_silent = false;
    bool     m_lastOk = false;
    /** 当前是不是在忙 —— 让 setBusy() 幂等，别把覆盖光标那个栈压失衡。 */
    bool     m_busy = false;
    /** 「功能设置」那一页；存在 <工具目录>/config/ini/resbuilder.ini。 */
    toolbin::ResbuilderOptions m_res;
};

#endif // TOOLBINWINDOW_H
