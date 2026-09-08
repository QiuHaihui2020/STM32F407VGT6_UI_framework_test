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
    QString enameIn;                ///< 可选：复用这个 ename.h 里的 id（对拍用）
    QString excelPath;              ///< 写进 Resbuilder.xml 的 excel_path
    quint32 language = 0x13;        ///< 语言掩码
    QString panelType = QStringLiteral("LCDPANEL");
    quint32 uiVersion = 0;          ///< 0 = 自动按内容算
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
