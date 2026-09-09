// 从工程 json 生成 project.bin(.sty) / ename.h / Resbuilder.xml / debug.txt。
//
// 这是原厂 QtToolBin.exe 的核心。所有规则都在 re/ 下有可复跑的验证脚本：
//   控件类型码      re/verify_types.py    273/273
//   记录排列顺序    re/verify_order.py    3/3 页（"孩子块序"）
//   css 内容        re/verify_css.py      各字段 273/273
//   控件负载        re/verify_payload.py  全部一致
//   资源号          re/verify_resids.py   511/511
//   数据区排布/CRC  re/sty_gen.py         24498 字节逐字节相同
//
// 唯一无法复现的是控件 id 的低 16 位：原厂是 per-page QMap 查表（很可能是带随机
// 种子的 qHash），同一份工程每次生成都不一样。本实现用 CRC16(ename) + 线性探测，
// 确定性且自洽（.sty 与 ename.h 一起产出，固件只认宏名）。
// 想和原厂产物逐字节对拍时，用 --ename 把原厂 ename.h 喂进来复用它的 id。
#ifndef STYBUILDER_H
#define STYBUILDER_H

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sty {

struct Options {
    int     pjId = 0;               ///< 界面上的"工程ID"，进 id 的 bit29..31
    int     rotate = 0;             ///< 0/1/2/3 -> 0/90/180/270
    QString optionIni;              ///< UITools/config/ini/option.ini
    QString projectDir;             ///< 工程目录（图片相对路径的基准）
    /** Resbuilder.xml 最终落在哪个目录。空 = 就落在工程目录。
     *  excel_path 要相对它折算 —— 原厂总是写进工程目录，所以正常情况下
     *  两者相同、结果和原厂一致；只有 -o 指到别处时才有区别。 */
    QString outDir;
    QString enameIn;                ///< 可选：复用这个 ename.h 里的 id（对拍用）
    QString excelPath;              ///< 写进 Resbuilder.xml 的 excel_path
    quint32 language = 0x13;        ///< 语言掩码
    QString panelType = QStringLiteral("LCDPANEL");
    quint32 uiVersion = 0;          ///< 0 = 自动按内容算

    /* ---- 「功能设置」那一页，最终都落进 Resbuilder.xml ----
     * 以前这些全写死在生成代码里，界面上没有出口。默认值就是原来写死的那套，
     * 不动设置时产出一字节不变。
     * 字体那几个数组为空时按老规矩全填「宋体 -16 常规」。 */
    QString      endian = QStringLiteral("LITTLEENDIAN");
    QString      picturePath;                          ///< 空 -> 写 NULL
    QString      res = QStringLiteral("result");       ///< 带出 resfilename/headerfilename
    quint32      bmpTransparentColor = 0x00FFFFFFu;
    quint32      pngBackgroundColor = 0x00000000u;
    QString      paletteType = QStringLiteral("rgb");
    QString      imageCompress = QStringLiteral("none");
    QString      stringCompress = QStringLiteral("none");
    QStringList  langKeys;          ///< 22 项 LANG；空 = 用内置表
    QStringList  langNames;         ///< 与 langKeys 等长
    QStringList  fontFaces;         ///< 22 项 lfFaceName
    QVector<int> fontHeights;       ///< 22 项 lfHeight（负像素）
    QVector<int> fontWeights;       ///< 22 项 lfWeight
    QVector<int> fontItalics;       ///< 22 项 lfItalic
    QVector<int> fontUnderlines;    ///< 22 项 lfUnderline
    QVector<int> fontStrikeOuts;    ///< 22 项 lfStrikeOut
};

struct Output {
    bool        ok = false;
    QString     error;
    QStringList warnings;
    QByteArray  sty;
    QByteArray  enameH;
    QByteArray  resbuilderXml;
    QByteArray  debugTxt;
};

class Builder
{
public:
    bool loadProject(const QString &jsonPath, QString *error);
    bool loadOptionIni(const QString &path, QString *error);

    Output build(const Options &opt);

private:
    struct Node;
    QJsonObject m_doc;
    QMap<QString, int> m_typeCode;      ///< option.ini [Control]
    QString m_jsonPath;
};

} // namespace sty

#endif // STYBUILDER_H
