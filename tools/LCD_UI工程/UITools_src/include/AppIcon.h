/*
 * AppIcon.h —— 界面图标的统一取用口
 *
 * 【为什么要过一层】图标是画死在 png 里的，主色是深蓝灰。浅色主题下没问题，
 * 换成深色主题，深色的墨压在深色工具栏上就糊成一片，什么都看不见。
 * 所以取图标一律走这里：深色主题下把"近中性的深色"像素换成浅墨，
 * 蓝/绿/红那些强调色不动 —— 它们在两种底色上都够醒目。
 *
 * 应用图标(app.png)和关于框那块屏(logo.png)不参与换色：它们本身就是
 * 带底色的完整图形，换了反而不对。
 */
#ifndef APPICON_H
#define APPICON_H

#include <QIcon>
#include <QString>

class QAction;
class QAbstractButton;
class QWidget;

namespace AppIcon {

/** 当前是不是深色主题。启动时由 main() 按 [全局设置] 定。 */
bool isDark();

/**
 * 切主题。会清掉图标缓存 —— 已经设出去的图标不会自己变，
 * 要让它们跟着换，切完再调一次 refresh()。
 */
void setDark(bool dark);

/**
 * 取一个图标。@param file 形如 "row-add.png"（不带 ":/icons/" 前缀）。
 * 结果按 (文件, 主题) 缓存，重复取不会反复解码和换色。
 */
QIcon get(const QString &file);

/**
 * 取图标并把名字记在对象上，这样主题一换 refresh() 能把它换掉。
 * 长期存在的东西（工具栏按钮）用这个；对话框里的按钮每次都新建，
 * 直接用 get() 就够了。
 */
void apply(QAction *act, const QString &file);
void apply(QAbstractButton *btn, const QString &file);

/** 把 root 下所有 apply() 过的图标按当前主题重设一遍。 */
void refresh(QWidget *root);

} // namespace AppIcon

#endif // APPICON_H
