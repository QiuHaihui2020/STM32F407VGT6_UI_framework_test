// QtToolBin（重建版）—— 原厂 QtToolBin.exe 的替代品。
//
// 不带参数时**弹界面**（和原厂一样：双击弹窗，在界面上点「生成资源文件(F5)」）。
// 带下面任一生成类选项则走命令行，不弹窗 —— 自动化/验收脚本用的就是这条路。
//   QtToolBin [工程.json] [选项]
//     --pj-id N          工程 ID（进控件 id 的 bit29..31），默认 0
//     --rotate N         0/1/2/3 -> 0/90/180/270
//     --option-ini <路径> 默认在 <工程目录>/../../../UITools/config/ini/option.ini 找
//     --excel <路径>     写进 Resbuilder.xml 的 excel_path
//     --language 0xNN    语言掩码，默认 0x13
//     --ename <ename.h>  复用已有 ename.h 里的 id（用于和原厂产物逐字节对拍）
//     -o <目录>          输出目录，默认与工程 json 同目录
//     --verify <目录>    生成后与该目录里的原厂产物对比
//     --run-resbuilder <ResBuilder.exe>  生成 Resbuilder.xml 后接着跑资源生成
//     --script <bat>     最后调用的脚本（默认 copy_file.bat，--no-script 关掉）
//     --gui / --cli      强制界面 / 强制命令行
//
// 不给 json 时，按原厂的规矩从**当前目录**的 config\ini\project.ini 里读：
//     projectfilename   要生成的工程 json
//     projectid         工程 ID  -> --pj-id
//     projectrotate     旋转     -> --rotate
//     projectbatscript  收尾脚本 -> --script
//     projectresbuilder true = 不重新生成资源（原厂界面上那个勾选框）
// 命令行显式给的选项优先于 project.ini。所以启动脚本可以是干净的一行：
//     cd project && ..\..\..\UITools_rebuilt\QtToolBin.exe --run-resbuilder <...>
//
// 产出：project.bin / ename.h / Resbuilder.xml / debug.txt
#include <QApplication>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <cstdio>
#endif
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QScopedPointer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QTextStream>

#include "StyBuilder.h"
#include "ToolBinWindow.h"

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    f.write(data);
    f.close();
    return true;
}

int compareOne(const QString &mine, const QString &ref)
{
    QFile a(mine), b(ref);
    if (!b.exists()) {
        out() << QStringLiteral("  [缺]  %1\n").arg(QFileInfo(ref).fileName());
        return 0;
    }
    a.open(QIODevice::ReadOnly);
    b.open(QIODevice::ReadOnly);
    const QByteArray da = a.readAll(), db = b.readAll();
    if (da == db) {
        out() << QStringLiteral("  [同]  %1  %2 字节\n")
                 .arg(QFileInfo(mine).fileName()).arg(da.size());
        return 0;
    }
    int i = 0;
    while (i < qMin(da.size(), db.size()) && da.at(i) == db.at(i)) {
        ++i;
    }
    int ndiff = 0;
    for (int k = 0; k < qMin(da.size(), db.size()); ++k) {
        if (da.at(k) != db.at(k)) {
            ++ndiff;
        }
    }
    out() << QStringLiteral("  [异]  %1  本版 %2 / 原厂 %3 字节，首个不同 @0x%4，共 %5 字节不同\n")
             .arg(QFileInfo(mine).fileName()).arg(da.size()).arg(db.size())
             .arg(i, 0, 16).arg(ndiff);
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    /* 原厂 QtToolBin 是弹窗工具，双击 step2 出界面、点「生成资源文件(F5)」才干活。
     * 重建版默认也这样；但只要给了任何一个生成类选项，就当成命令行调用不弹窗
     * —— re/verify_toolchain.py 和 step2 之外的自动化都走那条路。
     *
     * QApplication 必须在解析参数之前构造（它要吃掉 -style 之类的 Qt 参数），
     * 所以这里先扫一遍 argv 决定要不要图形界面。控制台模式用 QCoreApplication，
     * 免得在没有窗口站的环境（CI）里起不来。 */
    bool wantGui = true;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--cli") || a == QLatin1String("-o")
            || a == QLatin1String("--out") || a == QLatin1String("--verify")
            || a == QLatin1String("--run-resbuilder") || a == QLatin1String("--no-script")
            || a == QLatin1String("--ename") || a == QLatin1String("-h")
            || a == QLatin1String("--help")) {
            wantGui = false;
        } else if (a == QLatin1String("--gui")) {
            wantGui = true;
            break;
        }
    }

