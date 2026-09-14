/*
 * Products.h —— 这条链会在工程目录里产出哪些文件，以及怎么把它们清掉。
 *
 * 【为什么要有这么一张明确的表】清理产物这件事**绝对不能用通配符**。
 * 既有工程里带的那个 release.bat 就是反面教材：
 *     del *.txt   把 version.txt 也删了（那不是产物，删了回不来）
 *     del *.xml   把 Resbuilder.xml 删了还好，但同目录别的 xml 也一起没
 *     del config\*.png  文字预览图 —— 本工具链根本不产这些，删了找不回来
 * 所以这里逐个文件名列清楚，只删自己写出来的东西。
 *
 * 谁写的：
 *     project.bin ename.h Resbuilder.xml debug.txt   --gen（StyBuilder）
 *     result.* res_ver.h result_*_index.h            --pack（ResBuilderCore）
 *
 * 头文件里内联：这张表编辑器和两个子命令都要用，而它们在同一个 exe 里，
 * 但没有共用的静态库可挂（和 BuildDate.h / AssetPaths.h 一样的理由）。
 */
#ifndef PRODUCTS_H
#define PRODUCTS_H

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

namespace products {

/** 生成物的文件名，全在工程目录一层，不递归。 */
inline QStringList names()
{
    return QStringList{
        // --gen
        QStringLiteral("project.bin"),
        QStringLiteral("ename.h"),
        QStringLiteral("Resbuilder.xml"),
        QStringLiteral("debug.txt"),
        // --pack
        QStringLiteral("result.bin"),
        QStringLiteral("result.str"),
        QStringLiteral("result.h"),
        QStringLiteral("result.csv"),
        QStringLiteral("result.xml"),
        QStringLiteral("res_ver.h"),
        QStringLiteral("result_pic_index.h"),
        QStringLiteral("result_str_index.h"),
    };
}

/**
 * 把工程目录里的生成物删掉。
 *
 * 【不碰别的东西】只删 names() 里那几个确切的文件名，不认通配符、不进子目录。
 * 工程的 json、config\ 下的图片、多国语言表、copy_file.bat 一律不动。
 *
 * @param removed 非空时，回填实际删掉的文件名
 * @return 删掉几个
 */
inline int clean(const QString &projectDir, QStringList *removed = nullptr)
{
    const QDir d(projectDir);
    int n = 0;
    for (const QString &f : names()) {
        const QString p = d.filePath(f);
        if (QFile::exists(p) && QFile::remove(p)) {
            if (removed) {
                removed->append(f);
            }
            ++n;
        }
    }
    return n;
}

} // namespace products

#endif // PRODUCTS_H
