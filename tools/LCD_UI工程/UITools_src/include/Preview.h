/*
 * Preview.h —— 画布上的**内容**渲染
 *
 * 【为什么单独一块】在这之前画布只画背景色和边框，图片、文字、数字一个都
 * 没渲染 —— 那是"布局预览"，不是"内容预览"。
 *
 * 【怎么保证画的和屏上一致】不自己另发明一套画法，直接复用生成资源那条链：
 *   图片  ResBuilder 的判定是"非透明色即点亮"，透明色来自 Resbuilder.xml 的
 *         <bmp_transparent_color>（本工程 0x00FFFFFF，即白色透明）。
 *         这里同一套判定，所以画布上亮的点就是 .res 里置 1 的点。
 *   文字  用 res::rasterize()，就是生成 result.str 用的那个 GDI 光栅化器 ——
 *         它的输出和既有 result.str 逐字节相同（141/141，见 compat/verify_str.py）。
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

#include <QColor>
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

/* ---- 点阵屏预览配色 -----------------------------------------------------
 * 屏是单色的，但不同的点阵屏"亮"和"灭"呈现的颜色差很多：OLED 是黑底白字，
 * STN 是黄绿底黑字，蓝屏 LCD 是蓝底白字……预览想接近真机就得能配。
 *
 * 【只影响预览】这两个颜色**不进任何资源文件**。资源里只有"亮/灭"一个 bit，
 * 走的还是 Resbuilder.xml 的 <bmp_transparent_color> 那套判定，和这里无关。
 * 存在[全局设置]的 ui-config 里（Preview/LitColor、Preview/DarkColor）。 */

/** 像素点亮时画成什么颜色（默认白）。 */
QColor monoLit();
/** 像素熄灭时画成什么颜色，也是页面的底色（默认近黑）。 */
QColor monoDark();
/** 像素网格线的颜色（[全局设置] -> 点阵屏预览 -> 网格颜色）。 */
QColor monoGrid();
/** 从[全局设置]重新读一遍这两个颜色。改完设置要调。 */
void reloadMonoColors();

/** ResID（"m1"）-> 该语言的文字。查不到返回空。 */
QString stringOf(const QString &resId, int langIndex = 0);

/**
 * 按工程里存的图片路径（相对工程目录，如 "config/pic_lcd/v_block.bmp"）
 * 取它的单色位图。给 css 的"背景图片"用 —— 那也是屏上真会画出来的东西。
 * 路径为空、文件不在都返回空 QPixmap。
 */
QPixmap pictureOf(const QString &path, const QColor &lit);

/**
 * 把一个节点的"内容"画成位图，画布按控件矩形贴上去。
 * 画不出来（没有可画的内容、图片缺失…）就返回空 QPixmap，调用方照旧只画框。
 * @param lit 点亮像素用的颜色
 */
QPixmap contentOf(UiNode *n, const QColor &lit);

/* ---- 单色屏语义 --------------------------------------------------------
 * 这是**点阵屏**工具，屏上只有"亮/灭"，没有颜色。工程 json 里那一堆
 * #D9EE94 / #368FEE 之类的背景色，在设备上并不会显示成绿色蓝色 ——
 * 固件在
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

/* ---- text / ascii 的"预览文字" ----------------------------------------
 * 这两种编码格式的内容是**业务层运行时写进去的**（ui_text_set_text_by_id
 * 那几个接口），资源里没有，工具不可能知道会填什么，所以画布上只能是空的。
 * 但排版的时候看不到字就很难判断"这个框够不够宽、对齐对不对"。
 *
 * 于是给这类控件配一句**只用于预览**的假文字。
 *
 * 【绝对不能写进工程文件】写进去就改动了工程文件 —— 读写要逐字节相同
 * 是这套工具的硬指标（见 --json-roundtrip）。所以存在工具自己的配置里
 * （GlobalSettings，按 "工程文件名 + 控件标识" 做键），工程目录、资源目录
 * 一个字节都不碰，也不会进资源。
 */

/** 控件在预设表里的键：优先用 ID号（ename），没有就用它在树里的路径。 */
QString previewKey(const UiNode *n);
/** 读预设文字；没配返回空。 */
QString presetText(const UiNode *n);
/** 写预设文字（空串 = 删掉这条）。只落到工具配置里。 */
void setPresetText(const UiNode *n, const QString &text);
/** 这个控件该不该给"预览文字"这一栏：只有 text / ascii 需要。 */
bool needsPresetText(const UiNode *n);

/** 供自测用：当前缓存里有多少张图。 */
int cacheCount();

} // namespace Preview

#endif // PREVIEW_H
