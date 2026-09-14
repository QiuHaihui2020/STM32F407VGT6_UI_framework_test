/*
 * Forms.h —— 画布上的可编辑控件族
 *
 * 【类族】继承关系与 signals/slots 如下。带 ★ 的是画布控件对外的固定接口，
 * 别的地方按签名连着，改动要一起改。
 *
 *   ResizeFrame      : QWidget      ★ signal formWindowSizeChanged(QRect, QRect)
 *   ResizeHandle   : QWidget      ★ signal mouseButtonReleased(QRect, QRect)
 *   CanvasItem         : ResizeFrame  ★ enum ObjTypes + 13 个槽
 *   NewLayer/NewLayout/NewFrame/NewList/NewGrid : CanvasItem
 *
 * ResizeFrame / ResizeHandle 这两个名字沿用 Qt Designer 自带的
 * qdesigner_internal（formresizer.cpp / widgetselection.cpp）里的叫法，
 * 但这里是全局符号，不带命名空间。
 */
#ifndef FORMS_H
#define FORMS_H

#include <QPointer>
#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QColor>
#include <QString>
#include <QVector>

class UiNode;
class CanvasPage;
class QContextMenuEvent;
class QMenu;
class QPainter;
class QWheelEvent;

/** 八个方向的拖拽手柄之一。 */
class ResizeHandle : public QWidget
{
    Q_OBJECT

public:
    enum Direction { LeftTop, Top, RightTop, Right, RightBottom, Bottom, LeftBottom, Left };

    explicit ResizeHandle(QWidget *parent = nullptr, Direction d = RightBottom,
                            QWidget *target = nullptr);
    ~ResizeHandle() override;

    Direction direction() const { return m_dir; }
    void updatePosition();

signals:
    /* ★ 签名：void mouseButtonReleased(QRect, QRect) */
    void mouseButtonReleased(QRect a0, QRect a1);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    Direction m_dir;
    /* 【必须是 QPointer】目标控件可能先于本手柄被销毁（见 ResizeFrame 抬头），
     * 裸指针的话下面那些 !m_target 判空全是摆设。 */
    QPointer<QWidget> m_target;
    bool      m_dragging = false;
    QPoint    m_pressGlobal;
    QRect     m_startGeo;
};

/** 带尺寸手柄的可选中窗体。选中时在四周挂出 8 个 ResizeHandle。 */
class ResizeFrame : public QWidget
{
    Q_OBJECT

public:
    explicit ResizeFrame(QWidget *parent = nullptr);
    ~ResizeFrame() override;

    bool isSelected() const { return m_selected; }
    void setSelected(bool on);

    /**
     * 画不画编辑器自己的辅助线：虚线描边、选中框、控件名，
     * 以及选中时那 8 个缩放手柄。
     *
     * 关掉之后控件上只剩屏上真会显示的东西（背景填充、css 内边框线、
     * 图片/文字/数字）。放大看真实效果时用 —— 工具栏「隐藏辅助线」就是它。
     *
     * 【为什么在 ResizeFrame 这一层】手柄是挂在**父控件**上的独立子窗口
     * （ResizeHandle），不是 CanvasItem 自己画的，只能在这儿一起管。
     */
    void setShowChrome(bool on);
    bool showChrome() const { return m_showChrome; }

signals:
    /* ★ 原始签名：void formWindowSizeChanged(QRect oldGeo, QRect newGeo) */
    void formWindowSizeChanged(QRect oldGeo, QRect newGeo);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void moveEvent(QMoveEvent *e) override;

    /** 由子类在几何变化后调用，负责发 formWindowSizeChanged 并刷新手柄。 */
    void notifyGeometryChanged(const QRect &oldGeo);

    bool m_selected = false;
    bool m_showChrome = true;      ///< 见 setShowChrome()

private:
    void createHandles();
    void layoutHandles();

    /* 【手柄不是本控件的子窗口】它们挂在**父控件**上（要画在本控件外面），
     * 所以本控件析构时 Qt 不会带走它们 —— ~ResizeFrame() 里手动删。
     * 用 QPointer 是因为反过来也可能：父控件被销毁时 Qt 会先删掉手柄，
     * 那时本控件再去 delete 就是二次释放。 */
    QVector<QPointer<ResizeHandle>> m_handles;
    QRect m_lastGeo;
};

