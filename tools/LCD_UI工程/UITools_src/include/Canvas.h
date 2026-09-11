/*
 * Canvas.h —— 画布（页面）与画布管理器
 *
 * 【接口来源】ui-tools.exe 的 moc 元数据。★ 标记的成员签名与原二进制逐字一致。
 *   ScenesScreen  : QFrame    ★ slot onChangedBackgroundColor()
 *   CanvasManager : QObject   ★ Q_PROPERTY(QSize mPageSize) + 13 个槽
 *
 * CanvasManager 在原程序里是 QObject 而不是 QWidget —— 它是"文档 + 动作"的
 * 中枢：新建/打开/保存工程、增删页面、全局设置、缩放、截图，主窗口的菜单和
 * 工具栏直接连到它的槽上。本次重写保持这个职责划分。
 */
#ifndef CANVAS_H
#define CANVAS_H

#include <QFrame>
#include <QObject>
#include <QSize>
#include <QColor>
#include <QPixmap>
#include <QVector>
#include <QHash>
#include <QSet>

#include "ProjectModel.h"
#include "ControlLibrary.h"

class BaseForm;
class UiNode;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QWheelEvent;

/** 一页画布。工程树上的一个 page 节点对应一个 ScenesScreen。 */
class ScenesScreen : public QFrame
{
    Q_OBJECT

public:
    explicit ScenesScreen(QWidget *parent = nullptr);
    ~ScenesScreen() override;

    void     setPage(UiNode *page);
    UiNode  *page() const { return m_page; }

    /** 按工程树重建整页的画布控件。 */
    void rebuild();

    /** 选中某个节点对应的画布控件（树/页面列表点选时调用）。 */
    void selectNode(UiNode *node);

    /**
     * 树上那只"眼睛"：手动把某个节点从画布上藏起来/放出来。
     *
     * 这是**纯视觉**的，不写进工程数据 —— 手册 2.4 原话："隐藏操作是对某控件
     * 进行视觉隐藏，便于布局时的操作，对 UI 布局本身并无影响"。和属性面板上
     * 那个会存盘的"默认隐藏"是两码事。
     */
    void toggleUserHidden(UiNode *n);
    bool isUserHidden(UiNode *n) const { return m_userHidden.contains(n); }

    /** 连"默认隐藏"的控件也画出来（工具栏那个开关）。 */
    void setShowHidden(bool on);
    bool showHidden() const { return m_showHidden; }
    /**
     * 单独预览：选中谁，就只画谁所属的那个**顶层布局**，同级其它的一概不画。
     *
     * 【为什么必须是"不画"而不是"淡出"】这套工程一个图层下挂着 1~9 个尺寸
     * 全是整屏的布局（TFT 三页共 20 个，oled 十一页共 43 个），它们是互斥的
     * 界面状态 —— 主界面 / 音量 / EQ / 菜单…… 运行时只显示一个。整屏、背景
     * 不透明，淡到 18% 照样是一层洗在上面，而且叠放次序按创建顺序来，选中
     * 早建的那个时头上还压着后建的。只有真的不画才看得清。
     *
     * 数据也支持这么做：每一页**恰好只有一个** invisible=false 的顶层布局，
     * 所以"只画一个"正好还原运行时的样子，不会把本该同时显示的东西藏掉。
     */
    void setSolo(bool on);
    bool solo() const { return m_solo; }

    /** 当前页的"画面"列表 = 图层下的顶层布局，按先后顺序。 */
    QVector<UiNode *> screens() const;
    /** 当前正在预览的是第几个画面（没有就返回 -1）。 */
    int  currentScreenIndex() const;
    BaseForm *formFor(UiNode *node) const { return m_forms.value(node, nullptr); }
    /** 当前选中的节点（改倍率要能验证它没被弄丢）。 */
    UiNode *selectedNode() const { return m_selected; }

    int  zoom() const { return m_zoom; }
    void setZoom(int percent);

    QColor backgroundColor() const { return m_bg; }
    /** 页底色。跟[全局设置]里「像素熄灭颜色」走；右键「画布背景色…」也改它。 */
    void   setBackgroundColor(const QColor &c) { m_bg = c; update(); }
    /** 像素网格开关。之前画布只看 m_zoom，工具栏那个开关点了没反应。 */
    void setShowGrid(bool on);

