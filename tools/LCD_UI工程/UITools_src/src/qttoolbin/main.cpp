// QtToolBin —— 工程 json -> 资源描述与布局数据。
//
// 不带参数时**弹界面**：双击弹窗，在界面上点「生成资源文件(F5)」。
// 带下面任一生成类选项则走命令行，不弹窗 —— 自动化/验收脚本用的就是这条路。
//   QtToolBin [工程.json] [选项]
//     --pj-id N          工程 ID（进控件 id 的 bit29..31），默认 0
//     --rotate N         0/1/2/3 -> 0/90/180/270
//     --option-ini <路径> 默认在 <工程目录>/../../../UITools/config/ini/option.ini 找
//     --excel <路径>     写进 Resbuilder.xml 的 excel_path
//     --language 0xNN    语言掩码，默认 0x13
//     --ename <ename.h>  复用已有 ename.h 里的 id（用于和既有资源逐字节比对）
//     -o <目录>          输出目录，默认与工程 json 同目录
//     --verify <目录>    生成后与该目录里现成的一套产物做对比
//     --run-resbuilder <ResBuilder.exe>  生成 Resbuilder.xml 后接着跑资源生成
//     --script <bat>     最后调用的脚本（默认 copy_file.bat，--no-script 关掉）
//     --gui / --cli      强制界面 / 强制命令行
//
// 不给 json 时，从**当前目录**的 config\ini\project.ini 里读：
//     projectfilename   要生成的工程 json
//     projectid         工程 ID  -> --pj-id
//     projectrotate     旋转     -> --rotate
//     projectbatscript  收尾脚本 -> --script
//     projectresbuilder true = 不重新生成资源（界面上那个勾选框）
// 命令行显式给的选项优先于 project.ini。所以启动脚本可以是干净的一行：
//     cd project && ..\..\..\UIToolkit\QtToolBin.exe --run-resbuilder <...>
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
#include <QIcon>
#include <QScopedPointer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QTextStream>

