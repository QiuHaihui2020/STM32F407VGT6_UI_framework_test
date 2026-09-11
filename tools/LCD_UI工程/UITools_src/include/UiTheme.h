/*
 * UiTheme.h —— 界面配色
 *
 * 【只让人调两样】整份样式表要用到十几种色（页签底、滚动条槽、禁用态、
 * 悬停态…），全摆出来让人一个个配，既配不好也没人愿意配 —— 改个颜色要动
 * 七八处，谁都不想弄第二次。
 * 所以界面上只有三项：**方案**、**底色**、**强调色**。挑现成方案时底色那行
 * 是灰的（方案自带一整套）；选「自定义」才放开，让你自己定底色 —— 那一套
 * 由 derive() 从底色 + 强调色两个颜色推出来。
 * 其余十几种色一律派生（见 qss() 里那一串 mix）。换任意一样，整套跟着
 * 协调地动，不会出现"主色改了但按钮按下去还是旧蓝"。
 *
 * 【深浅是算出来的，不是另设一个开关】面板底色的明度低于一半就当深色主题 ——
 * 图标要不要把主色墨换成浅色，看的就是这个。这样"自定义一套暗色"也能自动
 * 拿到正确的图标，不需要用户再去勾一个"这是深色主题"。
 */
#ifndef UITHEME_H
#define UITHEME_H

#include <QColor>
#include <QString>
#include <QVector>

namespace UiTheme {

/** 用户能直接调的那 7 个颜色。其余都从这几个派生。 */
struct Palette {
    QColor win;       ///< 窗口底
    QColor panel;     ///< 停靠面板底
    QColor field;     ///< 输入区 / 树 / 列表的底
    QColor border;    ///< 边框
    QColor canvas;    ///< 画布宿主底（衬那块 128x64 的屏）
    QColor text;      ///< 正文字色
    QColor accent;    ///< 强调色：选中、当前页签、聚焦边、悬停

    /** 深色主题？看面板底色的明度。 */
    bool isDark() const { return panel.lightness() < 128; }
};

/** 一个预设方案。 */
struct Preset {
    QString name;
    Palette pal;
};

/** 内置方案表。界面上的「方案」下拉就是照它铺的。 */
const QVector<Preset> &presets();

/** 按名字取方案；没有这个名字就返回第一个。 */
Palette presetByName(const QString &name);

/**
 * 从两个颜色推一整套。
 *
 * @param base   面板底色。**深浅由它定** —— 明度低于一半就按深色主题推，
 *               输入区、画布会比它更深；浅色主题下输入区取纯白。
 * @param accent 强调色。
 *
 * 「自定义」方案走的就是这条路：只让人管两个色，剩下的窗口底、输入区、
 * 边框、文字色全按 base 的深浅算，配出来不会打架。
 */
Palette derive(const QColor &base, const QColor &accent);

/**
 * 从 [全局设置] 读出当前配色：方案给一整套底色，强调色单独覆盖。
 * 没配过就用第一个预设。
 */
Palette current();

/**
 * 存回 [全局设置]。
 * @param presetName 方案名；空串表示「自定义」，那时 base 才有意义。
 */
void save(const QString &presetName, const QColor &base, const QColor &accent);

/** 生成整份 Qt 样式表。 */
QString qss(const Palette &p);

} // namespace UiTheme

#endif // UITHEME_H
