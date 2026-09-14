// 资源打包 —— 资源描述文件 + 位图 + 多国语言表 -> 资源二进制与索引头文件。
//
// 这一段以前是独立的 ResBuilder.exe，现在是 UITools.exe 的一个子命令：
//
//   UITools --pack                       在当前目录找 Resbuilder.xml
//   UITools --pack <Resbuilder.xml>      指定输入
//   UITools --pack <xml> -o <目录>       指定输出目录（默认与 xml 同目录）
//   UITools --pack <xml> --verify <目录> 生成后与该目录里现成的产物逐字节对比
//
// 输出：result.bin / result.str / result.h / res_ver.h /
//       result_pic_index.h / result_str_index.h / result.csv / result.xml
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "ResBuilderCore.h"
#include "ResConfig.h"

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

int verifyAgainst(const QString &refDir, const QStringList &written)
{
    int same = 0, diff = 0, missing = 0;
    for (const QString &p : written) {
        const QString name = QFileInfo(p).fileName();
        const QString refPath = QDir(refDir).absoluteFilePath(name);
        QFile a(p), b(refPath);
        if (!b.exists()) {
            out() << QStringLiteral("  [缺]  %1（参考目录里没有）\n").arg(name);
            ++missing;
            continue;
        }
        a.open(QIODevice::ReadOnly);
        b.open(QIODevice::ReadOnly);
        const QByteArray da = a.readAll(), db = b.readAll();
        if (da == db) {
            out() << QStringLiteral("  [同]  %1  %2 字节\n").arg(name).arg(da.size());
            ++same;
        } else {
            int firstDiff = -1;
            for (int i = 0; i < qMin(da.size(), db.size()); ++i) {
                if (da.at(i) != db.at(i)) {
                    firstDiff = i;
                    break;
                }
            }
            if (firstDiff < 0) {
                firstDiff = qMin(da.size(), db.size());
            }
            out() << QStringLiteral("  [异]  %1  本次 %2 字节 / 参考 %3 字节，"
                                    "首个不同 @0x%4\n")
                     .arg(name).arg(da.size()).arg(db.size())
                     .arg(firstDiff, 0, 16);
            ++diff;
        }
    }
    out() << QStringLiteral("\n对比结果：一致 %1 / 不同 %2 / 缺失 %3\n")
             .arg(same).arg(diff).arg(missing);
    out().flush();
    return diff == 0 ? 0 : 2;
}

} // namespace

/* 由 src/main.cpp 的子命令分发调进来。QCoreApplication 在这儿建 ——
 * 分发发生在任何 QApplication 构造之前，不会重复。 */
int resbuilderMain(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    QString xmlPath, outDir, refDir;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if ((a == QLatin1String("-o") || a == QLatin1String("--out")) && i + 1 < args.size()) {
            outDir = args.at(++i);
        } else if (a == QLatin1String("--verify") && i + 1 < args.size()) {
            refDir = args.at(++i);
        } else if (a == QLatin1String("-h") || a == QLatin1String("--help")) {
            out() << QStringLiteral("用法: UITools --pack [Resbuilder.xml] "
                                    "[-o 输出目录] [--verify 参考目录]\n");
            out().flush();
            return 0;
        } else if (!a.startsWith(QLatin1Char('-'))) {
            xmlPath = a;
        }
    }
    if (xmlPath.isEmpty()) {
        xmlPath = QDir::current().absoluteFilePath(QStringLiteral("Resbuilder.xml"));
    }
    if (!QFile::exists(xmlPath)) {
        out() << QStringLiteral("找不到 %1\n").arg(xmlPath);
        out().flush();
        return 1;
    }
    if (outDir.isEmpty()) {
        outDir = QFileInfo(xmlPath).absolutePath();
    }

    res::ResConfig cfg;
    QString err;
    if (!cfg.load(xmlPath, &err)) {
        out() << QStringLiteral("读配置失败: %1\n").arg(err);
        out().flush();
        return 1;
    }
    out() << QStringLiteral("配置: %1\n").arg(QDir::toNativeSeparators(xmlPath));
    out() << QStringLiteral("  面板=%1  语言掩码=0x%2  页数=%3  旋转=%4\n")
             .arg(cfg.panelType).arg(cfg.language, 8, 16, QLatin1Char('0'))
             .arg(cfg.pages.size()).arg(cfg.rotate);

    res::ResBuilderCore core(cfg);
    const res::BuildResult r = core.build(outDir);
    for (const QString &w : r.warnings) {
        out() << QStringLiteral("  警告: %1\n").arg(w);
    }
    if (!r.ok) {
        out() << QStringLiteral("失败: %1\n").arg(r.error);
        out().flush();
        return 1;
    }
    for (const QString &p : r.written) {
        out() << QStringLiteral("  写出 %1  %2 字节\n")
                 .arg(QFileInfo(p).fileName()).arg(QFileInfo(p).size());
    }
    out().flush();

    if (!refDir.isEmpty()) {
        out() << QStringLiteral("\n与参考产物对比（%1）：\n")
                 .arg(QDir::toNativeSeparators(refDir));
        return verifyAgainst(refDir, r.written);
    }
    return 0;
}