#include "StyBuilder.h"
#include "FeatureDialog.h"
#include "ResbuilderOptions.h"
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
    out() << QStringLiteral("  [异]  %1  本次 %2 / 参考 %3 字节，首个不同 @0x%4，共 %5 字节不同\n")
             .arg(QFileInfo(mine).fileName()).arg(da.size()).arg(db.size())
             .arg(i, 0, 16).arg(ndiff);
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
    /* 默认是弹窗工具：双击 step2 出界面、点「生成资源文件(F5)」才干活。
     * 但只要给了任何一个生成类选项，就当成命令行调用不弹窗
     * —— compat/verify_toolchain.py 和 step2 之外的自动化都走那条路。
     *
     * QApplication 必须在解析参数之前构造（它要吃掉 -style 之类的 Qt 参数），
     * 所以这里先扫一遍 argv 决定要不要图形界面。控制台模式用 QCoreApplication，
     * 免得在没有窗口站的环境（CI）里起不来。 */
    bool wantGui = true;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--cli") || a == QLatin1String("-o")
            || a == QLatin1String("--verify-resxml")
            || a == QLatin1String("--out") || a == QLatin1String("--verify")
            || a == QLatin1String("--run-resbuilder") || a == QLatin1String("--no-script")
            || a == QLatin1String("--ename") || a == QLatin1String("-h")
            || a == QLatin1String("--help")) {
            wantGui = false;
        } else if (a == QLatin1String("--gui")
                   || a == QLatin1String("--shot-feature")) {
            // --shot-feature 要造 QDialog，必须有 QApplication
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
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    } else {
        appHolder.reset(new QCoreApplication(argc, argv));
    }
    QCoreApplication &app = *appHolder;
    const QStringList args = app.arguments();

    /* --verify-resxml <工程目录>
     *
     * 「功能设置」那一页的标准只有一条：**读得进现成那份配置，
     * 再写出去还是同一份**。不然用户点一次生成，原来配好的字体表、
     * 透明色、语言掩码就被悄悄改掉了。
     *
     * 做法：从 <工程目录>/Resbuilder.xml 读设置 -> 拿它生成一份新的 ->
     * 把两份的 LanguageList / Fonts / 尾部设置项逐条对。
     * 只读工程目录，产物写到临时目录，不动原文件。 */
    {
        QString vdir, vreport;
        for (int i = 1; i + 1 < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--verify-resxml")) {
                vdir = args.at(i + 1);
            } else if (args.at(i) == QStringLiteral("--report")) {
                vreport = args.at(i + 1);
            }
        }
        /* 报告先攒着，最后一次性写出去 —— 直接往 stdout 打，父进程用管道
         * 是收不到的（见本文件抬头 AttachConsole 那段）。 */
        QString rep;
        QTextStream rs(&rep);
        auto flushRep = [&rep, &rs, &vreport]() {
            rs.flush();
            if (!vreport.isEmpty()) {
                QFile f(vreport);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(rep.toUtf8());
                }
            }
            out() << rep;
            out().flush();
        };
        if (!vdir.isEmpty()) {
            const QString refXml = QDir(vdir).absoluteFilePath(
                QStringLiteral("Resbuilder.xml"));
            if (!QFile::exists(refXml)) {
                rs << QStringLiteral("找不到 %1\n").arg(refXml);
                                flushRep();
                return 2;
            }
            toolbin::ResbuilderOptions ro;
            if (!ro.loadFromProject(refXml)) {
                rs << QStringLiteral("读不出 %1\n").arg(refXml);
                                flushRep();
                return 2;
            }

            // 工程 json：从 project.ini 取
            QSettings pini(QDir(vdir).absoluteFilePath(
                               QStringLiteral("config/ini/project.ini")),
                           QSettings::IniFormat);
            const QString jf = pini.value(QStringLiteral("Project/projectfilename")).toString();
            const QString jp = QDir(vdir).absoluteFilePath(jf);
            sty::Options vo;
            ro.applyTo(vo);
            vo.projectDir = vdir;
            vo.pjId = pini.value(QStringLiteral("Project/projectid"), 0).toInt();
            vo.rotate = pini.value(QStringLiteral("Project/projectrotate"), 0).toInt();
            vo.optionIni = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("config/ini/option.ini"));
            vo.enameIn = QDir(vdir).absoluteFilePath(QStringLiteral("ename.h"));

            sty::Builder vb;
            QString verr;
            if (!vb.loadProject(jp, &verr) || !vb.loadOptionIni(vo.optionIni, &verr)) {
                rs << QStringLiteral("× %1\n").arg(verr);
                                flushRep();
                return 1;
            }
            const sty::Output vout = vb.build(vo);
            if (!vout.ok) {
                rs << QStringLiteral("× %1\n").arg(vout.error);
                                flushRep();
                return 1;
            }

            /* 逐条比。PageList 里是图片绝对路径，跟机器走，不比。 */
            auto pick = [](const QString &all, const QString &tag) {
                const QString a = QStringLiteral("<%1>").arg(tag);
                const QString b = QStringLiteral("</%1>").arg(tag);
                const int i = all.indexOf(a);
                const int j = all.indexOf(b, i);
                return (i < 0 || j < 0) ? QString() : all.mid(i + a.size(), j - i - a.size());
            };
            /* 【两边都按 UTF-8 优先解】现成那份是 UTF-8；这边历史上写过 GBK，
             * 所以沿用 ResConfig 那套探测。用错编码的话中文字数对不上，
             * 会报出一堆假的"不一致"。 */
            auto decode = [](const QByteArray &raw) {
                QString t = QString::fromUtf8(raw);
                if (t.contains(QChar(0xFFFD)) || t.toUtf8() != raw) {
                    t = QString::fromLocal8Bit(raw);
                }
                return t;
            };
            QFile rf(refXml);
            rf.open(QIODevice::ReadOnly);
            const QString refS = decode(rf.readAll());
            const QString newS = decode(vout.resbuilderXml);

            const QStringList tags{
                QStringLiteral("endian"), QStringLiteral("paneltype"),
                QStringLiteral("picture_path"), QStringLiteral("excel_path"),
                QStringLiteral("language"), QStringLiteral("bmp_transparent_color"),
                QStringLiteral("png_background_color"), QStringLiteral("res"),
                QStringLiteral("resfilename"), QStringLiteral("headerfilename"),
                QStringLiteral("image_compress_method"),
                QStringLiteral("string_compress_method"),
                QStringLiteral("palette_type"), QStringLiteral("rotate"),
            };
            int bad = 0;
            for (const QString &t : tags) {
                const QString a = pick(refS, t), b = pick(newS, t);
                if (a == b) {
                    rs << QStringLiteral("  [同] %1 = %2\n").arg(t, a);
                } else {
                    rs << QStringLiteral("  [异] %1  参考=%2  本次=%3\n").arg(t, a, b);
                    ++bad;
                }
            }
            // LanguageList / Fonts 整段比
            auto section = [](const QString &all, const QString &tag) {
                const QString a = QStringLiteral("<%1>").arg(tag);
                const QString b = QStringLiteral("</%1>").arg(tag);
                const int i = all.indexOf(a);
                const int j = all.indexOf(b, i);
                return (i < 0 || j < 0) ? QString() : all.mid(i, j - i);
            };
            /* 【属性次序不算差异】<fontNN .../> 每个属性的先后**每次生成
             * 都不一样**（同一台机器、同一个工程，隔一次跑就换一种排法）——
             * 和 ColorList 的排序是同一个成因：Qt 容器 + 进程级随机 hash 种子，
             * 详见 docs/QTTOOLBIN.md「ColorList 排序」。XML 属性本来就是无序的，
             * ResBuilder 解出来完全一样，所以这里按"属性名排序后"再比。 */
            auto canonSection = [](const QString &sec) {
                QStringList lines;
                QRegularExpression re(QStringLiteral("<(\\w+)\\s+([^>]*?)/>"));
                QRegularExpressionMatchIterator it = re.globalMatch(sec);
                while (it.hasNext()) {
                    const QRegularExpressionMatch m = it.next();
                    QRegularExpression ar(QStringLiteral(
                        "(\\w+)\\s*=\\s*\"([^\"]*)\""));
                    QRegularExpressionMatchIterator ai = ar.globalMatch(m.captured(2));
                    QStringList kv;
                    while (ai.hasNext()) {
                        const QRegularExpressionMatch a = ai.next();
                        kv << a.captured(1) + QLatin1Char('=') + a.captured(2);
                    }
                    kv.sort();
                    lines << m.captured(1) + QLatin1Char(' ') + kv.join(QLatin1Char(' '));
                }
                /* 一个自闭合元素都没有（LanguageList 那种带文本的）就原样比 */
                return lines.isEmpty() ? sec.simplified() : lines.join(QLatin1Char('\n'));
            };
            for (const QString &t : { QStringLiteral("LanguageList"),
                                      QStringLiteral("Fonts") }) {
                const QString a = canonSection(section(refS, t));
                const QString b = canonSection(section(newS, t));
                if (a == b) {
                    rs << QStringLiteral("  [同] %1 整段一致（属性名排序后比）\n").arg(t);
                } else {
                    rs << QStringLiteral("  [异] %1 不一致\n").arg(t);
                    const QStringList la = a.split(QLatin1Char('\n'));
                    const QStringList lb = b.split(QLatin1Char('\n'));
                    for (int i = 0; i < qMax(la.size(), lb.size()); ++i) {
                        if (la.value(i) != lb.value(i)) {
                            rs << QStringLiteral("        参考: %1\n        本次: %2\n")
                                     .arg(la.value(i), lb.value(i));
                            break;
                        }
                    }
                    ++bad;
                }
            }
            rs << QStringLiteral("\n配置项不一致 %1 处\n").arg(bad);
                        flushRep();
            return bad == 0 ? 0 : 1;
        }
    }

    /* 只把「配置界面」出一张图再退出。改过那一页的版式之后，"没崩"不等于
     * "没排坏"（控件被挤掉半截照样不崩），得能看一眼。只给自动化回归用。 */
    {
        QString featShot;
        for (int i = 1; i + 1 < args.size(); ++i) {
            if (args.at(i) == QStringLiteral("--shot-feature")) {
                featShot = args.at(i + 1);
            }
        }
        if (!featShot.isEmpty()) {
            toolbin::ResbuilderOptions o = toolbin::ResbuilderOptions::defaults();
            toolbin::FeatureDialog d(o);
            d.resize(d.sizeHint().expandedTo(QSize(720, 660)));
            bool ok = d.grab().save(featShot);

            /* 顺带把「配置语言」那张表的行为验一遍。QFontDialog 是模态的，
             * 无人值守点不了，所以走 applyFontForTest 这条等价路径。 */
            if (!d.cellsReadOnlyForTest()) {
                out() << QStringLiteral("x 表格单元格可以直接编辑（应该是只读的）\n");
                ok = false;
            } else {
                out() << QStringLiteral("配置语言表：单元格只读，符合预期\n");
            }
            if (d.rowCountForTest() > 0) {
                QFont nf(QStringLiteral("Arial"), 20);
                nf.setBold(true);
                nf.setItalic(true);
                nf.setUnderline(true);
                nf.setStrikeOut(true);
                d.applyFontForTest(0, nf);
                const toolbin::LangRow &r0 = d.options().langs.at(0);
                const bool hit = r0.face == QStringLiteral("Arial") && r0.point == 20
                                 && r0.bold && r0.italic && r0.underline && r0.strikeOut;
                out() << (hit ? QStringLiteral("字体弹窗那条路改得到值\n")
                              : QStringLiteral("x 选了字体但没写回去\n"));
                /* lfHeight = -(pt*4/3)：20pt 应当写成 -26 */
                const int h = r0.lfHeight();
                out() << QStringLiteral("  20pt -> lfHeight %1（应为 -26）\n").arg(h);
                ok = ok && hit && h == -26;
            }
            /* 「增加」那条路：弹窗填的名字要落到表里，LANG 里的非法字符要被挡掉
             * （它会变成 result.h 的宏名，带中文/空格就编不过）。 */
            {
                const int before = d.rowCountForTest();
                d.addLangForTest(QStringLiteral("越南语"),
                                 QStringLiteral("Viet namese-2"));
                const bool grew = d.rowCountForTest() == before + 1;
                const toolbin::LangRow &nr = d.options().langs.last();
                const bool named = nr.name == QStringLiteral("越南语")
                                   && nr.key == QStringLiteral("Vietnamese2");
                out() << (grew && named
                          ? QStringLiteral("增加语言：名字进表了，LANG 已滤成 %1\n").arg(nr.key)
                          : QStringLiteral("x 增加语言没生效（%1 / %2）\n")
                            .arg(nr.name, nr.key));
                ok = ok && grew && named;
            }
            out() << QStringLiteral("配置界面自截 %1: %2\n")
                     .arg(featShot, ok ? QStringLiteral("成功") : QStringLiteral("失败"));
            out().flush();
            return ok ? 0 : 8;
        }
    }


    QString jsonPath, outDir, refDir, resbuilderExe, script = QStringLiteral("copy_file.bat");
    bool noScript = false;
    // project.ini 只在命令行没给的时候才生效
    bool pjIdSet = false, rotateSet = false, scriptSet = false;
    bool excelSet = false, langSet = false;
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
            excelSet = true;
        } else if (a == QLatin1String("--language")) {
            const QString v = next();
            opt.language = v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                           ? v.mid(2).toUInt(nullptr, 16) : v.toUInt();
            langSet = true;
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
        } else if (a == QLatin1String("--verify-resxml")
                   || a == QLatin1String("--report")) {
            next();   // 值在下面那段里单独取
        } else if (a == QLatin1String("--shot")
                   || a == QLatin1String("--shot-feature")) {
            next();   // 值在界面分支里单独取，这里只是别让它掉进"工程 json"
        } else if (a == QLatin1String("-h") || a == QLatin1String("--help")) {
            out() << QStringLiteral("用法: QtToolBin <工程.json> [--pj-id N] [--rotate N] [--option-ini 路径]\n"
                "                [--excel 路径] [--language 0xNN] [--ename ename.h]\n"
                "                [-o 输出目录] [--verify 参考目录]\n"
                "                [--run-resbuilder ResBuilder.exe] [--script bat|--no-script]\n"
                "                [--verify-resxml 工程目录 [--report 文件]]\n");
            out().flush();
            return 0;
        } else if (!a.startsWith(QLatin1Char('-'))) {
            jsonPath = a;
        }
    }

    if (wantGui) {
        /* 工程目录：给了 json 就用它所在的目录，否则用当前目录
         * （step2 里就是 cd 到 project 再启动的）。 */
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
                    /* 【生成完必须把光标还回去】覆盖光标是个栈：
                     * setOverrideCursor 压一层、restoreOverrideCursor 弹一层。
                     * 以前 setBusy() 两个方向都压、只弹一次，每生成一次就残留
                     * 一层 WaitCursor —— 输出已经打印"生成完成"，鼠标却一直
                     * 转圈而且点不掉。这里直接查栈空不空。 */
                    if (QApplication::overrideCursor()) {
                        out() << QStringLiteral(
                            "x 生成结束后覆盖光标没还回去（鼠标会一直转圈）\n");
                        ok = false;
                    } else {
                        out() << QStringLiteral("生成结束光标已还原\n");
                    }
                }
                if (!shot.isEmpty()) {
                    ok = w.grab().save(shot) && ok;
                }
                QCoreApplication::exit(ok ? 0 : 7);
            });
        }
        return app.exec();
    }

    /* 不给 json 就读 config/ini/project.ini。
     * 界面上那几项（JSON文件 / 工程ID / 旋转 / 调用脚本 /
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
    opt.outDir = outDir;            // excel_path 相对它折算

    /* 【「功能设置」跟着工程走】这一页存在 <工程目录>/Resbuilder.xml 里
     * （见 ResbuilderOptions.h 抬头）。界面那条路在 ToolBinWindow 里读，
     * 命令行这条也得读 —— 不然 step2 双击一次和脚本跑一次出来的字体表、
     * 透明色会不一样。
     * 命令行显式给的 --excel / --language 优先，所以先存后恢复。 */
    {
        const QString savedExcel = opt.excelPath;
        const quint32 savedLang = opt.language;
        toolbin::ResbuilderOptions ro;
        if (ro.loadFromProject(QDir(projDir).absoluteFilePath(
                QStringLiteral("Resbuilder.xml")))) {
            ro.applyTo(opt);
        }
        if (excelSet) {
            opt.excelPath = savedExcel;
        }
        if (langSet) {
            opt.language = savedLang;
        }
    }
    /* 工程目录一般是 .../ui_xxx/<界面>/project，工具目录在上三级。
     * 先找放着本 exe 的那个工具目录，找不到再退回 UITools ——
     * 这样两套工具目录都能用。 */
    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList toolRoots;
    toolRoots << exeDir
              << QDir::cleanPath(QDir(projDir).absoluteFilePath(
                     QStringLiteral("../../../UIToolkit")))
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

    /* 【Resbuilder.dat 要跟着 Resbuilder.xml 走】
     * 它是字符串表的**逐格字体**（见 docs/FILE_FORMATS.md 10.6），
     * ResBuilder 在 xml 所在目录找它。就地生成时两者天然同目录；
     * 这里支持 -o 输出到别处，不带过去就会丢掉逐格字体 —— 该用宋体 -11 的
     * 那几条会按默认的 -16 渲染，result.str 直接对不上。 */
    {
        const QString srcDat = QDir(QFileInfo(jsonPath).absolutePath())
                               .absoluteFilePath(QStringLiteral("Resbuilder.dat"));
        const QString dstDat = dir.absoluteFilePath(QStringLiteral("Resbuilder.dat"));
        if (QFileInfo::exists(srcDat)
            && QFileInfo(srcDat).absoluteFilePath() != QFileInfo(dstDat).absoluteFilePath()) {
            QFile::remove(dstDat);
            if (QFile::copy(srcDat, dstDat)) {
                out() << QStringLiteral("  带上 Resbuilder.dat（逐格字体）\n");
            } else {
                out() << QStringLiteral("  警告: 复制 Resbuilder.dat 失败，"
                                        "逐格字体会退回默认字体\n");
            }
        }
    }
    out().flush();

    int rc = 0;
    if (!refDir.isEmpty()) {
        out() << QStringLiteral("\n与参考产物对比：\n");
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
