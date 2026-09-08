/*
 * EditorOps.h —— 编辑器的"操作与限制"集中处
 *
 * 【为什么单独一个文件】原厂 ui-tools.exe 的编辑限制（谁能挂到谁下面、
 * 剪贴板能往哪儿贴、删除要不要确认）散在 CompoentControls / BaseForm /
 * TreeDock 三处，但规则是同一套。重建版把规则收在这里，三处调用同一份，
 * 免得三处各写一遍再各错一遍。
 *
 * 【规则的出处】不是猜的，是 ui-tools.exe 里那几条提示语反推出来的
 * （re 目录下的字符串扫描，见 docs/FACTORY_UI.md）：
 *     "请选择一个布局或者新建一个并选中它."          -> 建控件要先选中布局
 *     "请选择一个图层或者新建一个图层,并选中它."      -> 建布局要先选中图层
 *     "当前类型容器不接受粘贴!"                       -> 只有布局收粘贴
 *     "剪切板里的对像支持粘贴到当前容器上,请选择一个<布局>对像."
 *     "当前的选中的对像不支持剪切板里的对像粘贴,请选择一个<布局>对像."
 *     "你真的要删除当前%1吗?删除之后不可以撤消,请选择<删除>删除."
 *     "宽高不能设置为零."
 * 提示语原文照抄（含原厂的标点和"对像"这个写法），这样用户从原厂换过来
 * 看到的东西是一样的。
 */
#ifndef EDITOROPS_H
#define EDITOROPS_H

#include <QString>

class QWidget;
class UiNode;

namespace EditorOps {

/* ---- 拖拽 ------------------------------------------------------------
 * 手册里建控件的**主要手势**是拖："点击图层并拖动到绘制面板上新建一个图层"、
 * "点击并拖动 slider 控件到布局上"。DragButton 打包的就是这个 MIME 类型，
 * 载荷是 "<-class>|<-type>"。 */
const char *const kControlMime = "application/x-uitools-control";

/* ---- 层级判定 --------------------------------------------------------
 * 判的是 "-class" 而不是 "-type"：control/ex/ 里的扩展控件（slider）
 * -type 也叫 NewLayout，按 type 判会把它错当容器。 */
bool isPage(const UiNode *n);
bool isLayer(const UiNode *n);
bool isLayout(const UiNode *n);

/** 能不能往它下面挂控件 / 收粘贴。原厂只认布局。 */
bool acceptsChild(const UiNode *n);

/**
 * 往 parent 下面挂孩子时该用哪个 json 键。
 *
 * 【别想当然写 "widget"】原厂 SmallColorTFT.json 里根本没有 widget 这个键，
 * 全工程只用三个（277 个节点统计出来的实际组合）：
 *     ScenesScreen --layer--------> NewLayer
 *     NewLayer     --layout-------> NewLayout
 *     NewLayout    --layout-------> NewFrame / NewLayout / NewList
 *     NewList      --listwidget---> NewLayout   （列表的行模板）
 * 键写错了，文件本身还是合法 json，但下游 QtToolBin 遍历不到那棵子树，
 * 表现是"编辑器里有这个控件，生成出来的 .sty 里没有"。
 */
QString childKeyFor(const UiNode *parent);

/* ---- 无人值守模式 ----------------------------------------------------
 * 模态框在没人点的环境里会把进程挂死，自测就永远跑不完。开了这个开关之后
 * 提示框改成"记下来"，确认框一律当作"确认"，输入框直接取默认值。
 * 只有 --ops-test 会打开它，界面正常跑的时候永远是关的。 */
void    setSilent(bool on);
bool    silent();
/** 无人值守模式下最后一条提示语，测试用它断言"该拦的确实拦了"。 */
QString lastMessage();
void    clearLastMessage();

/* ---- 提示 ------------------------------------------------------------ */
/** 标题「提示」的普通提示框。 */
void tip(QWidget *parent, const QString &text);
/** 标题「警告」。 */
void warn(QWidget *parent, const QString &text);
/**
 * 原厂的删除确认框：标题「删除提示」，按钮是 <删除> / 取消，
 * 不是 Qt 默认的 Yes/No —— 正文里那句"请选择<删除>删除"就是在说这个按钮。
 * @param what 填进 "你真的要删除当前%1吗" 的那个词，如 "布局" / "页面"。
 */
bool confirmDelete(QWidget *parent, const QString &what);

/* ---- Z 序 ------------------------------------------------------------
 * 画布上的叠放次序 = 节点在 parent->children 里的次序（后建的盖在上面，
 * 见 ScenesScreen::buildRecursive），所以挪层就是挪数组下标。 */
enum ZMove { ZTop, ZUp, ZDown, ZBottom };
bool moveZ(UiNode *n, ZMove how);

/* ---- 剪贴板（进程内，存 json）----------------------------------------
 * 存 json 而不是存指针：源节点随时可能被删掉，存指针就是悬空。 */
void    copyToClip(const UiNode *n);
bool    clipEmpty();
/** 把剪贴板内容克隆一份挂到 parent 下。parent 不收就返回 nullptr。 */
UiNode *pasteInto(UiNode *parent);

/** 在 parent 的孩子里取一个不重名的名字。 */
QString uniqueName(const UiNode *parent, const QString &base);

/* ---- 自定义控件目录 ---------------------------------------------------
 * 原厂[全局设置]里那一项："自定义的模版控件目录,默认是 widgets 目录"。
 * 重建版默认落到 <UITools>/control/ex/ —— ControlLibrary 扫的就是它，
 * 存进去下次启动就能在"自定义控件"那一组里看到。 */
void    setCustomWidgetDir(const QString &dir);
QString customWidgetDir();

} // namespace EditorOps

#endif // EDITOROPS_H