    /**
     * 自测用：模拟一次"把控件拖到画布 pos 处松手"。
     *
     * 【为什么不能用 QApplication::sendEvent】Qt 的拖放事件是由
     * QWidgetWindow 直接派发给目标控件的，**不经过 QWidget::event()**，
     * 所以 sendEvent(canvas, &dropEvent) 送不到 dropEvent()，实测
     * dropEvent 一次都没被调到、测试还"通过"了（因为什么都没发生，
     * 节点数自然没变）。这里直接调处理函数，覆盖的是 nodeAt +
     * dropTargetFor + createDropped 这条真正属于本工程的链路。
     * @return 落点收下了返回 true
     */
    bool simulateDropForTest(const QPoint &pos, const QString &cls, const QString &type,
                             UiNode **landedOn = nullptr);
    bool showGrid() const { return m_showGrid; }
    /** 画不画编辑器的辅助线（控件描边/选中框/控件名/像素网格）。 */
    void setShowChrome(bool on);
    bool showChrome() const { return m_showChrome; }
    /** 画布底图（"修改背景"里双击选的那张）。空串 = 只用背景色。 */
    void setBackgroundImage(const QString &path);

signals:
    /** Ctrl+滚轮请求改倍率。真正改的是 CanvasManager（它管着所有页）。 */
    void zoomStepRequested(int delta);
    void nodeSelected(UiNode *node);
    void geometryEdited(UiNode *node);
    /** 树的结构变了（删/粘/挪层/加行），外面要 reload 树和页面栏。 */
    void structureChanged();
    /** 画布上有人点了右键菜单里的"查找对像"。 */
    void findRequested();
    /** 页面右键 -> 删除当前页面。真正动页数组的是 CanvasManager。 */
    void deletePageRequested();

public slots:
    void onChangedBackgroundColor();          ///< ★

signals:
    /** 有人把控件按钮拖到画布上了。参数是落点所在的容器和画布坐标。
     *  真正建节点的是 CompoentControls（限制、命名都在它那儿）。 */
    void controlDropped(UiNode *parent, const QString &cls, const QString &type,
                        const QPoint &pos);

protected:
    void resizeEvent(QResizeEvent *e) override;

    /** 空白处右键 = 页面的菜单：删除当前页面 / 修改背景色 / 修改背景图片。 */
    void contextMenuEvent(QContextMenuEvent *e) override;
    /** Ctrl+滚轮缩放；不按 Ctrl 就交给外层滚动条。 */
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

    void paintEvent(QPaintEvent *e) override;
    /** 画布上直接点控件时，把选中同步给树和属性面板。
     *  BaseForm 自己只会把手柄显出来，不通知任何人，所以在这儿拦一道。 */
    bool eventFilter(QObject *watched, QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    void buildRecursive(UiNode *n, QWidget *parentWidget);
    /** 按"默认隐藏 + 当前选中"重新决定谁显示、谁淡出。 */
    void applyVisibility();
    /** n 所属的顶层布局（父节点是图层的那一层）。 */
    UiNode *topScreenOf(UiNode *n) const;
    /** 画布坐标 p 落在哪个节点上（最深的那个）。拖放和空白右键都要用。 */
    UiNode *nodeAt(const QPoint &p) const;

public:
    /**
     * 拖 cls 落在 hit 这个节点上，最终会挂到谁下面。
     * 单独开出来给 ops-test 用：走像素点会被 Qt 的父级裁剪坑到
     * （工程里的列表常常被父布局裁得一个像素都点不到），
     * 而这里要验的是**规则**，不是命中测试。
     */
    bool resolveDropTargetForTest(UiNode *hit, const QString &cls,
                                  UiNode **target) const;

private:

    UiNode *m_page = nullptr;
    /** 当前选中的节点。selectNode() 靠它判重 —— 见 Canvas.cpp 里的说明。 */
    UiNode *m_selected = nullptr;
    int     m_zoom = 100;
    bool    m_showGrid = true;
    /** 像素网格的覆盖层：铺满画布、盖在所有控件之上，见 Canvas.cpp。 */
    QWidget *m_gridOverlay = nullptr;
    bool    m_showHidden = false;
    /** 被眼睛手动藏起来的节点（纯视觉，不进工程数据）。 */
    QSet<UiNode *> m_userHidden;
    bool    m_solo = true;
    QColor  m_bg;
    QPixmap m_bgImage;
    bool    m_showChrome = true;   ///< 见 setShowChrome()
    QHash<UiNode *, BaseForm *> m_forms;
};

/** 文档中枢：持有 ProjectModel + ControlLibrary，驱动全部页面。 */
class CanvasManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QSize mPageSize READ mPageSize WRITE setMPageSize)   ///< ★

