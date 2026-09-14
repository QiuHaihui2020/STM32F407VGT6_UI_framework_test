/*
 * main.cpp —— UITools 入口
 *
 * 命令行：
 *   UITools [--tools-root <UITools目录>] [工程.json]
 *   UITools --sty-dump <JL.sty>      只解析 .sty 并打印结构 + 往返校验，不开界面
 *   UITools --json-roundtrip <工程.json> [--out <新.json>]
 *                                    读入工程再写出，与原文件逐字节比较
 *                                    —— 工程文件读写兼容性的验收项
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
 * --tools-root 默认取可执行文件同级目录；控件库(assets/widgets.json)、
 * 控件图标、多国语言表都从这里找（各自的确切位置见 AssetPaths.h）。
 */
#include <QApplication>
#include <QIcon>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QFont>
#include <QPixmap>
#include <QSettings>
#include <QVector>

#include "MainWindow.h"
#include "AppIcon.h"
#include "AssetPaths.h"
#include "Products.h"
#include "Preview.h"

#include "EventActionDialog.h"
#include "WaitOverlay.h"
#include "ProjectSettingsDialog.h"
#include "GlobalSettings.h"
#include "AlignGuide.h"
#include "GuideLine.h"
#include "StringPicker.h"
#include "ImagePicker.h"
#include "ImageStrip.h"
#include "MenuItemEditor.h"
#include "ProgressBox.h"
#include "Ruler.h"
#include "ScaleDialog.h"
#include "FindDialog.h"
#include "StyFile.h"
#include "ProjectModel.h"
#include "ToolBinWindow.h"

/* 两个子命令的入口，实现在各自的 main.cpp 里（原来是两个独立 exe 的 main）。 */
int toolbinMain(int argc, char *argv[]);
int resbuilderMain(int argc, char *argv[]);

namespace {

/**
 * 子命令分发。
 *
 * 【为什么合成一个 exe】三个 exe 里有两个和别处的工具重名，而且各自带一份
 * Qt；合成一个之后工具目录少两项，静态链接时 Qt 也只打包一份。
 *
 * 【为什么必须在构造 QApplication 之前分发】被分发到的那两个入口自己要建
 * Q*Application（--gen 还要按参数决定建带界面的还是不带的），这儿先建一个
 * 就冲突了。
 *
 * 子命令从 argv 里摘掉再往下传，那两段的参数解析一个字都不用改。
 *
 * @return 命中就返回退出码，没命中返回 -1（继续走编辑器那条路）。
 */
int dispatchSubcommand(int argc, char *argv[])
{
    if (argc < 2) {
        return -1;
    }
    /* --clean [工程目录]：把生成物删掉，默认当前目录。
     * 这一段以前是 clear.bat / clear.sh 两个脚本，收进来之后工具目录少两项，
     * 而且"哪些算生成物"只有 Products.h 一处定义，不会两边写岔。 */
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) != "--clean") {
            continue;
        }
        const QString dir = (i + 1 < argc && argv[i + 1][0] != '-')
                            ? QString::fromLocal8Bit(argv[i + 1])
                            : QDir::currentPath();
        QStringList gone;
        const int n = products::clean(dir, &gone);
        QTextStream o(stdout);
        o << QStringLiteral("清掉 %1 个生成物（%2）\n")
             .arg(n).arg(QDir::toNativeSeparators(dir));
        for (const QString &f : gone) {
            o << QStringLiteral("  %1\n").arg(f);
        }
        o.flush();
        return 0;
    }

    int (*entry)(int, char **) = nullptr;
    int at = -1;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "--gen") {
            entry = toolbinMain;
        } else if (a == "--pack") {
            entry = resbuilderMain;
        } else {
            continue;
        }
        at = i;
        break;
    }
    if (!entry) {
        return -1;
    }
    QVector<char *> rest;
    rest.reserve(argc - 1);
    for (int i = 0; i < argc; ++i) {
        if (i != at) {
            rest.append(argv[i]);
        }
    }
    return entry(rest.size(), rest.data());
}

} // namespace

