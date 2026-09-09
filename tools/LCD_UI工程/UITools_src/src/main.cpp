/*
 * main.cpp —— UITools（重建版）入口
 *
 * 命令行：
 *   UITools [--tools-root <UITools目录>] [工程.json]
 *   UITools --sty-dump <JL.sty>      只解析 .sty 并打印结构 + 往返校验，不开界面
 *   UITools --json-roundtrip <工程.json> [--out <新.json>]
 *                                    读入工程再写出，与原文件逐字节比较
 *                                    —— 这是「能否顶替原厂 ui-tools.exe」的验收项
 *   UITools --dialog-smoke [工程目录] [--out <PNG输出目录>]
 *                                    把每个对话框造一遍再销毁，检查有没有一开就崩
 *   UITools --tools-root <UITools目录> --click-test <N> <工程.json>
 *                                    打开工程后连续选中 N 个节点（走和点对象树
 *                                    完全相同的那条路），跑完就退出。
 *                                    用来复现/回归选中链路上的死循环
 *   UITools --tools-root <UITools目录> --ops-test <工程.json>
 *                                    无人值守跑一遍"操作逻辑与限制"：建控件/建布局
 *                                    的选中前提、复制粘贴的容器限制、Z 序、删除、
 *                                    宽高不能为零、列表加行，最后再存一次读一次
 *   UITools --json-rebuild <工程.json>
 *                                    把**每个节点都标脏**再写出，同样要求逐字节相同。
 *                                    roundtrip 只证明"没改的原样带出"，这一项才证明
 *                                    "改过的重建出来也一样" —— 属性面板一编辑就走这条路
 *
 * --tools-root 默认取可执行文件同级目录；控件库(control/control.json)、
 * 控件图标、多国语言表都从这里找，和原厂 ui-tools.exe 的相对布局一致。
 */
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QFont>
#include <QPixmap>
#include <QSettings>

#include "MainWindow.h"
#include "Preview.h"