public:
    explicit CanvasManager(QObject *parent = nullptr);
    ~CanvasManager() override;

    /* ★ 原二进制里的属性 */
    QSize mPageSize() const { return m_pageSize; }
    void  setMPageSize(const QSize &v);

    ProjectModel   *model() { return &m_model; }
    ControlLibrary *library() { return &m_lib; }

    /** UITools 根目录（控件库、图标、多国语言表都在这一级）。 */
    void    setToolsRoot(const QString &p);
    QString toolsRoot() const { return m_lib.root(); }

    /** 主窗口把画布容器交给管理器，页面切换由管理器负责。 */
    void          attachHost(QWidget *host);
    ScenesScreen *screen(int pageIndex) const;
    ScenesScreen *currentScreen() const;
    int           currentPage() const { return m_current; }
    void          setCurrentPage(int i);

    /** 有未保存改动就按原厂那句话问一次。true = 可以继续。 */
    bool confirmDiscardChanges();

    /**
     * 改过没有 —— 唯一的入口，别再各处直接 m_model.setDirty()。
     *
     * 【为什么要收口】"标题带不带 *" 和 "退出问不问要不要保存" 都只看
     * ProjectModel::dirty()。以前拖控件、改结构会置它，**属性面板改参数不会**
     * （UiNode::markDirty() 只置节点自己的标志，够回写用，但传不到模型），
     * 而且置了也没人去刷标题。结果就是改完参数看不到 *、退出也不提示，
     * 一不小心就白改。现在统一走这里，顺带发信号让标题跟上。
     */
    void setDirty(bool d);
    void markDirty() { setDirty(true); }

    /* ---- 画布缩放 ----------------------------------------------------
     * 【原厂没有这个】原厂画布只有 1:1，128x64 在 927px 宽的画布上就是左上角
     * 一个指甲盖。点阵屏工程尤其难受，所以加了缩放。缩放**只影响显示**：
     * UiNode::rect 始终存 1:1 的像素坐标，缩放着编辑再保存不会把倍率乘进去
     * （见 ScenesScreen::buildRecursive 里的换算）。 */
    int  zoom() const { return m_zoom; }
    void setZoom(int percent);
    /** 让当前页正好铺满可视区，返回算出来的倍率。 */
    int  zoomToFit(const QSize &viewport);

    /**
     * 打开工程后补齐"缺胳膊少腿"的节点：没有 element_css 的、
     * 没有唯一 ID 号的。
     *
     * 【为什么会缺】旧版「添加行」在列表还空着时现搭了个只有
     * -class/-type/-name 的壳（见 docs/FACTORY_UI.md §14.12）。这种节点没有
     * 几何也没有样式，生成资源时它和它整棵子树的 css 全是零尺寸，
     * 烧进设备什么都不显示，而且从编辑器到生成器没有一处报错。
     *
     * 建节点那条路已经修好了，但**已经存进 json 的坏节点救不回来** ——
     * 只能在打开时按 control.json 的模板补一份，并按它在容器里的位置
     * 给一个合理的矩形。
     *
     * 没有 ID 号那条同样致命：生成 ename.h 时宏名是空的，写出来就是
     * `#define  0XC30002`，编译器一看就炸。
     *
     * @return 补了几个（按节点算，一个节点补两样也只算一个）
     */
    int healBrokenNodes();

    /**
     * 把 [全局设置] 的配置文件绑到这个工程的目录下，并重读跟它走的东西
     * （点阵屏预览配色、每个文字控件的「预览文字」）。
     *
     * 打开 / 另存工程之后调。这样复制一份 UI 工程，这些设置一起被复制走。
     */
    void bindSettingsToProject(const QString &jsonPath);

    /* ---- 无人值守入口（--make-sample / ops-test 用）------------------
     * onCreateNewProject() / onCreateNewScenesScreen() 里塞满了模态框
     * （"是否关闭当前工程"、ProjectDialog…），脚本里跑不了。这两个是
     * 把对话框之后那段正事单拎出来，行为和走界面完全一致。 */
    /** 新建一个空工程（一页 + 一图层 + 一布局），跳过所有对话框。 */
    void newProjectForTest(const QString &name, const QSize &pageSize);
    /** 追加一页，跳过对话框。 */
    void addPageForTest();

    /** 画布可见性开关，转发给所有页。 */
    void setShowHidden(bool on);
    bool showHidden() const { return m_showHidden; }
    /** 网格开关。ops-test 要看它有没有被[全局设置]误改。 */
    bool showGrid() const { return m_showGrid; }
    /**
     * 辅助线总开关，转发给所有页。
     *
     * 关掉之后画布上只剩屏上真会显示的像素：控件的虚线描边、选中框、
     * 控件名、像素网格全不画。放大到 400% 看真实效果时用。
     */
    void setShowChrome(bool on);
    bool showChrome() const { return m_showChrome; }
    void setSolo(bool on);
    bool solo() const { return m_solo; }
    /** 翻到当前页的上一个/下一个画面。 */
    void stepScreen(int delta);
    /** 直接跳到当前页的第 index 个画面（工具栏那个画面下拉走这条）。 */
    void gotoScreen(int index);

    bool openProject(const QString &path, QString *err);
    bool saveProjectAs(const QString &path, QString *err);
    /// 保存后把 json 文件名写回 config/ini/project.ini，下游 QtToolBin 靠它
    void writeProjectIni(const QString &jsonPath);

