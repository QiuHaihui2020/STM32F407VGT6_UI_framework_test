/*
 * Forms.h —— 画布上的可编辑控件族
 *
 * 【接口来源】类名、继承关系、signals/slots 全部取自 ui-tools.exe 里逆向出的
 * Qt moc 元数据（见 docs/RECOVERED_CLASSES.txt）。带 ★ 的成员是原始二进制里
 * 确实存在的，签名逐字一致，不要改；其余是本次重写为了实现功能新增的。
 *
 *   FormResizer      : QWidget      ★ signal formWindowSizeChanged(QRect, QRect)
 *   SizeHandleRect   : QWidget      ★ signal mouseButtonReleased(QRect, QRect)
 *   BaseForm         : FormResizer  ★ enum ObjTypes + 13 个槽
 *   NewLayer/NewLayout/NewFrame/NewList/NewGrid : BaseForm
 *
 * FormResizer / SizeHandleRect 这两个名字来自 Qt Designer 自带的
 * qdesigner_internal（formresizer.cpp / widgetselection.cpp）。原作者把那份
 * 代码抄进了工程并挪出命名空间 —— 从二进制看它们是全局符号，不带命名空间。
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
class ScenesScreen;
class QContextMenuEvent;
class QMenu;
class QPainter;
class QWheelEvent;

/** 八个方向的拖拽手柄之一。 */
class SizeHandleRect : public QWidget
{
    Q_OBJECT

public:
    enum Direction { LeftTop, Top, RightTop, Right, RightBottom, Bottom, LeftBottom, Left };

    explicit SizeHandleRect(QWidget *parent = nullptr, Direction d = RightBottom,
                            QWidget *target = nullptr);
    ~SizeHandleRect() override;

    Direction direction() const { return m_dir; }
    void updatePosition();

signals:
    /* ★ 原始签名：void mouseButtonReleased(QRect, QRect) —— 参数在二进制里没有名字 */
    void mouseButtonReleased(QRect a0, QRect a1);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    Direction m_dir;
    /* 【必须是 QPointer】目标控件可能先于本手柄被销毁（见 FormResizer 抬头），
     * 裸指针的话下面那些 !m_target 判空全是摆设。 */
    QPointer<QWidget> m_target;
    bool      m_dragging = false;
    QPoint    m_pressGlobal;
    QRect     m_startGeo;
};

/** 带尺寸手柄的可选中窗体。选中时在四周挂出 8 个 SizeHandleRect。 */
class FormResizer : public QWidget
{
    Q_OBJECT

public:
    explicit FormResizer(QWidget *parent = nullptr);
    ~FormResizer() override;

    bool isSelected() const { return m_selected; }
    void setSelected(bool on);

    /**
     * 画不画编辑器自己的辅助线：虚线描边、选中框、控件名，
     * 以及选中时那 8 个缩放手柄。
     *
     * 关掉之后控件上只剩屏上真会显示的东西（背景填充、css 内边框线、
     * 图片/文字/数字）。放大看真实效果时用 —— 工具栏「隐藏辅助线」就是它。
     *
     * 【为什么在 FormResizer 这一层】手柄是挂在**父控件**上的独立子窗口
     * （SizeHandleRect），不是 BaseForm 自己画的，只能在这儿一起管。
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
     * 所以本控件析构时 Qt 不会带走它们 —— ~FormResizer() 里手动删。
     * 用 QPointer 是因为反过来也可能：父控件被销毁时 Qt 会先删掉手柄，
     * 那时本控件再去 delete 就是二次释放。 */
    QVector<QPointer<SizeHandleRect>> m_handles;
    QRect m_lastGeo;
};

/**
 * 画布控件基类。每个实例绑定工程树上的一个 UiNode，
 * 属性面板改值 -> 走这里的槽 -> 回写 UiNode -> 重绘。
 */
class BaseForm : public FormResizer
{
    Q_OBJECT

public:
    /* ★ 逆向自二进制的枚举，取值不要动：.sty 里控件 type 依赖它 */
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

    explicit BaseForm(QWidget *parent = nullptr);
    ~BaseForm() override;

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
     * 【为什么要让 BaseForm 自己知道倍率】以前 syncRectToNode() 直接拿
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
     * 原厂两边是同一套动作 —— 从 ui-tools.exe 里扒出来的那一串菜单文字
     * （"删除当前-%1 / 保存成控件 / 显示同类容器 / 显示 / 隐藏同类容器 /
     * 隐藏 / 复制 / 粘贴 / 查找对像" 加上 "移到顶层 / 移上一层 / 移下一层 /
     * 移到底层"）就是这个菜单，TreeDock 只是把右键位置转发过来。
     */
    void showContextMenu(const QPoint &globalPos);

    /* 右键菜单里那几个动作也单独暴露出来：对象树要用，自测（--ops-test）
     * 也要用 —— 只有能在没人点鼠标的情况下跑，这些限制才谈得上回归。 */
    /** "粘贴"。判得了就贴，判不了按原厂的话术拒绝。 */
    void doPaste();
    /** "保存成控件"。 */
    void saveAsTemplate();
    /** 直接子级的画布控件（"显示/隐藏同类容器"要用）。 */
    QVector<BaseForm *> subForms() const;

signals:
    /** 结构变了（删除 / 粘贴 / 挪层 / 加行），树、页面栏、画布都得重来。 */
    void structureChanged();
    /** 请求打开"查找对像"对话框。 */
    void findRequested();
    /** 请求把某个节点从画布上藏起来/放出来（纯视觉，由画布统一记账）。 */
    void userHideRequested(UiNode *n);

public slots:
    /* ★ 以下 13 个槽的名字与签名逐字来自二进制 */
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

    /** 子类往右键菜单里加自己特有的项（列表的加行、表格的加行列…）。 */
    virtual void appendTypeActions(QMenu &menu) { Q_UNUSED(menu) }

    UiNode *m_node = nullptr;

private:
    int    m_zoom = 100;
    bool   m_moving = false;
    QPoint m_pressGlobal;
    QRect  m_startGeo;
};

class NewLayer : public BaseForm
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

class NewLayout : public BaseForm
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

class NewFrame : public BaseForm
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

class NewList : public BaseForm
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
    /** 当前滚到第几行/列。手册 2.11："如果垂直列表控件不够显示所有行，
     *  可以通过鼠标滚轮来进行行切换。" */
    int  firstVisible() const { return m_first; }
    void setFirstVisible(int i);

protected:
    void appendTypeActions(QMenu &menu) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    /** 按 sizehw / space / m_first 把各行摆好。 */
    void relayoutRows();

    int m_first = 0;
};

class NewGrid : public BaseForm
{
    Q_OBJECT
public:
    explicit NewGrid(QWidget *parent = nullptr);
    ~NewGrid() override;
    ObjTypes objType() const override { return T_NewGrid; }
    QColor frameColor() const override;
public slots:
    void onDeleteMe();            ///< ★
    void onAddOneRow();           ///< ★
    void onAddOneCol();           ///< ★

protected:
    void appendTypeActions(QMenu &menu) override;
};

/** 按工程 json 里的 "-class" 造出对应的画布控件。 */
BaseForm *createFormForClass(const QString &cls, QWidget *parent);

#endif // FORMS_H