/**
 * 画布控件基类。每个实例绑定工程树上的一个 UiNode，
 * 属性面板改值 -> 走这里的槽 -> 回写 UiNode -> 重绘。
 */
class CanvasItem : public ResizeFrame
{
    Q_OBJECT

public:
    /* ★ 取值不要动：.sty 里控件 type 依赖它 */
    enum ObjTypes {
        T_NewLayer  = 0,
        T_NewLayout = 1,
        T_NewFrame  = 2,
        T_NewList   = 3,
        T_NewGrid   = 4,
        TYPESS      = 5,
        Object      = 6,
    };
    Q_ENUM(ObjTypes)

    explicit CanvasItem(QWidget *parent = nullptr);
    ~CanvasItem() override;

    void     bind(UiNode *node);
    UiNode  *node() const { return m_node; }
    virtual ObjTypes objType() const { return Object; }

    /** 把当前几何写回 UiNode::rect（画布坐标 = 屏幕像素坐标）。 */
    void syncRectToNode();
    /** 反向：按 UiNode::rect 摆好自己。 */
    void syncRectFromNode();

    /**
     * 画布倍率。**只影响显示**，UiNode 里的坐标永远是 1:1。
     *
     * 【为什么要让 CanvasItem 自己知道倍率】以前 syncRectToNode() 直接拿
     * geometry() 回写，压根没除倍率；400% 下拖一下，写回去的就是 4 倍的
     * 坐标。当时没炸是因为紧跟着的 formWindowSizeChanged 又用正确值写了
     * 一遍盖过去 —— 纯属侥幸，谁先谁后换个顺序就出事。现在换算收在这里，
     * 进出都走同一套公式。
     */
    void setDisplayZoom(int percent);
    int  displayZoom() const { return m_zoom; }


    /**
     * 画布和对象树共用的右键菜单。
     *
     * 两边是同一套动作 —— 那一串菜单文字
     * （"删除当前-%1 / 保存成控件 / 显示同类容器 / 显示 / 隐藏同类容器 /
     * 隐藏 / 复制 / 粘贴 / 查找控件" 加上 "移到顶层 / 移上一层 / 移下一层 /
     * 移到底层"）就是这个菜单，ObjectTreeDock 只是把右键位置转发过来。
     */
    void showContextMenu(const QPoint &globalPos);

    /* 右键菜单里那几个动作也单独暴露出来：对象树要用，自测（--ops-test）
     * 也要用 —— 只有能在没人点鼠标的情况下跑，这些限制才谈得上回归。 */
    /** "粘贴"。判得了就贴，判不了按提示语拒绝。 */
    void doPaste();
    /** "保存成控件"。 */
    void saveAsTemplate();
    /** 直接子级的画布控件（"显示/隐藏同类容器"要用）。 */
    QVector<CanvasItem *> subForms() const;

signals:
    /** 结构变了（删除 / 粘贴 / 挪层 / 加行），树、页面栏、画布都得重来。 */
    void structureChanged();
    /** 请求打开"查找控件"对话框。 */
    void findRequested();
    /** 请求把某个节点从画布上藏起来/放出来（纯视觉，由画布统一记账）。 */
    void userHideRequested(UiNode *n);

public slots:
    /* ★ 以下 13 个槽的名字与签名各处都按它连着，别随手改 */
    void onXYWHChangedValue(int v);
    void onSwapViewObject();
    void onClearJsonValue();
    void onTextChanged(QString str);
    void onTextSelected();
    void onNumberChanged(int num);
    void onEnumItemChanged(QString txt);
    void onColorButtonClicked();
    void onBorderChangedValue(int v);
    void onBackgroundImageDialog();
    void onActionDialog();
    void onDeleteMe();
    void onListImageChanged(QString a0);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

    /** 子类给自己挑个边框色，好在画布上区分层级。 */
    virtual QColor frameColor() const;
    /** 内容的水平对齐（element_css 的 align）。 */
    Qt::Alignment contentAlign() const;
    /** 画 css 里的"内边框线"。 */
    void paintBorder(QPainter &p);
    /** 内容来自资源、但资源还没配 —— 画布上要给个占位框。 */
    bool isContentEmpty() const;

