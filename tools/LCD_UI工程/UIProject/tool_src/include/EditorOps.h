/*
 * EditorOps.h —— 编辑器的"操作与限制"集中处
 *
 * 【为什么单独一个文件】编辑限制（谁能挂到谁下面、剪贴板能往哪儿贴、
 * 删除要不要确认）会被 CompoentControls / BaseForm / TreeDock 三处用到，
 * 但规则是同一套。收在这里三处调同一份，免得各写一遍再各错一遍。
 *
 * 【提示语】沿用用户已经熟悉的说法（包括"对像"这种写法），换工具不用重新适应：
 *     "请选择一个布局或者新建一个并选中它."          -> 建控件要先选中布局
 *     "请选择一个图层或者新建一个图层,并选中它."      -> 建布局要先选中图层
 *     "当前类型容器不接受粘贴!"                       -> 只有布局收粘贴
 *     "剪切板里的对像支持粘贴到当前容器上,请选择一个<布局>对像."
 *     "当前的选中的对像不支持剪切板里的对像粘贴,请选择一个<布局>对像."
 *     "你真的要删除当前%1吗?删除之后不可以撤消,请选择<删除>删除."
 *     "宽高不能设置为零."
 */
#ifndef EDITOROPS_H
#define EDITOROPS_H

#include <QJsonObject>
#include <QRect>
#include <QString>

class QWidget;
class UiNode;
class ProjectModel;
class ControlLibrary;
class QJsonObject;

