#include "ToolBinWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QTextStream>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

#include "BuildDate.h"
#include "FeatureDialog.h"
#include "StyBuilder.h"

namespace {

QString iniPathOf(const QString &projectDir)
{
    return QDir(projectDir).absoluteFilePath(QStringLiteral("config/ini/project.ini"));
}

/* 「功能设置」跟着**工程**走：原厂就是存在工程目录的 Resbuilder.xml 里
 * （见 ResbuilderOptions.h 抬头那段证据）。两个工程各有各的设置。 */
QString resXmlPathOf(const QString &projectDir)
{
    return QDir(projectDir).absoluteFilePath(QStringLiteral("Resbuilder.xml"));
}

/// 工具目录 = 本 exe 所在目录（重建版三个 exe 放一起）
QString toolDir()
{
    return QCoreApplication::applicationDirPath();
}

} // namespace

ToolBinWindow::ToolBinWindow(const QString &projectDir, QWidget *parent)
    : QWidget(parent), m_projectDir(projectDir)
{
    setWindowTitle(tr("UIToolBin工具(Build:%1)").arg(common::buildDate()));

    // ---- 运行 ----
    m_run = new QPushButton(tr("生成资源文件(F5)"), this);
    m_run->setMinimumHeight(28);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    // ---- JSON文件 + 工程ID ----
    m_json = new QComboBox(this);
    m_json->setEditable(true);
    m_json->setMinimumWidth(240);
    auto *pick = new QPushButton(tr("…"), this);
    pick->setFixedWidth(28);
    m_pjId = new QSpinBox(this);
    m_pjId->setRange(0, 7);          // id 的 bit29..31，只有 3 位

    auto *jsonRow = new QHBoxLayout;
    jsonRow->addWidget(m_json, 1);
    jsonRow->addWidget(pick);
    jsonRow->addSpacing(12);
    jsonRow->addWidget(new QLabel(tr("工程ID:"), this));
    jsonRow->addWidget(m_pjId);

    // ---- 资源设置 / 资源文件 / 调用脚本 ----
    auto *feature = new QPushButton(tr("功能设置"), this);
    m_skipRes = new QCheckBox(tr("不重新生成资源文件"), this);
    m_script = new QLineEdit(this);

    // ---- 芯片平台 / 旋转 / 版本配置 ----
    m_platform = new QComboBox(this);
    m_platform->addItem(tr("全平台"));
    m_platform->setEnabled(false);
    m_platform->setToolTip(
        tr("这一项不影响产出，只占位、不生效。"));

    m_rotate = new QComboBox(this);
    m_rotate->addItems(QStringList{ QStringLiteral("0"), QStringLiteral("90"),
                                    QStringLiteral("180"), QStringLiteral("270") });
    m_version = new QLineEdit(this);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(tr("运行:"), m_run);
    form->addRow(tr("进度条:"), m_progress);
    form->addRow(tr("JSON文件:"), jsonRow);
    form->addRow(tr("资源设置:"), feature);
    form->addRow(tr("资源文件:"), m_skipRes);
    form->addRow(tr("调用脚本:"), m_script);
    form->addRow(tr("芯片平台:"), m_platform);
    form->addRow(tr("旋转:"), m_rotate);
    form->addRow(tr("版本配置:"), m_version);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(180);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(new QLabel(tr("输出:"), this));
    root->addWidget(m_log, 1);
    resize(620, 520);

    connect(m_run, &QPushButton::clicked, this, &ToolBinWindow::onGenerate);
    connect(pick, &QPushButton::clicked, this, &ToolBinWindow::onPickJson);
    connect(feature, &QPushButton::clicked, this, &ToolBinWindow::onFeatureSettings);
    auto *f5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(f5, &QShortcut::activated, this, &ToolBinWindow::onGenerate);

    loadIni();
    log(tr("工程目录: %1").arg(QDir::toNativeSeparators(m_projectDir)));
    log(tr("工具目录: %1").arg(QDir::toNativeSeparators(toolDir())));
}

ToolBinWindow::~ToolBinWindow() = default;

