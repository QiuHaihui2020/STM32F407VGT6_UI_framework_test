/*
 * Preview.h —— 画布上的**内容**渲染
 *
 * 【为什么单独一块】在这之前画布只画背景色和边框，图片、文字、数字一个都
 * 没渲染 —— 是"布局预览"，不是"内容预览"。原厂是有内容预览的。
 *
 * 【怎么保证画的和屏上一致】不自己另发明一套画法，直接复用生成资源那条链：
 *   图片  ResBuilder 的判定是"非透明色即点亮"，透明色来自 Resbuilder.xml 的
 *         <bmp_transparent_color>（本工程 0x00FFFFFF，即白色透明）。
 *         这里同一套判定，所以画布上亮的点就是 .res 里置 1 的点。
 *   文字  用 res::rasterize()，就是生成 result.str 用的那个 GDI 光栅化器 ——
 *         它的输出和原厂 result.str 逐字节相同（141/141，见 re/verify_str.py）。
 *         字体取 Resbuilder.xml <Fonts> 里对应语言的 LOGFONT。
 *   数字/时间  按控件的 format 拼数字图片列表里的位图，和固件的做法一致。
 *
 * 【单色屏的"点亮"画成什么颜色】屏是单色的，点亮就是亮。画布上用控件自己的
 * "文字颜色"（工程里普遍是 #ffffffff），没有就用白色 —— 图层背景是蓝/绿/紫，
 * 白色压上去看得清。未点亮的像素画成透明，露出底下的背景。
 *
 * 所有结果都按 (路径|颜色) / (文字|字体|颜色) 缓存 —— paintEvent 调得很频，
 * 每次重新解 BMP、重新走一遍 GDI 会卡死。
 */
#ifndef PREVIEW_H
#define PREVIEW_H

#include <QPixmap>
#include <QString>
#include <QStringList>

class UiNode;

namespace Preview {

/**
 * 工程换了就要重来一遍：图片路径是相对工程目录的，字体在工程的
 * Resbuilder.xml 里，文字表在多国语言 xls 里。
 * @param projectDir 工程 json 所在目录
 * @param excelPath  多国语言 xls；空则文字画不出来（退化成画 ResID）
 */
void setProject(const QString &projectDir, const QString &excelPath);
/** 清掉所有缓存（工程重载、资源改动后调）。 */
void invalidate();

/** ResID（"m1"）-> 该语言的文字。查不到返回空。 */
QString stringOf(const QString &resId, int langIndex = 0);

/**
 * 把一个节点的"内容"画成位图，画布按控件矩形贴上去。
 * 画不出来（没有可画的内容、图片缺失…）就返回空 QPixmap，调用方照旧只画框。
 * @param lit 点亮像素用的颜色
 */
QPixmap contentOf(UiNode *n, const QColor &lit);

/* ---- 单色屏语义 --------------------------------------------------------
 * 这是**点阵屏**工具，屏上只有"亮/灭"，没有颜色。工程 json 里那一堆
 * #D9EE94 / #368FEE 之类的背景色，在设备上并不会显示成绿色蓝色 ——
 * 固件（User/ui_framework/lcd_drive/middle/ui_synthesis_oled.c）在
 * DC_DATA_FORMAT_MONO 下只认三个魔数：
 *
 *     #define BGC_MONO_SET  0x555aaa   背景：只有它填充，其余一律清除
 *     #define TEXT_MONO_CLR 0x555aaa   文字：等于它就不显示
 *     #define TEXT_MONO_INV 0xaaa555   文字：反显（整块点亮，字挖空）
 *     #define RECT_MONO_CLR 0x555aaa   边框：等于它就不画（判断方向和背景相反）
 *
 *     jlui_fill_rect : color == BGC_MONO_SET ? 0xffff : 0x55aa
 *     jlui_draw_rect : color != RECT_MONO_CLR ? (color ?: 0xffff) : 0x55aa
 *     jlui_draw_text : color == TEXT_MONO_INV -> 先 fill 整块再把字画成灭
 *
 * 比较是在 **RGB565** 上做的（UI_RGB565 先降位），所以这里也降到 565 再比，
 * 免得 24 位里几个相近的值判错。
 *
 * 实测这套工程里 #ff555aaa 用了 8 次 —— 设计师确实在用这个魔数表示"填充"。
 */
enum class MonoFill { None, Set };
enum class MonoText { Normal, Invert, Hidden };

/** 背景色字符串 -> 填不填。 */
MonoFill fillOf(const QString &cssColor);
/** 文字色字符串 -> 正常/反显/不显示。 */
MonoText textModeOf(const QString &cssColor);
/** 边框色字符串 -> 画不画。 */
bool borderVisible(const QString &cssColor);

/**
 * 节点所在的图层是不是单色（OSD1）。
 *
 * 图层有个 color_format 属性，枚举是 {OSD16:2, OSD1:4}，手册 2.3：
 * "若为点阵屏，颜色类型选择 OSD1，若为彩屏，选择 OSD16"。
 * 取不到时按 OSD1 算 —— 这套 SDK 是 128x64 点阵屏，保守选项是"当成单色"，
 * 免得在单色工程里放出一堆没意义的取色器。
 */
bool isMonoLayer(const UiNode *n);

/* 固件认的三个魔数，写属性时用这几个常量，别在别处再抄一遍字面量 */
extern const char *const kMonoFillOn;    ///< "#ff555aaa" 背景填充 / 文字不显示 / 边框不画
extern const char *const kMonoInvert;    ///< "#ffaaa555" 文字反显
extern const char *const kMonoLit;       ///< "#ffffffff" 普通点亮（工程里 254 处在用）

/** 供自测用：当前缓存里有多少张图。 */
int cacheCount();

} // namespace Preview

#endif // PREVIEW_H