signals:
    void projectChanged();                 ///< 打开/新建后，树和页面列表要重建
    void pagesChanged();
    void currentPageChanged(int index);
    /** 缩放变了（Ctrl+滚轮改的时候要让工具栏那个下拉跟上）。 */
    void zoomChanged(int percent);
    /** 画面列表或当前画面变了，工具栏上那个"2/5 布局_11"要跟着刷。 */
    void screenListChanged();
    void nodeSelected(UiNode *node);
    /** 改过/存过，标题上那个 * 要跟着变。 */
    void dirtyChanged(bool dirty);
    void statusMessage(const QString &msg);
    /** 当前页里有节点被删/粘/挪层，主窗口据此 reload 树和页面栏。 */
    void structureChanged();
    /** 画布右键菜单里的"查找对像"。 */
    void findRequested();
    /** 预览配色变了（[全局设置]里改的）。只重画，不动工程数据。 */
    void previewStyleChanged();
    /** 有控件被拖到画布上。转发自当前页的 ScenesScreen。 */
    void controlDropped(UiNode *parent, const QString &cls, const QString &type,
                        const QPoint &pos);

public slots:
    /* ★ 以下 10 个是 public，3 个是 private —— 与二进制里的 access 一致 */
    void onSaveProject();
    void onSaveAsProject();
    void onOpenProject();
    void onCreateNewProject();
    void onUpdateNewProjectSize();
    void onGlobalBtn();
    /** [全局设置]点了确定之后要做的事。单独拆出来是为了能无人值守回归 ——
     *  GlobalSettings::exec() 是模态的，测不了。 */
    void applyGlobalSettings();
    void onSshoot();
    void onZoomProject();
    void onAboutBtn();
    void onSelectGrid();

private slots:
    void onCreateNewScenesScreen();        ///< ★
    void onDelCurrentScenesScreen();       ///< ★
    void onConfProject();                  ///< ★

private:
    void rebuildScreens();

    ProjectModel    m_model;
    ControlLibrary  m_lib;
    QWidget        *m_host = nullptr;
    QWidget        *m_stackHost = nullptr;   ///< 真正装页面的容器，外层负责左上对齐
    QVector<ScenesScreen *> m_screens;
    int    m_current = 0;
    QSize  m_pageSize = QSize(128, 64);
    bool   m_showGrid = true;
    int    m_zoom = 100;
    bool   m_showHidden = false;
    bool   m_solo = true;
    bool   m_showChrome = true;
};

#endif // CANVAS_H