#include "ActionList.h"
#include "BusyIndicator.h"
#include "ConfigProject.h"
#include "GlobalSettings.h"
#include "GridHelpLine.h"
#include "HVLineWidget.h"
#include "I18nLanguage.h"
#include "ImageFileDialog.h"
#include "ImageListView.h"
#include "MenuItemDialog.h"
#include "ProgressDlg.h"
#include "RuleWidget.h"
#include "ZoomProject.h"
#include "findDlg.h"
#include "StyFile.h"
#include "ProjectModel.h"
#include "ToolBinWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    /* 原厂是 Qt 5.9，Windows 上默认字体 "MS Shell Dlg 2"，中文回落到 SimSun(宋体)。
     * Qt 5.15 默认换成了系统 UI 字体（中文机器上是微软雅黑），字形一眼就不一样。
     * 显式钉死成宋体 9pt，才和原厂截图对得上。 */
    {
        QFont f(QStringLiteral("SimSun"), 9);
        f.setStyleStrategy(QFont::PreferDefault);
        QApplication::setFont(f);
    }
    QCoreApplication::setApplicationName(QStringLiteral("UITools"));
    QCoreApplication::setApplicationVersion(QStringLiteral("rebuild-0.1"));
    QCoreApplication::setOrganizationName(QStringLiteral("JLUI"));

    QCommandLineParser p;
    p.setApplicationDescription(
        QStringLiteral("杰理点阵屏 UI 布局工具"));
    p.addHelpOption();
    p.addVersionOption();
    QCommandLineOption optRoot(QStringList() << QStringLiteral("tools-root"),
                               QStringLiteral("UITools 目录（含 control/ backgrounds/）"),
                               QStringLiteral("dir"));
    QCommandLineOption optSty(QStringList() << QStringLiteral("sty-dump"),
                              QStringLiteral("只解析 .sty 并退出"),
                              QStringLiteral("file"));
    QCommandLineOption optRt(QStringList() << QStringLiteral("json-roundtrip"),
                             QStringLiteral("读入工程 json 再写出，与原文件逐字节比较"),
                             QStringLiteral("file"));
    QCommandLineOption optRb(QStringList() << QStringLiteral("json-rebuild"),
                             QStringLiteral("把每个节点都标脏再写出，与原文件逐字节比较"),
                             QStringLiteral("file"));
    QCommandLineOption optSmoke(QStringList() << QStringLiteral("dialog-smoke"),
                                QStringLiteral("把每个对话框造一遍再销毁"),
                                QStringLiteral("dir"));
    QCommandLineOption optOut(QStringList() << QStringLiteral("out"),
                              QStringLiteral("配合 --json-roundtrip，把重写结果落盘"),
                              QStringLiteral("file"));
    QCommandLineOption optClick(QStringList() << QStringLiteral("click-test"),
                                QStringLiteral("打开后连续选中 N 个节点再退出"),
                                QStringLiteral("N"));
    QCommandLineOption optShot(QStringList() << QStringLiteral("shot"),
                               QStringLiteral("启动后自截主窗口到 PNG 再退出（不依赖屏幕可见）"),
                               QStringLiteral("file"));
    p.addOption(optRoot);
    p.addOption(optSty);
    p.addOption(optRt);
    p.addOption(optRb);
    p.addOption(optSmoke);
    p.addOption(optOut);
    QCommandLineOption optOps(QStringList() << QStringLiteral("ops-test"),
                              QStringLiteral("无人值守跑一遍编辑操作与限制，打印逐条结果再退出"));
    p.addOption(optClick);
    p.addOption(optShot);
    QCommandLineOption optZoom(QStringList() << QStringLiteral("zoom"),
                               QStringLiteral("自截前把画布缩放到指定倍率"),
                               QStringLiteral("percent"));
    QCommandLineOption optSel(QStringList() << QStringLiteral("select"),
                              QStringLiteral("自截前选中第 N 个节点（看隔离效果）"),
                              QStringLiteral("N"));
    p.addOption(optOps);
    QCommandLineOption optPvDump(QStringList() << QStringLiteral("preview-dump"),
                                 QStringLiteral("把每个控件的内容预览存成 PNG 再退出"),
                                 QStringLiteral("dir"));
    QCommandLineOption optExport(QStringList() << QStringLiteral("export-test"),
                                 QStringLiteral("走工具栏「资源导出」那条路跑一遍再退出"
                                                "（不跑收尾脚本）"));
    p.addOption(optZoom);
    p.addOption(optSel);
    p.addOption(optPvDump);
    p.addOption(optExport);
    p.addPositionalArgument(QStringLiteral("project"),
                            QStringLiteral("要打开的工程 json"));
    p.process(app);

    QTextStream out(stdout);

    if (p.isSet(optRt)) {
        /* 这是「能不能顶替原厂 ui-tools.exe」的硬指标：
         * 原厂工程 json 就是 Qt 的 toJson(Indented) 输出，
         * 我们读进来再写出去必须逐字节相同，否则下游 QtToolBin 拿到的东西就变了。 */
        const QString f = p.value(optRt);
        QString rep;
        const bool ok = ProjectModel::verifyRoundTrip(f, &rep);
        out << rep << QLatin1Char('\n');
        if (p.isSet(optOut)) {
            ProjectModel m;
            QString err;
            if (m.load(f, &err)) {
                QFile g(p.value(optOut));
                if (g.open(QIODevice::WriteOnly)) {
                    g.write(m.toJsonBytes());
                    out << QStringLiteral("重写结果已写入 %1\n").arg(p.value(optOut));
                }
            }
        }
        return ok ? 0 : 3;
    }

    if (p.isSet(optRb)) {
        /* 属性面板一编辑，节点就会被标脏，回写时走的是「按模型重建」那条路。
         * 这里把所有节点都标脏，看重建出来还是不是原来那些字节 ——
         * 能过就说明模型没漏字段，编辑不会悄悄弄丢东西。 */
        const QString f = p.value(optRb);
        ProjectModel m;
        QString err;
        if (!m.load(f, &err)) {
            out << QStringLiteral("打不开: %1\n").arg(err);
            return 1;
        }
        int n = 0;
        for (UiNode *page : m.pages()) {
            page->forEach([&n](UiNode *x) {
                x->markDirty();
                ++n;
                return true;
            });
        }
        QFile g(f);
        g.open(QIODevice::ReadOnly);
        const QByteArray orig = g.readAll();
        const QByteArray again = m.toJsonBytes();
        const bool ok = (orig == again);
        out << QStringLiteral("全节点标脏重建（%1 个节点）：%2\n")
               .arg(n).arg(ok ? QStringLiteral("%1 字节逐字节相同").arg(again.size())
                              : QStringLiteral("不一致（原 %1 / 重建 %2 字节）")
                                .arg(orig.size()).arg(again.size()));
        if (!ok) {
            const int lim = qMin(orig.size(), again.size());
            int i = 0;
            while (i < lim && orig.at(i) == again.at(i)) {
                ++i;
            }
            out << QStringLiteral("   首个不同 @%1\n   原  : %2\n   重建: %3\n")
                   .arg(i)
                   .arg(QString::fromUtf8(orig.mid(qMax(0, i - 40), 90)))
                   .arg(QString::fromUtf8(again.mid(qMax(0, i - 40), 90)));
        }
        return ok ? 0 : 5;
    }

    if (p.isSet(optSmoke)) {
        /* 15 个类刚从"只有签名的骨架"填成实现，最容易犯的错是构造函数里
         * 少 new 了一个成员、后面直接解引用。这里全造一遍、渲染一次再销毁，
         * 崩了就立刻暴露，不用手点。 */
        const QString dir = p.value(optSmoke);
        /* 给了 --out <目录> 就把每个对话框存成 PNG。改过对话框版式之后
         * "没崩"不等于"没排坏"（按钮被裁掉半截照样不崩），得能看一眼。 */
        const QString shotDir = p.isSet(optOut) ? p.value(optOut) : QString();
        if (!shotDir.isEmpty()) {
            QDir().mkpath(shotDir);
        }
        int n = 0;
        auto touch = [&n, &shotDir](QWidget *w) {
            /* 400x300 太小，列表类对话框挤成一条缝，出的图看不出排版对不对。
             * 按对话框自己的默认大小来，放不下再兜到 400x300。 */
            const QSize want = w->sizeHint().expandedTo(QSize(400, 300));
            w->resize(want);
            const QPixmap pm = w->grab();     // 强制走一次 paintEvent
            if (!shotDir.isEmpty()) {
                pm.save(QDir(shotDir).filePath(
                    QStringLiteral("%1_%2.png")
                    .arg(n, 2, 10, QLatin1Char('0'))
                    .arg(QString::fromLatin1(w->metaObject()->className()))));
            }
            delete w;
            ++n;
        };
        {
            auto *d = new ImageFileDialog;
            d->setProjectDir(dir);
            /* 【挑真实存在的图】以前这里给的是 config/pic_lcd/x.bmp（不存在），
             * 出图只有空列表，改过缩略图之后"没崩"根本看不出画对没有。
             * 这里拿目录里前几张真图，让截图上能看到缩略图。 */
            QStringList real;
            const QString picDir = QDir(dir).filePath(QStringLiteral("config/pic_lcd"));
            for (const QString &f : QDir(picDir).entryList(
                     QStringList{ QStringLiteral("*.bmp"), QStringLiteral("*.BMP") },
                     QDir::Files, QDir::Name)) {
                real << QStringLiteral("config/pic_lcd/") + f;
                if (real.size() >= 6) {
                    break;
                }
            }
            d->setSelected(real.isEmpty()
                           ? QStringList{ QStringLiteral("config/pic_lcd/x.bmp") }
                           : real);
            touch(d);
        }
        {
            auto *d = new I18nLanguage;
            const QStringList xls = QDir(dir).entryList(
                QStringList{ QStringLiteral("*.xls") }, QDir::Files);
            if (!xls.isEmpty()) {
                d->loadExcel(QDir(dir).absoluteFilePath(xls.first()));
            }
            d->setSelected(QStringList{ QStringLiteral("m1") });
            touch(d);
        }
        touch(new ActionList);
        {
            auto *d = new ConfigProject;
            d->setProjectName(QStringLiteral("smoke"));
            d->setLanguageMask(0x13);
            touch(d);
        }
        touch(new GlobalSettings);
        {
            /* 工具栏上的「资源导出」弹的就是它。现在 QtToolBin 那几个源文件
             * 同时链进了 UITools，构造时会去读 project.ini 和 Resbuilder.xml，
             * 路径算错了会当场炸，所以拉进冒烟里。 */
            auto *d = new ToolBinWindow(dir.isEmpty() ? QDir::currentPath() : dir);
            touch(d);
        }
        {
            auto *d = new ZoomProject;
            d->setOldSize(QSize(128, 64));
            touch(d);
        }
        touch(new findDlg);
        touch(new MenuItemDialog);
        {
            auto *d = new ImageListView;
            d->setRootDir(dir.isEmpty() ? QDir::currentPath() : dir);
            touch(d);
        }
        {
            auto *d = new ProgressDlg;
            d->setRange(0, 10);
            d->setValue(5);
            d->setText(QStringLiteral("smoke"));
            touch(d);
        }
        {
            auto *d = new BusyIndicator;
            d->setText(QStringLiteral("smoke"));
            d->onRotate();
            touch(d);
        }
        {
            auto *w = new GridHelpLine;
            w->setStep(8);
            touch(w);
        }
        {
            auto *w = new HVLineWidget;
            w->setCross(QPoint(20, 30));
            touch(w);
        }
        {
            auto *w = new RuleWidget;
            w->setOrientation(Qt::Horizontal);
            w->setZoom(200);
            touch(w);
        }
        {
            auto *w = new RuleWidget;
            w->setOrientation(Qt::Vertical);
            touch(w);
        }
        out << QStringLiteral("对话框冒烟：%1 个全部构造+渲染+销毁通过\n").arg(n);
        return 0;
    }

    if (p.isSet(optSty)) {
        const QString f = p.value(optSty);
        StyFile sty;
        QString err;
        if (!sty.load(f, &err)) {
            out << QStringLiteral("解析失败: %1\n").arg(err);
            return 1;
        }
        QString rt;
        const bool ok = StyFile::verifyRoundTrip(f, &rt);
        out << rt << QLatin1Char('\n') << sty.describe() << QLatin1Char('\n');
        return ok ? 0 : 2;
    }

    /* 点阵屏预览配色存在工程目录的 ui-config 里，建窗口之前先读进来，
     * 不然第一帧画出来的还是默认的黑白。 */
    Preview::reloadMonoColors();

    MainWindow w;

    /* 工具目录：命令行给了就用；否则先看 exe 自己旁边（重建版工具目录里就有
     * control/ 和 config/），再按原厂的目录约定往上找三级。这样在工程目录里
     * 双击启动脚本、一个参数都不给也能跑起来。 */
    QString root = p.value(optRoot);
    if (root.isEmpty()) {
        QStringList cand;
        cand << QCoreApplication::applicationDirPath()
             << QDir::cleanPath(QDir::current().absoluteFilePath(
                    QStringLiteral("../../../UITools_rebuilt")))
             << QDir::cleanPath(QDir::current().absoluteFilePath(
                    QStringLiteral("../../../UITools")));
        for (const QString &c : cand) {
            if (QFile::exists(QDir(c).absoluteFilePath(
                    QStringLiteral("control/control.json")))) {
                root = c;
                break;
            }
        }
        if (root.isEmpty()) {
            root = QCoreApplication::applicationDirPath();
        }
    }
    w.setToolsRoot(root);
    w.show();

    /* 要打开哪个工程：命令行给了就用；否则读当前目录的
     * config/ini/project.ini 里的 projectfilename —— 和 QtToolBin 同一份配置，
     * 也是原厂那套启动脚本（cd project && start ui-tools.exe）能工作的原因。 */
    QString openPath = p.positionalArguments().value(0);
    if (openPath.isEmpty()) {
        const QString ini = QDir::current().absoluteFilePath(
            QStringLiteral("config/ini/project.ini"));
        if (QFile::exists(ini)) {
            QSettings st(ini, QSettings::IniFormat);
            const QString f = st.value(QStringLiteral("Project/projectfilename")).toString();
            if (!f.isEmpty() && QFile::exists(QDir::current().absoluteFilePath(f))) {
                openPath = QDir::current().absoluteFilePath(f);
            }
        }
    }
    if (!openPath.isEmpty()) {
        w.openProject(openPath);
    }

    /* 编辑操作与限制的无人值守回归。原厂那套限制（建控件要先选布局、
     * 粘贴只认布局、宽高不能为零……）本来全靠弹框拦人，弹框在没人点的
     * 环境里会把进程挂死，所以 runOpsTest() 先开 EditorOps 的无人值守开关。 */
    if (p.isSet(optOps)) {
        /* UITools 是 WIN32 子系统程序（双击不弹黑窗口），被重定向时
         * stdout 拿不到有效句柄，报告会整份丢掉。所以 --out 给了路径就
         * 落盘，自动化脚本读文件，不跟控制台较劲。 */
        const QString repFile = p.isSet(optOut) ? p.value(optOut) : QString();
        QTimer::singleShot(300, &app, [&w, &out, repFile]() {
            QString rep;
            const int fail = w.runOpsTest(&rep);
            out << rep << endl;
            out.flush();
            if (!repFile.isEmpty()) {
                QFile f(repFile);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(rep.toUtf8());
                    f.write(QByteArrayLiteral("\n"));
                }
            }
            QCoreApplication::exit(fail == 0 ? 0 : 7);
        });
        return app.exec();
    }

    /* 选中链路的无人值守回归：连续选中若干节点，走的是和"在对象树里点一下"
     * 完全相同的那条路（ScenesScreen::selectNode）。这条路上出过一次
     * 信号回环导致的栈溢出，所以留个自动化的复现手段。 */
    if (p.isSet(optClick)) {
        const int n = p.value(optClick).toInt();
        const QString shotAfter = p.isSet(optShot) ? p.value(optShot) : QString();
        QTimer::singleShot(300, &app, [&w, n, &out, shotAfter]() {
            int done = 0;
            for (int i = 0; i < n; ++i) {
                if (!w.selectNthNodeForTest(i)) {
                    break;
                }
                ++done;
            }
            out << QStringLiteral("选中链路：连点 %1 个节点没有崩\n").arg(done);
            // 同时给了 --shot 就顺手出图，用来肉眼确认选中之后属性面板跟上了
            if (!shotAfter.isEmpty()) {
                const bool ok = w.grab().save(shotAfter);
                out << QStringLiteral("选中后自截 %1: %2\n")
                       .arg(shotAfter, ok ? QStringLiteral("成功") : QStringLiteral("失败"));
            }
            out.flush();
            QCoreApplication::exit(done > 0 ? 0 : 6);
        });
        return app.exec();
    }

    /* 自截：QWidget::grab() 画的是 Qt 自己的后备缓冲，锁屏/无人值守也能出图，
     * 比走屏幕截图可靠。用来做界面回归。 */
    if (p.isSet(optPvDump)) {
        const QString dir = p.value(optPvDump);
        QTimer::singleShot(300, &app, [&w, dir, &out]() {
            const int n = w.dumpPreviewForTest(dir);
            out << QStringLiteral("内容预览导出 %1 张 -> %2\n").arg(n).arg(dir);
            out.flush();
            QCoreApplication::exit(n > 0 ? 0 : 8);
        });
        return app.exec();
    }

    /* 「资源导出」那颗按钮现在是点了直接跑、不弹界面的，出了问题界面上只剩
     * 一句状态文字。这条命令行走的是**同一个** exportResourceForTest()，
     * 把完整输出打出来，能无人值守回归。 */
    if (p.isSet(optExport)) {
        QTimer::singleShot(300, &app, [&w, &out]() {
            QString rep;
            const bool ok = w.exportResourceForTest(&rep);
            out << rep << QLatin1Char('\n');
            out << QStringLiteral("资源导出：%1\n")
                   .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"));
            out.flush();
            QCoreApplication::exit(ok ? 0 : 9);
        });
        return app.exec();
    }

    if (p.isSet(optShot)) {
        const QString shot = p.value(optShot);
        const int zoomTo = p.isSet(optZoom) ? p.value(optZoom).toInt() : 0;
        const int selN = p.isSet(optSel) ? p.value(optSel).toInt() : -1;
        if (zoomTo > 0) {
            w.setCanvasZoomForTest(zoomTo);
        }
        if (selN >= 0) {
            w.selectNthNodeForTest(selN);
        }
        QTimer::singleShot(1500, &app, [&w, shot, &out]() {
            const bool ok = w.grab().save(shot);
            out << QStringLiteral("自截 %1: %2\n")
                   .arg(shot, ok ? QStringLiteral("成功") : QStringLiteral("失败"));
            QCoreApplication::exit(ok ? 0 : 4);
        });
    }
    return app.exec();
}
