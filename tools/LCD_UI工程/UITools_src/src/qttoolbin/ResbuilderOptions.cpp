#include "ResbuilderOptions.h"

#include <QDir>
#include <QFileInfo>
#include "ResConfig.h"

namespace toolbin {

namespace {

/* 语言表和 StyBuilder 里那份是同一套（Resbuilder.xml 的 LanguageList 就按它写），
 * 那边是 static const，跨编译单元拿不到，这里照抄一份。两处要一起改。 */
const char *const kLangKeys[22] = {
    "Chinese_Simplified", "Chinese_Traditional", "Japanese", "Korean",
    "English", "French", "German", "Italian",
    "Dutch", "Portuguese", "Spanish", "Swedish",
    "Czech", "Danish", "Polish", "Russian",
    "Turkey", "Hebrew", "Thai", "Hungarian",
    "Romanian", "Arabic",
};
const char *const kLangNames[22] = {
    "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87",     // 简体中文
    "\xe7\xb9\x81\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87",     // 繁体中文
    "\xe6\x97\xa5\xe8\xaf\xad",                             // 日语
    "\xe9\x9f\xa9\xe8\xaf\xad",                             // 韩语
    "\xe8\x8b\xb1\xe8\xaf\xad",                             // 英语
    "\xe6\xb3\x95\xe8\xaf\xad",                             // 法语
    "\xe5\xbe\xb7\xe8\xaf\xad",                             // 德语
    "\xe6\x84\x8f\xe5\xa4\xa7\xe5\x88\xa9\xe8\xaf\xad",     // 意大利语
    "\xe8\x8d\xb7\xe5\x85\xb0\xe8\xaf\xad",                 // 荷兰语
    "\xe8\x91\xa1\xe8\x90\x84\xe7\x89\x99\xe8\xaf\xad",     // 葡萄牙语
    "\xe8\xa5\xbf\xe7\x8f\xad\xe7\x89\x99\xe8\xaf\xad",     // 西班牙语
    "\xe7\x91\x9e\xe5\x85\xb8\xe8\xaf\xad",                 // 瑞典语
    "\xe6\x8d\xb7\xe5\x85\x8b\xe8\xaf\xad",                 // 捷克语
    "\xe4\xb8\xb9\xe9\xba\xa6\xe8\xaf\xad",                 // 丹麦语
    "\xe6\xb3\xa2\xe5\x85\xb0\xe8\xaf\xad",                 // 波兰语
    "\xe4\xbf\x84\xe5\x9b\xbd\xe8\xaf\xad",                 // 俄国语
    "\xe5\x9c\x9f\xe8\x80\xb3\xe5\x85\xb6\xe8\xaf\xad",     // 土耳其语
    "\xe5\xb8\x8c\xe4\xbc\xaf\xe6\x9d\xa5\xe8\xaf\xad",     // 希伯来语
    "\xe6\xb3\xb0\xe8\xaf\xad",                             // 泰语
    "\xe5\x8c\x88\xe7\x89\x99\xe5\x88\xa9\xe8\xaf\xad",     // 匈牙利语
    "\xe7\xbd\x97\xe9\xa9\xac\xe5\xb0\xbc\xe4\xba\x9a\xe8\xaf\xad", // 罗马尼亚语
    "\xe9\x98\xbf\xe6\x8b\x89\xe4\xbc\xaf\xe8\xaf\xad",     // 阿拉伯语
};

const char *const kSongTi = "\xe5\xae\x8b\xe4\xbd\x93";     // 宋体

} // namespace

ResbuilderOptions ResbuilderOptions::defaults()
{
    ResbuilderOptions o;
    o.langs.reserve(22);
    for (int i = 0; i < 22; ++i) {
        LangRow r;
        r.key = QString::fromUtf8(kLangKeys[i]);
        r.name = QString::fromUtf8(kLangNames[i]);
        /* 原厂 Resbuilder.xml：font00 是 Cambria、01..05 宋体，六个都是
         * lfHeight=-32（24pt）；font06..21 宋体 lfHeight=-16（12pt）。 */
        r.face = (i == 0) ? QStringLiteral("Cambria") : QString::fromUtf8(kSongTi);
        r.point = (i <= 5) ? 24 : 12;
        o.langs.append(r);
    }
    return o;
}

bool ResbuilderOptions::loadFromProject(const QString &xmlPath)
{
    *this = defaults();
    if (!QFileInfo::exists(xmlPath)) {
        return false;
    }
    /* 复用读 Resbuilder.xml 的那套解析（ResBuilder 自己也用它），
     * 免得同一份格式在两处各解析一遍、以后改一处漏一处。 */
    res::ResConfig c;
    if (!c.load(xmlPath, nullptr)) {
        return false;
    }

    languageMask = c.language;
    picturePath = (c.picturePath.compare(QLatin1String("NULL"), Qt::CaseInsensitive) == 0)
                  ? QString() : c.picturePath;
    res = c.res;
    bmpTransparentColor = c.bmpTransparentColor;
    pngBackgroundColor = c.pngBackgroundColor;
    excelPath = c.excelPath;          // 原厂存的是相对工程目录的路径，原样带着
    panelType = c.panelType;
    endian = c.endian;
    paletteType = c.paletteType;
    imageCompress = c.imageCompress;
    stringCompress = c.stringCompress;

    /* 语言行：名字来自 LanguageList，字体来自 Fonts，两张表按下标一一对应 */
    for (int i = 0; i < langs.size(); ++i) {
        LangRow &r = langs[i];
        if (i < c.langKeys.size()) {
            r.key = c.langKeys.at(i);
        }
        if (i < c.langNames.size()) {
            r.name = c.langNames.at(i);
        }
        if (i < c.fonts.size()) {
            const res::LogFontSpec &f = c.fonts.at(i);
            r.face = f.faceName;
            /* lfHeight 是 96 DPI 下的负像素，界面上填的是磅：pt = -h * 3 / 4 */
            r.point = qMax(1, (-f.height) * 3 / 4);
            r.italic = (f.italic != 0);
            r.bold = (f.weight >= 700);
            r.underline = (f.underline != 0);
            r.strikeOut = (f.strikeOut != 0);
        }
    }
    return true;
}

} // namespace toolbin