void ToolBinWindow::loadIni()
{
    // 工程目录里所有 json 都列出来（autosave 是编辑器的自动存档，不算工程）
    const QStringList js = QDir(m_projectDir).entryList(
        QStringList{ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString &j : js) {
        if (j.compare(QLatin1String("autosave.json"), Qt::CaseInsensitive) != 0) {
            m_json->addItem(j);
        }
    }

    QSettings ini(iniPathOf(m_projectDir), QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("Project"));
    const QString f = ini.value(QStringLiteral("projectfilename")).toString();
    if (!f.isEmpty()) {
        const int i = m_json->findText(f);
        if (i >= 0) {
            m_json->setCurrentIndex(i);
        } else {
            m_json->setEditText(f);
        }
    }
    m_pjId->setValue(ini.value(QStringLiteral("projectid"), 0).toInt());
    m_skipRes->setChecked(ini.value(QStringLiteral("projectresbuilder"), false).toBool());
    m_script->setText(ini.value(QStringLiteral("projectbatscript"),
                                QStringLiteral("copy_file.bat")).toString());
    const int rot = ini.value(QStringLiteral("projectrotate"), 0).toInt();
    m_rotate->setCurrentIndex(qBound(0, rot, 3));
    ini.endGroup();
    ini.beginGroup(QStringLiteral("Version"));
    m_version->setText(ini.value(QStringLiteral("versionid")).toString());
    ini.endGroup();

    // 「功能设置」那一页 —— 从工程目录的 Resbuilder.xml 读，和原厂一致
    m_res.loadFromProject(resXmlPathOf(m_projectDir));

    /* 多国语言表：设置里配了就用它。原厂存的是**相对工程目录**的路径
     * （../../../UITools/多国语言_128_64.xls），这里解成绝对路径再用。 */
    if (!m_res.excelPath.isEmpty()) {
        m_excelPath = QFileInfo(m_res.excelPath).isAbsolute()
                      ? m_res.excelPath
                      : QDir(m_projectDir).absoluteFilePath(m_res.excelPath);
    } else {
        const QStringList xls = QDir(toolDir()).entryList(
            QStringList{ QStringLiteral("*.xls") }, QDir::Files);
        if (!xls.isEmpty()) {
            m_excelPath = QDir(toolDir()).absoluteFilePath(xls.first());
        }
    }
    m_language = m_res.languageMask;
    m_panelType = m_res.panelType;
}

void ToolBinWindow::saveIni()
{
    const QString p = iniPathOf(m_projectDir);
    QDir().mkpath(QFileInfo(p).absolutePath());
    QSettings ini(p, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("Project"));
    ini.setValue(QStringLiteral("projectfilename"), m_json->currentText().trimmed());
    ini.setValue(QStringLiteral("projectid"), m_pjId->value());
    ini.setValue(QStringLiteral("projectresbuilder"), m_skipRes->isChecked());
    ini.setValue(QStringLiteral("projectbatscript"), m_script->text().trimmed());
    ini.setValue(QStringLiteral("projectrotate"), m_rotate->currentIndex());
    ini.endGroup();
    ini.beginGroup(QStringLiteral("Version"));
    ini.setValue(QStringLiteral("versionid"), m_version->text().trimmed());
    ini.endGroup();
    ini.sync();
}

/* 出错提示。自测（--selftest-generate）时不能弹模态框 —— 没人点，进程就挂死了，
 * 这个坑刚踩过一次。静默模式下只写进日志。 */
void ToolBinWindow::notify(bool fatal, const QString &title, const QString &text)
{
    log((fatal ? QStringLiteral("× ") : QStringLiteral("! ")) + text);
    if (m_silent) {
        return;
    }
    if (fatal) {
        QMessageBox::critical(this, title, text);
    } else {
        QMessageBox::warning(this, title, text);
    }
}

void ToolBinWindow::log(const QString &s)
{
    m_log->appendPlainText(s);
    m_log->ensureCursorVisible();
    /* 【自测模式顺带吐到 stdout】日志本来只进界面上那个输出框，自动化跑
     * --selftest-generate 失败时只能看到一个退出码，查不出卡在哪一步。 */
    if (m_silent) {
        QTextStream(stdout) << s << QLatin1Char('\n');
        QTextStream(stdout).flush();
    }
    QApplication::processEvents();
}

void ToolBinWindow::setBusy(bool on)
{
    m_run->setEnabled(!on);
    /* 【覆盖光标是个栈，不是开关】QApplication::setOverrideCursor 每调一次都
     * **往栈里压一层**，restoreOverrideCursor 才弹一层。
     *
     * 原来这里两个方向都压、只在 off 的时候弹一次：
     *     push(Wait) -> push(Arrow) -> pop()   栈里还剩一个 Wait
     * 于是"生成完成"打印出来了，鼠标却一直转圈，而且每生成一次多留一层，
     * 想点掉都点不掉。
     *
     * 现在 on 只压、off 只弹，并且用 m_busy 记状态做幂等 —— 重复调同一个
     * 方向不会把栈搞失衡（调用点有好几条提前 return 的路径，将来加一条忘了
     * 配对也不至于又卡住光标）。 */
    if (on == m_busy) {
        return;
    }
    m_busy = on;
    if (on) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void ToolBinWindow::onPickJson()
{
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择工程 json"), m_projectDir, tr("ui-tools 工程 (*.json)"));
    if (f.isEmpty()) {
        return;
    }
    // 只认工程目录里的，别的路径下游的相对路径全对不上
    if (QFileInfo(f).absolutePath() != QDir(m_projectDir).absolutePath()) {
        notify(false, tr("JSON文件"), tr("只能选工程目录里的 json：\n%1")
                             .arg(QDir::toNativeSeparators(m_projectDir)));
        return;
    }
    m_json->setEditText(QFileInfo(f).fileName());
}

void ToolBinWindow::onFeatureSettings()
{
    /* 原厂这一页叫「配置界面」，版式见 temp/功能设置.jpg。
     * 以前这儿只有三项占位（多国语言表 / 语言掩码 / 面板类型），
     * 字体、透明色、资源文件名、压缩方式这些只能去改代码。 */
    toolbin::FeatureDialog d(m_res, this);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    m_res = d.options();

    // 主界面上跟着走的那两处
    if (!m_res.excelPath.isEmpty()) {
        m_excelPath = QFileInfo(m_res.excelPath).isAbsolute()
                      ? m_res.excelPath
                      : QDir(m_projectDir).absoluteFilePath(m_res.excelPath);
    }
    m_language = m_res.languageMask;
    m_panelType = m_res.panelType;
    /* 【不另存文件】这一页的落盘就是下一次生成时重写的 Resbuilder.xml ——
     * 原厂也是这个路子。所以改完要点「生成资源文件」才算存下来。 */
    log(tr("配置已更新，点「生成资源文件」后写入 %1")
        .arg(QDir::toNativeSeparators(resXmlPathOf(m_projectDir))));
}

bool ToolBinWindow::runResBuilder()
{
    const QString exe = QDir(toolDir()).absoluteFilePath(QStringLiteral("ResBuilder.exe"));
    if (!QFile::exists(exe)) {
        log(tr("× 找不到 %1").arg(QDir::toNativeSeparators(exe)));
        return false;
    }
    QProcess p;
    p.setWorkingDirectory(m_projectDir);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(exe, QStringList()
            << QDir(m_projectDir).absoluteFilePath(QStringLiteral("Resbuilder.xml")));
    if (!p.waitForStarted(10000)) {
        log(tr("× ResBuilder 起不来"));
        return false;
    }
    while (!p.waitForFinished(100)) {
        QApplication::processEvents();
    }
    const QString outText = QString::fromLocal8Bit(p.readAll()).trimmed();
    if (!outText.isEmpty()) {
        log(outText);
    }
    return p.exitCode() == 0;
}

bool ToolBinWindow::runScript()
{
    if (!m_allowScript) {
        log(tr("（自测模式，跳过收尾脚本，不往固件工程里拷东西）"));
        return true;
    }
    const QString bat = m_script->text().trimmed();
    if (bat.isEmpty()) {
        return true;
    }
    const QString full = QDir(m_projectDir).absoluteFilePath(bat);
    if (!QFile::exists(full)) {
        log(tr("× 找不到脚本 %1").arg(bat));
        return false;
    }
    QProcess p;
    p.setWorkingDirectory(m_projectDir);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("cmd"), QStringList() << QStringLiteral("/c") << full);
    if (!p.waitForStarted(10000)) {
        log(tr("× 脚本起不来"));
        return false;
    }
    while (!p.waitForFinished(100)) {
        QApplication::processEvents();
    }
    const QString outText = QString::fromLocal8Bit(p.readAll()).trimmed();
    if (!outText.isEmpty()) {
        log(outText);
    }
    return p.exitCode() == 0;
}

void ToolBinWindow::onGenerate()
{
    const QString jsonName = m_json->currentText().trimmed();
    if (jsonName.isEmpty()) {
        notify(false, tr("生成资源文件"), tr("还没选工程 json。\n新工程要先用绘图工具建页面并保存。"));
        return;
    }
    const QString jsonPath = QDir(m_projectDir).absoluteFilePath(jsonName);
    if (!QFile::exists(jsonPath)) {
        notify(false, tr("生成资源文件"), tr("找不到 %1").arg(QDir::toNativeSeparators(jsonPath)));
        return;
    }

    setBusy(true);
    m_log->clear();
    m_progress->setValue(0);
    saveIni();                      // 界面上的值写回 project.ini，和原厂一样
    log(tr("配置已写回 config\\ini\\project.ini"));

    // ---- 1. 工程 json -> project.bin / ename.h / Resbuilder.xml / debug.txt ----
    sty::Options opt;
    /* 先灌「功能设置」那一页（字体表、透明色、压缩方式…），
     * 再让主界面上那几个控件覆盖它们各自管的项。 */
    m_res.applyTo(opt);
    opt.pjId = m_pjId->value();
    opt.rotate = m_rotate->currentIndex();
    opt.projectDir = m_projectDir;
    opt.outDir = m_projectDir;      // 界面这条路产物就落在工程目录
    opt.excelPath = m_excelPath;
    opt.language = m_language;
    opt.panelType = m_panelType;
    opt.optionIni = QDir(toolDir()).absoluteFilePath(
        QStringLiteral("config/ini/option.ini"));

    sty::Builder b;
    QString err;
    m_progress->setValue(5);
    if (!b.loadProject(jsonPath, &err) || !b.loadOptionIni(opt.optionIni, &err)) {
        log(tr("× %1").arg(err));
        m_lastOk = false;
        m_progress->setValue(0);
        setBusy(false);
        notify(true, tr("生成资源文件"), err);
        return;
    }
    log(tr("工程: %1").arg(jsonName));
    log(tr("类型表: %1").arg(QDir::toNativeSeparators(opt.optionIni)));
    m_progress->setValue(20);

    const sty::Output o = b.build(opt);
    for (const QString &w : o.warnings) {
        log(tr("! %1").arg(w));
    }
    if (!o.ok) {
        log(tr("× %1").arg(o.error));
        m_lastOk = false;
        m_progress->setValue(0);
        setBusy(false);
        notify(true, tr("生成资源文件"), o.error);
        return;
    }

    struct F { const char *name; QByteArray data; };
    const QVector<F> files = {
        { "project.bin",    o.sty },
        { "ename.h",        o.enameH },
        { "Resbuilder.xml", o.resbuilderXml },
        { "debug.txt",      o.debugTxt },
    };
    for (const F &f : files) {
        const QString p = QDir(m_projectDir).absoluteFilePath(QLatin1String(f.name));
        QFile g(p);
        if (!g.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log(tr("× 写不了 %1").arg(f.name));
            m_lastOk = false;
        m_progress->setValue(0);
            setBusy(false);
            return;
        }
        g.write(f.data);
        g.close();
        log(tr("  写出 %1  %2 字节").arg(QLatin1String(f.name)).arg(f.data.size()));
    }
    m_progress->setValue(50);

    // ---- 2. 资源 ----
    if (m_skipRes->isChecked()) {
        log(tr("勾了「不重新生成资源文件」，跳过 ResBuilder"));
    } else {
        log(tr("调 ResBuilder…"));
        if (!runResBuilder()) {
            m_lastOk = false;
        m_progress->setValue(0);
            setBusy(false);
            notify(true, tr("生成资源文件"), tr("ResBuilder 失败，看输出"));
            return;
        }
    }
    m_progress->setValue(80);

    // ---- 3. 收尾脚本 ----
    log(tr("调 %1…").arg(m_script->text().trimmed()));
    const bool scriptOk = runScript();
    m_progress->setValue(100);
    setBusy(false);

    m_lastOk = scriptOk;
    if (scriptOk) {
        log(tr("完成。"));
    } else {
        log(tr("生成完了，但收尾脚本返回了非 0 —— 资源可能没拷进固件工程。"));
        notify(false, tr("生成资源文件"), tr("资源已生成，但 %1 执行失败，看输出。")
                             .arg(m_script->text().trimmed()));
    }
}

bool ToolBinWindow::generateForTest()
{
    m_lastOk = false;
    onGenerate();
    return m_lastOk;
}

bool ToolBinWindow::runHeadless(QString *log)
{
    /* onGenerate() 有几条早退分支（没选 json、文件不在）不置 m_lastOk，
     * 所以进来先清成 false，别把上一次的结果当成这一次的。 */
    const bool prevSilent = m_silent;
    m_silent = true;              // 过程中的弹框压成日志，最后由调用方统一报
    m_lastOk = false;
    onGenerate();
    m_silent = prevSilent;
    if (log) {
        *log = m_log->toPlainText();
    }
    return m_lastOk;
}