    /** 子类往右键菜单里加自己特有的项（列表的加行、表格的加行列…）。 */
    virtual void appendTypeActions(QMenu &menu) { Q_UNUSED(menu) }

public:
    /**
     * 画布把这个控件的**子控件全建完之后**调一次。
     *
     * 列表要靠它把各行摆到格子上：relayoutRows() 得等子控件存在才有意义，
     * 而 bind() 跑的时候一个孩子都还没有。以前只有滚轮翻行会调 relayout，
     * 于是画布重建之后行是散的 —— 行比格子大，里头的图片被裁得看不见。
     */
    virtual void onSubtreeBuilt() {}

protected:
    /**
     * 按 EditorOps::cellRectFor() 把每个孩子的矩形重算一遍（列表的行、
     * 表格的项）。sizehw / space / 行列数 / 滚动方向一变，格子就变了，
     * 各项的 rect 得跟着走 —— 那不只是显示，生成资源读的就是它。
     * @return 有没有真的改动过
     */
    bool reflowCells();
    /** 改完格子参数统一走这里：重排 + 通知外面重画、置脏。 */
    void cellParamsChanged();

    UiNode *m_node = nullptr;

private:
    int    m_zoom = 100;
    bool   m_moving = false;
    QPoint m_pressGlobal;
    QRect  m_startGeo;
};

class NewLayer : public CanvasItem
{
    Q_OBJECT
public:
    explicit NewLayer(QWidget *parent = nullptr);
    ~NewLayer() override;
    ObjTypes objType() const override { return T_NewLayer; }
    QColor frameColor() const override;
public slots:
    void onDeleteMe();            ///< ★
};

class NewLayout : public CanvasItem
{
    Q_OBJECT
public:
    explicit NewLayout(QWidget *parent = nullptr);
    ~NewLayout() override;
    ObjTypes objType() const override { return T_NewLayout; }
    QColor frameColor() const override;
public slots:
    void onDeleteMe();            ///< ★
    void onBeComeTemplateWidget();///< ★
};

class NewFrame : public CanvasItem
{
    Q_OBJECT
public:
    explicit NewFrame(QWidget *parent = nullptr);
    ~NewFrame() override;
    ObjTypes objType() const override { return T_NewFrame; }
    QColor frameColor() const override;
public slots:
    void onDeleteMe();            ///< ★
};

class NewList : public CanvasItem
{
    Q_OBJECT
public:
    explicit NewList(QWidget *parent = nullptr);
    ~NewList() override;
    ObjTypes objType() const override { return T_NewList; }
    QColor frameColor() const override;
public slots:
    void onAddManyLine();         ///< ★
    void onSetFixedHeight();      ///< ★
    void onDeleteMe();            ///< ★

public:
    /* 下面这几个是"改格子参数"的正经入口：右键菜单问完数字调它们，
     * ops-test 也直接调它们 —— 免得为了测一条规则去驱动模态对话框。 */
    void setCellSize(int v);              ///< 行高（垂直）/ 列宽（水平）
    void setCellSpace(int v);             ///< 单元间隔
    void setOrientation(bool vertical);   ///< 垂直滚动 / 水平滚动

public:
    /** 当前滚到第几行/列。手册 2.11："如果垂直列表控件不够显示所有行，
     *  可以通过鼠标滚轮来进行行切换。" */
    int  firstVisible() const { return m_first; }
    void setFirstVisible(int i);

protected:
    void appendTypeActions(QMenu &menu) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

public:
    void onSubtreeBuilt() override { relayoutRows(); }

private:
    /** 按 sizehw / space / m_first 把各行摆好。 */
    void relayoutRows();

    int m_first = 0;
};

class NewGrid : public CanvasItem
{
    Q_OBJECT
public:
    explicit NewGrid(QWidget *parent = nullptr);
    ~NewGrid() override;
    ObjTypes objType() const override { return T_NewGrid; }
    QColor frameColor() const override;
public:
    void setCellSize(int w, int h);       ///< 单元尺寸
    void setCellSpace(int v);             ///< 单元间距
    void setGrid(int rows, int cols);     ///< 设置行列

public slots:
    void onDeleteMe();            ///< ★
    void onAddOneRow();           ///< ★
    void onAddOneCol();           ///< ★

protected:
    void appendTypeActions(QMenu &menu) override;
};

/** 按工程 json 里的 "-class" 造出对应的画布控件。 */
CanvasItem *createFormForClass(const QString &cls, QWidget *parent);

#endif // FORMS_H
