/*
 * ResbuilderOptions.h —— 「功能设置」（又叫「配置界面」）里的那一整页设置。
 *
 * 这些值最终全都落进 Resbuilder.xml，也就是 ResBuilder.exe 的唯一输入描述文件。
 * 以前 StyBuilder 生成那份 xml 时除了 paneltype / excel_path / language / rotate
 * 之外全是硬编码，界面上也就没有出口 —— 想换资源文件名、换透明色、换压缩方式、
 * 改某种语言的字体，只能去改代码。
 *
 * 【存在哪：工程目录的 Resbuilder.xml】
 * 一开始存到了 <工具目录>/config/ini/resbuilder.ini，那是错的：工程目录里那份
 * Resbuilder.xml（LCDPANEL / 选中 13 / 简体中文 Cambria 24 / 德语 宋体 12 /
 * excel 是相对路径 ../../../UITools/多国语言_128_64.xls）和界面上显示的逐项
 * 吻合，而工具目录里的 Resbuilder.xml / Resbuilder.dat 是另一套
 * （TFTPANEL / 0x7 / twsbox）。这一页是**跟着工程走**的 ——
 * 两个工程各有各的设置，正合常理。
 *
 * 所以：开工程时从 <工程目录>/Resbuilder.xml 读；生成时再写回同一个文件
 * （那本来就是每次生成都会重写的产物）。不再另外存 ini。
 *
 * 【字号和 lfHeight 的关系】界面上填的是**磅值**，xml 里的 lfHeight 是 96 DPI 下
 * 的**负像素**：px = pt * 96 / 72 = pt * 4/3。24pt -> -32，12pt -> -16。
 */
#ifndef RESBUILDEROPTIONS_H
#define RESBUILDEROPTIONS_H

#include <QString>
#include <QVector>

namespace toolbin {

/** 「配置语言」表里的一行。顺序即 Resbuilder.xml 里 font00..font21 的顺序。 */
struct LangRow {
    QString key;                            ///< LANG 属性，如 "Chinese_Simplified"
    QString name;                           ///< 中文显示名，如 "简体中文"
    QString face;                           ///< 字体名（lfFaceName）
    int     point = 12;                     ///< 字号（磅）
    bool    italic = false;
    bool    bold = false;                   ///< 对应 lfWeight 700 / 400
    bool    underline = false;
    /** Select Font 弹窗的 Effects 里有 Strikeout，对应 lfStrikeOut。
     *  表格上没有这一列，但值要存住、要写进 xml。 */
    bool    strikeOut = false;

    /** 磅值 -> lfHeight（96 DPI 下的负像素）。 */
    int lfHeight() const { return -(point * 4 / 3); }
    int lfWeight() const { return bold ? 700 : 400; }
};

struct ResbuilderOptions {
    QVector<LangRow> langs;                 ///< 固定 22 行
    quint32 languageMask = 0x13;            ///< 界面上那个"选中: 13"，bit i = 第 i 行启用
    QString picturePath;                    ///< 空 = xml 里写 NULL
    QString res = QStringLiteral("result"); ///< 资源文件名（不带扩展名）
    quint32 bmpTransparentColor = 0x00FFFFFFu;
    quint32 pngBackgroundColor = 0x00000000u;
    QString excelPath;
    QString panelType = QStringLiteral("LCDPANEL");     ///< LCDPANEL / TFTPANEL
    QString endian = QStringLiteral("LITTLEENDIAN");    ///< LITTLEENDIAN / BIGENDIAN
    QString paletteType = QStringLiteral("rgb");        ///< rgb / yuv
    QString imageCompress = QStringLiteral("none");
    QString stringCompress = QStringLiteral("none");
    /** 「其他配置」里那个"旋转"勾选框。实际的旋转角度在主界面上那个下拉框里，
     *  这个勾选框到底还管什么没有确认，所以只存不用（和主界面"芯片平台"一样）。 */
    bool rotateFlag = true;

    /** 默认那套值：font00 Cambria 24pt，01..05 宋体 24pt，
     *  06..21 宋体 12pt。不动设置时产出与既有资源一致。 */
    static ResbuilderOptions defaults();

    /**
     * 从工程目录的 Resbuilder.xml 读这一页设置。
     * 文件不存在/读不出来就保持默认值并返回 false。
     * @param xmlPath <工程目录>/Resbuilder.xml
     */
    bool loadFromProject(const QString &xmlPath);

    /**
     * 把这一页设置灌进 sty::Options —— 生成 Resbuilder.xml 时按它写。
     * 【模板函数是为了不让 ResbuilderOptions 依赖 StyBuilder.h】这两边分属
     * 界面层和生成层，互相 include 会绕成一圈；调用点自己带类型进来即可。
     */
    template <typename Opt>
    void applyTo(Opt &o) const
    {
        o.endian = endian;
        o.panelType = panelType;
        o.picturePath = picturePath;
        o.excelPath = excelPath;
        o.language = languageMask;
        o.res = res;
        o.bmpTransparentColor = bmpTransparentColor;
        o.pngBackgroundColor = pngBackgroundColor;
        o.paletteType = paletteType;
        o.imageCompress = imageCompress;
        o.stringCompress = stringCompress;
        o.langKeys.clear();
        o.langNames.clear();
        o.fontFaces.clear();
        o.fontHeights.clear();
        o.fontWeights.clear();
        o.fontItalics.clear();
        o.fontUnderlines.clear();
        o.fontStrikeOuts.clear();
        for (const LangRow &r : langs) {
            o.langKeys.append(r.key);
            o.langNames.append(r.name);
            o.fontFaces.append(r.face);
            o.fontHeights.append(r.lfHeight());
            o.fontWeights.append(r.lfWeight());
            o.fontItalics.append(r.italic ? 1 : 0);
            o.fontUnderlines.append(r.underline ? 1 : 0);
            o.fontStrikeOuts.append(r.strikeOut ? 1 : 0);
        }
    }
};

} // namespace toolbin

#endif // RESBUILDEROPTIONS_H