int main(int argc, char *argv[])
{
    const int sub = dispatchSubcommand(argc, argv);
    if (sub >= 0) {
        return sub;
    }

    QApplication app(argc, argv);
    /* 应用图标：标题栏左上角、任务栏、Alt-Tab 都取这一个。
     * exe 自己在资源管理器里的图标是另一回事，由 resources/app.rc 提供。 */
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    /* Qt 5.15 默认用系统 UI 字体（中文机器上是微软雅黑），点阵屏工程里
     * 字形偏大偏圆，排版看不准。显式钉死成宋体 9pt。 */
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
                               QStringLiteral("工具目录（含 assets/）"),
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
    QCommandLineOption optNoChrome(QStringList() << QStringLiteral("no-chrome"),
                                   QStringLiteral("自截前隐藏辅助线"
                                                  "（控件描边/选中框/控件名/像素网格）"));
    p.addOption(optOps);
    QCommandLineOption optSample(QStringList() << QStringLiteral("make-sample"),
                                 QStringLiteral("造一份每种控件各一个+各种组合的样例工程"
                                                "（兼容性比对用）"),
                                 QStringLiteral("out.json"));
    QCommandLineOption optSamplePic(QStringList() << QStringLiteral("sample-pics"),
                                    QStringLiteral("样例工程里图片控件到哪儿找图"
                                                   "（相对工程目录，默认 config/pic_lcd）"),
                                    QStringLiteral("dir"),
                                    QStringLiteral("config/pic_lcd"));
    p.addOption(optSample);
    p.addOption(optSamplePic);
    QCommandLineOption optPvDump(QStringList() << QStringLiteral("preview-dump"),
                                 QStringLiteral("把每个控件的内容预览存成 PNG 再退出"),
                                 QStringLiteral("dir"));
    QCommandLineOption optExport(QStringList() << QStringLiteral("export-test"),
                                 QStringLiteral("走工具栏「资源导出」那条路跑一遍再退出"
                                                "（不跑收尾脚本）"));
    p.addOption(optZoom);
    p.addOption(optSel);
    p.addOption(optPvDump);
    p.addOption(optNoChrome);
    p.addOption(optExport);
    p.addPositionalArgument(QStringLiteral("project"),
                            QStringLiteral("要打开的工程文件（.uiproj / .json）"));
    p.process(app);

    QTextStream out(stdout);

    if (p.isSet(optRt)) {
        /* 工程文件读写兼容性的硬指标：
         * 工程 json 就是 Qt 的 toJson(Indented) 格式，
         * 读进来再写出去必须逐字节相同，否则下游 QtToolBin 拿到的东西就变了。 */
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
            auto *d = new ImagePicker;
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
            auto *d = new StringPicker;
            const QStringList xls = QDir(dir).entryList(
                QStringList{ QStringLiteral("*.xls") }, QDir::Files);
            if (!xls.isEmpty()) {
                d->loadExcel(QDir(dir).absoluteFilePath(xls.first()));
            }
            d->setSelected(QStringList{ QStringLiteral("m1") });
            touch(d);
        }
        touch(new EventActionDialog);
        {
            auto *d = new ProjectSettingsDialog;
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
            auto *d = new ScaleDialog;
            d->setOldSize(QSize(128, 64));
            touch(d);
        }
        touch(new FindDialog);
        touch(new MenuItemEditor);
        {
            auto *d = new ImageStrip;
            d->setProjectDir(dir.isEmpty() ? QDir::currentPath() : dir);
            /* 出图时能看到缩略图和当前这张被选中的效果 */
            d->setSelected(QStringLiteral("config/pic_lcd/v_block.bmp"));
            touch(d);
        }
        {
            auto *d = new ProgressBox;
            d->setRange(0, 10);
            d->setValue(5);
            d->setText(QStringLiteral("smoke"));
            touch(d);
        }
        {
            auto *d = new WaitOverlay;
            d->setText(QStringLiteral("smoke"));
            d->onRotate();
            touch(d);
        }
        {
            auto *w = new AlignGuide;
            w->setStep(8);
            touch(w);
        }
        {
            auto *w = new GuideLine;
            w->setCross(QPoint(20, 30));
            touch(w);
        }
        {
            auto *w = new Ruler;
            w->setOrientation(Qt::Horizontal);
            w->setZoom(200);
            touch(w);
        }
        {
            auto *w = new Ruler;
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

    /* 工具目录：命令行给了就用；否则先看 exe 自己旁边（工具目录里就有
     * assets/），再按目录约定找 —— 工具在 UI 工程目录下的 tool\ 里，
     * 从 <UI工程>/project 数上去就是 ../tool。这样在工程目录里启动、
     * 一个参数都不给也能跑起来。
     * 后面几个候选是老布局（工具目录和工程并列）的兜底。 */
    QString root = p.value(optRoot);
    if (root.isEmpty()) {
        QStringList cand;
        cand << QCoreApplication::applicationDirPath();
        for (const char *up : { "../tool", "../../tool",
                                "../../../UIToolkit", "../../../UITools" }) {
            cand << QDir::cleanPath(QDir::current().absoluteFilePath(
                        QString::fromLatin1(up)));
        }
        for (const QString &c : cand) {
            if (assets::isToolRoot(c)) {
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

    /* 要打开哪个工程：命令行给了就用（双击关联的 .uiproj 走的就是这条）；
     * 否则按目录约定自己找。
     *
     * 【为什么不止看当前目录】当前目录是启动脚本 cd 进去的那个工程目录 ——
     * 而直接双击 <UI工程>\tool\UITools.exe 时，当前目录是 tool\，那儿没有
     * project.ini，于是什么都不开、进来是个空窗口。所以再往 exe 的邻居
     * ..\project 看一眼：一个 UI 工程目录里就一份工程，不会有歧义。 */
    auto projectInDir = [](const QString &dir) -> QString {
        const QString ini = QDir(dir).absoluteFilePath(
            QStringLiteral("config/ini/project.ini"));
        if (!QFile::exists(ini)) {
            return QString();
        }
        QSettings st(ini, QSettings::IniFormat);
        const QString f = st.value(QStringLiteral("Project/projectfilename")).toString();
        if (f.isEmpty()) {
            return QString();
        }
        const QString full = QDir(dir).absoluteFilePath(f);
        return QFile::exists(full) ? full : QString();
    };

    QString openPath = p.positionalArguments().value(0);
    if (openPath.isEmpty()) {
        const QString exeDir = QCoreApplication::applicationDirPath();
        const QStringList dirs{
            QDir::currentPath(),
            QDir::cleanPath(QDir(exeDir).absoluteFilePath(QStringLiteral("../project"))),
            QDir::cleanPath(QDir(exeDir).absoluteFilePath(QStringLiteral(".."))),
        };
        for (const QString &d : dirs) {
            openPath = projectInDir(d);
            if (!openPath.isEmpty()) {
                break;
            }
        }
    }
    if (!openPath.isEmpty()) {
        w.openProject(openPath);
    }

    /* 编辑操作与限制的无人值守回归。那套限制（建控件要先选布局、
     * 粘贴只认布局、宽高不能为零……）本来全靠弹框拦人，弹框在没人点的
     * 环境里会把进程挂死，所以 runOpsTest() 先开 EditorOps 的无人值守开关。 */
    if (p.isSet(optSample)) {
        const QString outPath = p.value(optSample);
        const QString picDir = p.value(optSamplePic);
        const QString repFile = p.isSet(optOut) ? p.value(optOut) : QString();
        QTimer::singleShot(300, &app, [&w, &out, outPath, picDir, repFile]() {
            QString rep;
            const int n = w.makeSampleProject(outPath, picDir, &rep);
            out << rep << endl;
            out.flush();
            /* WIN32 子系统程序被重定向时 stdout 拿不到有效句柄，
             * 报告会整份丢掉 —— 和 --ops-test 一样，给了 --out 就落盘。 */
            if (!repFile.isEmpty()) {
                QFile f(repFile);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(rep.toUtf8());
                    f.write("\n", 1);
                }
            }
            QCoreApplication::exit(n > 0 ? 0 : 8);
        });
        return app.exec();
    }

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
     * 完全相同的那条路（CanvasPage::selectNode）。这条路上出过一次
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
        if (p.isSet(optNoChrome)) {
            w.setShowChromeForTest(false);
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