namespace EditorOps {

/* ---- 拖拽 ------------------------------------------------------------
 * 手册里建控件的**主要手势**是拖："点击图层并拖动到绘制面板上新建一个图层"、
 * "点击并拖动 slider 控件到布局上"。DragButton 打包的就是这个 MIME 类型，
 * 载荷是 "<-class>|<-type>"。 */
const char *const kControlMime = "application/x-uitools-control";

/* ---- 层级判定 --------------------------------------------------------
 * 判的是 "-class" 而不是 "-type"：自定义控件里的扩展控件（slider）
 * -type 也叫 NewLayout，按 type 判会把它错当容器。 */
bool isPage(const UiNode *n);
bool isLayer(const UiNode *n);
bool isLayout(const UiNode *n);
bool isFrame(const UiNode *n);      ///< NewFrame：文字/图片/时间/数字/电池，叶子
bool isList(const UiNode *n);       ///< NewList：垂直列表 / 水平列表
bool isGrid(const UiNode *n);       ///< NewGrid：表格控件

/**
 * 是不是"顶层互斥布局" —— 直接挂在图层下的那一层布局。
 *
 * 一页里通常有好几个这样的整屏布局（主界面 / 音量 / EQ / 菜单…），运行时
 * **只显示一个**，靠 element_css.invisible 区分。编辑器里"默认画哪一个"、
 * "选中即隔离要隔离谁"都是按这一层算的。
 *
 * 【只在这一层看 invisible】再往下的控件也可以标"默认隐藏"，但那是给固件
 * 运行时用的位（程序自己决定什么时候把它显出来），不是编辑器的可见性开关。
 * 画布上看不看得见，只由对象树那只眼睛管。
 */
bool isTopScreen(const UiNode *n);

/* ---- 谁能装什么 ------------------------------------------------------
 * 来自既有两个工程 277 个节点的全量统计（一个反例都没有）：
 *
 *   装「布局」的：图层(64)、布局(3，套娃)、**列表(165)**
 *   装「控件」的：只有布局(494)
 *
 * 列表那 165 个就是它的行/项模板 —— 列表的孩子**清一色是 NewLayout**，
 * 一个 NewFrame 都没有。所以控件不能直接塞进列表，得先有行布局。
 * 这不是洁癖：固件那边按"列表的孩子是行"来遍历，塞个裸控件进去，
 * 生成出来的 .sty 就是个从没出现过的形状，固件读不对。
 *
 * 【表格 NewGrid 和列表同等对待】工程里没有表格样本，但三件事都对得上：
 *   · option.ini 里 NewGrid / VerticalList / HorizontalList **同为类型码 5**，
 *     StyBuilder 走的是同一条路；
 *   · 固件 ui_grid.h 的 `struct ui_grid_item_info` 里，每一项就是
 *     `struct layout_info *info` —— 和列表的行是同一种东西；
 *   · 水平列表的 -class 也是 NewList（-type 才是 HorizontalList），
 *     所以列表那两种早就是同一套规则，表格没道理单独一套。
 *
 * 【水平列表不用单独处理】它的 -class 是 NewList，isList() 天然覆盖。
 * 【slider / vslider 这些扩展控件也不用】它们的 -class 就是
 *   NewLayout，本来就按布局收孩子（工程里的 MUSIC_FILE_SLIDER 底下就挂着三个图片）。
 */

/** 能不能往它下面挂一个**布局**：图层 / 布局 / 列表 / 表格。 */
bool acceptsLayout(const UiNode *n);
/** 能不能往它下面挂一个**普通控件**（NewFrame）：只有布局。 */
bool acceptsWidget(const UiNode *n);

/** 能不能往它下面收粘贴。那三条提示语写死了"请选择一个<布局>对像"。 */
bool acceptsChild(const UiNode *n);

/* ---- 点按钮建东西时，孩子到底挂到谁下面 ------------------------------
 * 规则如下：
 *
 *   「新建控件」
 *       选中为空                       -> 提示"请选择一个布局或者新建一个并选中它."
 *       选中是 NewLayer                -> 同上提示
 *       选中是 NewFrame/NewList/NewGrid -> 挂到**它的父级**（当兄弟，不钻进去）
 *       其它（NewLayout）              -> 挂到它自己
 *
 *   「新建布局」
 *       选中为空                       -> 提示"请选择一个图层或者新建一个图层,并选中它."
 *       选中是 NewLayout               -> 挂到它自己（布局套布局，既有工程里 3 例）
 *       选中是 NewLayer                -> 挂到它自己
 *       选中是 NewFrame/NewList        -> 挂到**它的父级**
 *       其它                           -> 什么也不做，连提示都没有
 *
 * 注意"挂到父级"这一条：一度是直接拦下报错，那不对 ——
 * 选中一个文字控件再点「新建文字」，本意是在**同一个布局里**再加一个。
 *
 * 【★ 一处取舍】选中**列表或表格**点「新建布局」，这里是**加进去**，
 * 因为那才是用户点这一下想要的东西；另一种做法是加到它的父级、加行只能走
 * 右键菜单「添加行」，表格则连加项的入口都没有。
 * 产物不受影响：加进去的还是 NewLayout、还是挂 listwidget 键，
 * 和既有工程里那 165 行一模一样的形状。
 */

/** @return 「新建控件」该挂到哪儿；nullptr = 该弹那句"请选择一个布局…". */
UiNode *hostForNewControl(UiNode *sel);
/**
 * @return 「新建布局」该挂到哪儿；nullptr = 拦下。
 * @param needTip 拦下时要不要弹"请选择一个图层…"。只有"选中为空"这一种
 *                情况弹，其余是静默返回。
 */
UiNode *hostForNewLayout(UiNode *sel, bool *needTip);

/**
 * 列表/表格里第 index 项（行/列/单元）该占的矩形，相对容器自己。
 *
 * 【为什么必须算】工程里的行不是随便摆的，`sizehw` / `space` 决定了
 * 每一格多大、隔多远，行自己的 rect 就是那一格：
 *
 *   垂直列表 VerticalList rect=(0,16,128,32) sizehw=16 space=0
 *       行 rect=(0, i*16, **128**, **16**)   宽=列表宽，高=sizehw
 *   水平列表 HorizontalList rect=(0,24,128,40) sizehw=32 space=0
 *       行 rect=(i*32, 0, **32**, **40**)    宽=sizehw，高=列表高
 *
 * 新建的行如果还用通用默认尺寸（比模板大得多），会被容器裁掉一大半 ——
 * 用户看到的就是"往列表里放了张图，预览里什么都没有"。
 *
 * 表格（NewGrid）工程里没有样本，按 cell_w / cell_h + rows/cols 铺，
 * 这是固件 ui_grid 那套行列语义的直读。
 *
 * @return 无效矩形表示 container 不是列表/表格，调用方该用通用默认值。
 */
QRect cellRectFor(const UiNode *container, int index);

/**
 * 往 parent 下面挂孩子时该用哪个 json 键。
 *
 * 【别想当然写 "widget"】SmallColorTFT.json 里根本没有 widget 这个键，
 * 全工程只用三个（277 个节点统计出来的实际组合）：
 *     ScenesScreen --layer--------> NewLayer
 *     NewLayer     --layout-------> NewLayout
 *     NewLayout    --layout-------> NewFrame / NewLayout / NewList
 *     NewList      --listwidget---> NewLayout   （列表的行模板，两个工程共 165 个）
 * 表格控件（NewGrid）的孩子也走 `listwidget`。
 * 【别用 "GridWidget"】读子节点只认 layer / layout / listwidget 三个键，
 * 写成别的名字，那棵子树在下游就直接看不见。
 * 表格和列表同为类型码 5，项也同样是 layout_info，走 listwidget
 * 是唯一既读得到、语义又对得上的选择。
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
 * 删除确认框：标题「删除提示」，按钮是 <删除> / 取消，
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

/**
 * 给 sub 这棵子树里每个节点重新分配一个不撞车的"唯一ID号"。
 *
 * 【为什么粘贴必须重分配】剪贴板里存的是节点的 json 副本，ename 一起带过来了；
 * 直接贴上去，ename.h 里就会出现两个同名宏（`#define X ...` 两次），
 * 业务代码引用到哪一个全看运气，所以粘出来的控件 ID 号一律是新的。
 *
 * 去重范围是**整棵工程树**（从 parent 一路往上找到根），不是只看兄弟节点。
 */
void reassignEnames(UiNode *sub, UiNode *parent);

/**
 * 把当前工程模型交给 EditorOps —— reassignEnames() 要靠它把去重范围扩到
 * **整个工程**。
 *
 * 【为什么不能顺着 UiNode::parent 往上爬】爬到页节点就到头了（页的 parent
 * 是 nullptr），跨页的 ename 根本扫不到：在页 0 的列表里加一行，分到的
 * BaseForm 会和页 1、页 3 里已有的撞车。
 */
void setModel(ProjectModel *m);

/** 在 parent 的孩子里取一个不重名的名字。 */
QString uniqueName(const UiNode *parent, const QString &base);

/**
 * 新建节点的默认名字：`<中文名>_<全局序号>`，例如 `布局_37`。
 *
 * 【为什么要单独抽出来】建出来的东西名字要是**中文 caption 加序号**
 * （图层_0 / 布局_1 / 文字_48），序号是**跨页连着走的全局计数**。
 * 以前只有 CompoentControls::appendChild() 这一条路照做，列表右键「添加行」
 * 那条路是自己现搭一个 `-name: "NewLayout"`，于是同一个工程里冒出英文名。
 *
 * @param caption 控件的中文名（布局 / 文字 / 图片…）；空的话退回 "控件"
 */
QString defaultNodeName(const QString &caption);

/**
 * 把控件模板库交给 EditorOps。
 *
 * 【为什么要这一手】"建一个控件"这件事有两条入口：控件栏那条走
 * CompoentControls（它自己拿得到 ControlLibrary），列表右键「添加行」那条
 * 在 BaseForm 里，够不着任何管理器。以前那条路就自己现搭一个
 * `{-class,-type,-name}` 的空壳 —— 一条属性都没有，属性面板上空空如也，
 * 生成资源时既没几何也没样式。两条路必须用**同一份模板**。
 */
void setLibrary(const ControlLibrary *lib);

/** 按 -type 取 control.json 里的模板原文；没有就返回空对象。 */
QJsonObject templateFor(const QString &type);

/**
 * 照模板建一个节点挂到 parent 下（不入 parent->children，由调用方决定位置）。
 *
 * 名字、唯一 ID 号、格子矩形全按和控件栏那条路一样的规矩来
 * （见 defaultNodeName / cellRectFor）。
 * @param type    control.json 里的 -type，如 "NewLayout"
 * @param caption 中文名，空的话用模板里的
 */
UiNode *makeFromTemplate(UiNode *parent, const QString &type,
                         const QString &caption = QString());

/* ---- 自定义控件目录 ---------------------------------------------------
 * [全局设置]里那一项："自定义的模版控件目录,默认是 widgets 目录"。
 * 默认落到工具目录的 assets/widgets.d/ —— ControlLibrary 扫的就是它，
 * 存进去下次启动就能在"自定义控件"那一组里看到。 */
void    setCustomWidgetDir(const QString &dir);
QString customWidgetDir();

/**
 * 把焦点里那个编辑器还没提交的改动逼出来。
 *
 * 属性面板上的输入框都是 editingFinished 才写回模型，而
 * **工具栏按钮是 Qt::NoFocus** —— 点"保存"/"资源导出"不会让输入框失焦，
 * editingFinished 不发，刚敲进去的值还停在控件里没进模型。
 * 用户看到的现象就是"改完直接点保存，参数没保存，得先点一下别处"。
 *
 * 凡是要读模型的动作（保存/另存为/资源导出/退出前的脏检查）都先调它。
 */
void commitPendingEdit();

} // namespace EditorOps

#endif // EDITOROPS_H