#ifdef Q_OS_WIN
    /* 这个 exe 是 WIN32 子系统的 —— 双击 step2 弹界面时才不会顺带蹦一个黑窗口。
     * 代价是命令行模式下 stdout 没接到父进程的控制台，在 cmd 里跑什么都看不见
     * （重定向到文件/管道是好的，所以验收脚本一直正常）。这里把父进程的控制台
     * 接回来，命令行模式就和普通控制台程序一样了。 */
    if (!wantGui && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
#endif

    QScopedPointer<QCoreApplication> appHolder;
    if (wantGui) {
        appHolder.reset(new QApplication(argc, argv));
        QFont f(QStringLiteral("SimSun"), 9);
        QApplication::setFont(f);
    } else {
        appHolder.reset(new QCoreApplication(argc, argv));
    }
    QCoreApplication &app = *appHolder;
    const QStringList args = app.arguments();

    QString jsonPath, outDir, refDir, resbuilderExe, script = QStringLiteral("copy_file.bat");
    bool noScript = false;
    // project.ini 只在命令行没给的时候才生效
    bool pjIdSet = false, rotateSet = false, scriptSet = false;
    sty::Options opt;

    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        auto next = [&]() { return (i + 1 < args.size()) ? args.at(++i) : QString(); };
        if (a == QLatin1String("--pj-id")) {
            opt.pjId = next().toInt();
            pjIdSet = true;
        } else if (a == QLatin1String("--rotate")) {
            opt.rotate = next().toInt();
            rotateSet = true;
        } else if (a == QLatin1String("--option-ini")) {
            opt.optionIni = next();
        } else if (a == QLatin1String("--excel")) {
            opt.excelPath = next();
        } else if (a == QLatin1String("--language")) {
            const QString v = next();
            opt.language = v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                           ? v.mid(2).toUInt(nullptr, 16) : v.toUInt();
        } else if (a == QLatin1String("--ename")) {
            opt.enameIn = next();
        } else if (a == QLatin1String("-o") || a == QLatin1String("--out")) {
            outDir = next();
        } else if (a == QLatin1String("--verify")) {
            refDir = next();
        } else if (a == QLatin1String("--run-resbuilder")) {
            resbuilderExe = next();
        } else if (a == QLatin1String("--script")) {
            script = next();
            scriptSet = true;
        } else if (a == QLatin1String("--no-script")) {
            noScript = true;
        } else if (a == QLatin1String("--gui") || a == QLatin1String("--cli")
                   || a == QLatin1String("--selftest-generate")) {
            // 上面挑运行模式时已经处理过了
        } else if (a == QLatin1String("--shot")) {
            next();   // 值在界面分支里单独取，这里只是别让它掉进"工程 json"
        } else if (a == QLatin1String("-h") || a == QLatin1String("--help")) {
            out() << QStringLiteral("用法: QtToolBin <工程.json> [--pj-id N] [--rotate N] [--option-ini 路径]\n"
                "                [--excel 路径] [--language 0xNN] [--ename ename.h]\n"
                "                [-o 输出目录] [--verify 原厂目录]\n"
                "                [--run-resbuilder ResBuilder.exe] [--script bat|--no-script]\n");
            out().flush();
            return 0;
        } else if (!a.startsWith(QLatin1Char('-'))) {
            jsonPath = a;
        }
    }

    if (wantGui) {
        /* 工程目录：给了 json 就用它所在的目录，否则用当前目录
         * （step2 里是 cd 到 project 再启动的，和原厂一样）。 */
        const QString projDirGui = jsonPath.isEmpty()
            ? QDir::currentPath()
            : QFileInfo(jsonPath).absolutePath();
        ToolBinWindow w(projDirGui);
        w.show();

        /* 自测：--selftest-generate 等于自动点一次「生成资源文件」，
         * 并且**不跑收尾脚本**（免得往固件工程里拷东西）；
         * --shot 顺手把窗口截下来。两个都只给自动化回归用。 */
        const bool selftest = args.contains(QStringLiteral("--selftest-generate"));
        QString shot;
        for (int i = 1; i + 1 < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--shot")) {
                shot = args.at(i + 1);
            }
        }
        if (selftest || !shot.isEmpty()) {
            w.setRunScriptEnabled(!selftest);
            w.setSilent(selftest);
            QTimer::singleShot(400, &w, [&w, selftest, shot]() {
                bool ok = true;
                if (selftest) {
                    ok = w.generateForTest();
                }
                if (!shot.isEmpty()) {
                    ok = w.grab().save(shot) && ok;
                }
                QCoreApplication::exit(ok ? 0 : 7);
            });
        }
        return app.exec();
    }

    /* 不给 json 就按原厂的规矩读 config/ini/project.ini。
     * 原厂 QtToolBin 的界面上那几项（JSON文件 / 工程ID / 旋转 / 调用脚本 /
     * 不重新生成资源文件）就是存在这个文件里的，双击 bat 直接跑靠的就是它。 */
    bool skipRes = false;
    {
        const QString iniPath = QDir(jsonPath.isEmpty()
                                     ? QDir::currentPath()
                                     : QFileInfo(jsonPath).absolutePath())
                                .absoluteFilePath(QStringLiteral("config/ini/project.ini"));
        if (QFile::exists(iniPath)) {
            QSettings ini(iniPath, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("Project"));
            const QString f = ini.value(QStringLiteral("projectfilename")).toString();
            if (jsonPath.isEmpty() && !f.isEmpty()) {
                jsonPath = QDir::current().absoluteFilePath(f);
            }
            if (!pjIdSet) {
                opt.pjId = ini.value(QStringLiteral("projectid"), 0).toInt();
            }
            if (!rotateSet) {
                opt.rotate = ini.value(QStringLiteral("projectrotate"), 0).toInt();
            }
            if (!scriptSet) {
                const QString b = ini.value(QStringLiteral("projectbatscript")).toString();
                if (!b.isEmpty()) {
                    script = b;
                }
            }
            skipRes = ini.value(QStringLiteral("projectresbuilder"), false).toBool();
            ini.endGroup();
            out() << QStringLiteral("读配置 %1\n").arg(QDir::toNativeSeparators(iniPath));
        }
    }

    if (jsonPath.isEmpty()) {
        // 空工程（刚用「新建工程.bat」拉出来的）会走到这儿，给条明确的下一步
        const QString ini = QDir::current().absoluteFilePath(
            QStringLiteral("config/ini/project.ini"));
        if (QFile::exists(ini)) {
            out() << QStringLiteral("config\\ini\\project.ini 里的 projectfilename 是空的 —— "
                "这个工程还没建界面。\n"
                "先跑 step1 打开编辑器，新建页面并保存，"
                "保存时会自动把工程名写回这个 ini，然后再跑 step2。\n");
        } else {
            out() << QStringLiteral("要给一个工程 json（或在工程目录里放 "
                                   "config\\ini\\project.ini），-h 看用法\n");
        }
        out().flush();
        return 1;
    }
    const QString projDir = QFileInfo(jsonPath).absolutePath();
    if (outDir.isEmpty()) {
        outDir = projDir;
    }
    opt.projectDir = projDir;
    /* 工程目录一般是 .../ui_xxx/<界面>/project，工具目录在上三级。
     * 先找放着本 exe 的那个工具目录（重建版通常叫 UITools_rebuilt），
     * 找不到再退回原厂的 UITools —— 这样两套工具目录都能用。 */
    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList toolRoots;
    toolRoots << exeDir
              << QDir::cleanPath(QDir(projDir).absoluteFilePath(
                     QStringLiteral("../../../UITools_rebuilt")))
              << QDir::cleanPath(QDir(projDir).absoluteFilePath(
                     QStringLiteral("../../../UITools")));
    if (opt.optionIni.isEmpty()) {
        for (const QString &r : toolRoots) {
            const QString c = QDir(r).absoluteFilePath(QStringLiteral("config/ini/option.ini"));
            if (QFile::exists(c)) {
                opt.optionIni = c;
                break;
            }
        }
    }
    // 多国语言 xls：命令行没给就在工具目录里找唯一的那个
    if (opt.excelPath.isEmpty()) {
        for (const QString &r : toolRoots) {
            const QStringList x = QDir(r).entryList(QStringList{ QStringLiteral("*.xls") },
                                                    QDir::Files);
            if (!x.isEmpty()) {
                opt.excelPath = QDir(r).absoluteFilePath(x.first());
                break;
            }
        }
    }

    sty::Builder b;
    QString err;
    if (!b.loadProject(jsonPath, &err)) {
        out() << QStringLiteral("%1\n").arg(err);
        out().flush();
        return 1;
    }
    if (!b.loadOptionIni(opt.optionIni, &err)) {
        out() << QStringLiteral("%1\n").arg(err);
        out().flush();
        return 1;
    }
    out() << QStringLiteral("工程: %1\n").arg(QDir::toNativeSeparators(jsonPath));
    out() << QStringLiteral("类型表: %1\n").arg(QDir::toNativeSeparators(opt.optionIni));

    const sty::Output o = b.build(opt);
    for (const QString &w : o.warnings) {
        out() << QStringLiteral("  警告: %1\n").arg(w);
    }
    if (!o.ok) {
        out() << QStringLiteral("失败: %1\n").arg(o.error);
        out().flush();
        return 1;
    }

    struct F { const char *name; QByteArray data; };
    const QVector<F> files = {
        { "project.bin",     o.sty },
        { "ename.h",         o.enameH },
        { "Resbuilder.xml",  o.resbuilderXml },
        { "debug.txt",       o.debugTxt },
    };
    QDir dir(outDir);
    dir.mkpath(QStringLiteral("."));
    for (const F &f : files) {
        const QString p = dir.absoluteFilePath(QLatin1String(f.name));
        if (!writeFile(p, f.data)) {
            out() << QStringLiteral("写不了 %1\n").arg(p);
            out().flush();
            return 1;
        }
        out() << QStringLiteral("  写出 %1  %2 字节\n").arg(QLatin1String(f.name)).arg(f.data.size());
    }
    out().flush();

    int rc = 0;
    if (!refDir.isEmpty()) {
        out() << QStringLiteral("\n与原厂产物对比：\n");
        for (const F &f : files) {
            rc |= compareOne(dir.absoluteFilePath(QLatin1String(f.name)),
                             QDir(refDir).absoluteFilePath(QLatin1String(f.name)));
        }
        out().flush();
    }

    if (skipRes && !resbuilderExe.isEmpty()) {
        out() << QStringLiteral("\nproject.ini 里 projectresbuilder=true，跳过资源生成\n");
        resbuilderExe.clear();
    }
    if (!resbuilderExe.isEmpty()) {
        out() << QStringLiteral("\n调 ResBuilder…\n");
        out().flush();
        QProcess p;
        p.setWorkingDirectory(outDir);
        p.start(resbuilderExe, QStringList()
                << dir.absoluteFilePath(QStringLiteral("Resbuilder.xml"))
                << QStringLiteral("-o") << outDir);
        p.waitForFinished(-1);
        out() << QString::fromLocal8Bit(p.readAllStandardOutput());
        if (p.exitCode() != 0) {
            out() << QStringLiteral("ResBuilder 返回 %1\n").arg(p.exitCode());
            rc |= 1;
        }
        out().flush();
    }

    if (!noScript && !script.isEmpty()) {
        const QString bat = dir.absoluteFilePath(script);
        if (QFile::exists(bat)) {
            out() << QStringLiteral("\n调脚本 %1…\n").arg(script);
            out().flush();
            QProcess p;
            p.setWorkingDirectory(outDir);
            p.start(QStringLiteral("cmd"), QStringList() << QStringLiteral("/c") << bat);
            p.waitForFinished(-1);
            out() << QString::fromLocal8Bit(p.readAllStandardOutput());
            out().flush();
        }
    }
    return rc;
}
