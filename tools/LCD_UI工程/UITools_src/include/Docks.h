/*
 * Docks.h —— 主窗口四周的停靠面板（布局照原厂 ui-tools.exe 复刻）
 *
 * 【原厂布局】把 ui-tools.exe 跑起来加载 SmallColorTFT.json 后的实际形态：
 *
 *   ┌────────────────────────────────────────────────────────────────────┐
 *   │ [新建工程][打开工程][保存工程][另存为]│[新建页面][删除当前页]│[截屏]│ │
 *   │ [全局设置][工程缩放]│[关于]   初始化编辑环境完成                    │  ← 只有工具栏，没有菜单栏
 *   ├──────────┬──────────────┬──────────────────────────┬───────────────┤
 *   │ 结点│属性│ID号          │                          │  页面_0 渲染   │
 *   │ 图层_0  NewLayer  BT_LA │  控件列表 ┌────────────┐ │  页面_1 渲染   │
 *   │  布局_1 NewLayout BT_LA │   [ 图层 ]              │ │  页面_2 渲染   │
 *   │   电池电量_2 NewFrame   │   [ 布局 ]              │ │               │
 *   │   文字_3     NewFrame   │   电池电量│图片          │ │  ← PageView   │
 *   │   ...                   │   文字   │时间          │ │               │
 *   │                         │   数字   │表格控件      │ │               │
 *   │                         │   垂直列表│水平列表     │ │               │
 *   │                         │   自定义控件 slider/vslider              │
 *   │                         ├──────────────┤          │               │
 *   │                         │ ID号 [...]   │  画布     │               │
 *   │                         │ CSS属性_0    │          │               │
 *   │                         │ 对齐方式...  │          │               │
 *   │ 控件数量: 277           │ 位置坐标...  │          │               │
 *   └──────────┴──────────────┴──────────────────────────┴───────────────┘
 *      TreeDock      控件列表+属性区(第二列)      中央画布      PageView
 *
 * 配色（从原厂截图采样）：面板绿 #C0DCC0、属性区 #CEE2CE、
 * 列表内白底 #F1F1F1、画布灰 #F0F0F0。
 *
 * ★ 标记的成员来自逆向出的 moc 元数据，签名逐字一致。
 */
#ifndef DOCKS_H
#define DOCKS_H

#include "Property.h"

#include <QComboBox>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QGroupBox>
#include <QTabWidget>
#include <QImage>
#include <QPoint>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QWidget;

class UiNode;
class CanvasManager;
class ComProperty;
class CssProperty;
class BaseScrollArea;

/** 左侧对象树：三列 结点 / 属性 / ID号，容器行带"眼睛"开关，底部显示控件总数。 */
class TreeDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit TreeDock(QWidget *parent = nullptr);
    ~TreeDock() override;

    void setManager(CanvasManager *m) { m_mgr = m; }
    void reload();
    void selectNode(UiNode *n);
    /** 树上当前高亮的那个节点；没有就是 nullptr。ops-test 用。 */
    UiNode *currentNodeForTest() const;

signals:
    void nodeActivated(UiNode *node);

public slots:
    void onItemPressed(QTreeWidgetItem *item, int col);   ///< ★
    void onCustomContextMenu(QPoint point);               ///< ★
    void onSwapShowHideObject();                          ///< ★
    void onSwapShowHideSubObject();                       ///< ★

private:
    void addNode(UiNode *n, QTreeWidgetItem *parentItem);
    UiNode *nodeOf(QTreeWidgetItem *it) const;
    int  countAll() const;

    QTreeWidget   *m_tree = nullptr;
    QLabel        *m_count = nullptr;
    CanvasManager *m_mgr = nullptr;
};

/** 右侧页面栏：每页按实际尺寸渲染一遍，下方是可改名的标题。 */
class PageView : public QDockWidget
{
    Q_OBJECT

public:
    explicit PageView(QWidget *parent = nullptr);
    ~PageView() override;

    void setManager(CanvasManager *m) { m_mgr = m; }
    void reload();

    /**
     * 右列：当前页的**顶层布局**，也就是工具栏「当前画面」下拉框里那一组。
     *
     * 只放顶层是有取舍的：这些布局互斥，实测一页最多 9 个，一屏放得下；
     * 连嵌套的一起放会到 43 个，其中 34 个是列表的行（128x16、长得都一样），
     * 有用的那 9 张就被淹了。
     *
     * 节点集合没变时只更新「当前」标记，不重建控件 —— 每选一次控件都重建
     * 9 个预览太浪费。
     */
    void reloadLayouts();

    /** 自测用：把第 i 页的渲染抓成图（要和画布画出来的一致）。 */
    QImage grabPageForTest(int i) const;
    /** 自测用：右列现在有几张。 */
    int layoutCountForTest() const { return m_layoutNodes.size(); }
    /** 自测用：右列第 i 张对应的节点。 */
    UiNode *layoutNodeForTest(int i) const
    {
        return (i >= 0 && i < m_layoutNodes.size()) ? m_layoutNodes.at(i) : nullptr;
    }
    /** 自测用：右列第 i 张的渲染（不含外面那圈留白）。 */
    QImage grabLayoutForTest(int i) const;

signals:
    void pageActivated(int index);
    /** 右列点了第 index 个顶层布局。 */
    void layoutActivated(int index);

public slots:
    void onClickedItem(QListWidgetItem *a0);   ///< ★
    void onItemChanged(QListWidgetItem *a0);   ///< ★
    void onLayoutClicked(QListWidgetItem *a0);

private:
    /** 只刷新「当前」标记和选中行，不动控件。 */
    void markCurrentLayout();

    QListWidget       *m_list = nullptr;
    QListWidget       *m_layouts = nullptr;
    QWidget           *m_brace = nullptr;      ///< 两列中间那个大括号
    QVector<UiNode *>  m_layoutNodes;
    CanvasManager     *m_mgr = nullptr;
    bool               m_loading = false;
};

/** "控件列表" 组框：图层 / 布局 两个大按钮 + 控件网格 + 自定义控件。
 *  数据来自 UITools/control/control.json 与 control/ex/*.json。 */
class CompoentControls : public QGroupBox
{
    Q_OBJECT

public:
    explicit CompoentControls(QWidget *parent = nullptr);
    ~CompoentControls() override;

    void setManager(CanvasManager *m) { m_mgr = m; }
    void reload();
    /** 当前选中的节点。原厂的新建限制是"看你选中的是什么"，所以必须知道它。 */
    void setCurrentNode(UiNode *n) { m_current = n; }
    /** 拖放落地的总入口（画布已经判过落点合法性）。 */
    void createDropped(UiNode *parent, const QString &cls, const QString &type,
                       const QPoint &pos);

signals:
    void nodeCreated(UiNode *node);

public slots:
    void onCreateCompoentToCanvas();   ///< ★
    void onCreateCustomWidget();       ///< ★
    void onCreateNewLayout();          ///< ★
    void onCreateNewLayer();           ///< ★

    /**
     * 建控件的公共路径：查限制 -> 问名字 -> 挂上去。自测直接调它。
     * @param parent 挂到谁下面。nullptr = 用当前选中的节点（点击那条路）；
     *               非空 = 拖放指定的容器。
     * @param pos    落点（相对 parent）。无效点表示不指定，用默认位置。
     */
    void createControl(const QString &cls, const QString &type, const QString &caption,
                       UiNode *parent = nullptr, const QPoint &pos = QPoint(-1, -1));

private:
    UiNode *appendChild(UiNode *parent, const QString &cls, const QString &type,
                        const QString &caption, const QString &name,
                        const QPoint &pos = QPoint(-1, -1));
    void addControlButton(const QString &cls, const QString &type,
                          const QString &caption, int row, int col);

    BaseScrollArea *m_area = nullptr;
    QWidget        *m_grid = nullptr;
    QGroupBox      *m_custom = nullptr;
    CanvasManager  *m_mgr = nullptr;
    UiNode         *m_current = nullptr;   ///< 当前选中节点，新建限制据此判断
};

/** "CSS属性_N" 页签容器。原厂一个 CSS 状态一个页签，当前实现只有 _0。 */
/**
 * 属性区的两个页签：「基础设置」和「资源」。
 *
 * 【和原厂的差别】原厂是**上下堆叠**：ID号 / CSS属性_0、_1… 页签 / 控件专有
 * 属性区，一个「文字」控件铺满 22 行，窄边栏里必须滚。本版把它拆成两页
 * （最高的「时间」也只有 13 行），并把原来那层"一个 CSS 状态一个页签"
 * 降级成常驻的下拉框 —— 因为背景/边框挪到资源页之后，两页都受状态影响，
 * 状态选择器不能再藏在其中一页里。
 * 这是**有意偏离原厂**，见 docs/FACTORY_UI.md §17。
 *
 * 每一页 = CssProperty(该分区) + ComProperty 专有属性区的那一半。
 */
class PropertyTab : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyTab(QWidget *parent = nullptr);
    ~PropertyTab() override;

    void showNode(UiNode *n);
    /** 把 ComProperty 专有属性区的两半装进对应页签。 */
    void setDynamicSections(QWidget *basic, QWidget *resource);

    /** 当前 CSS 状态（原来的 "CSS属性_N" 的 N）。 */
    int  state() const { return m_state; }
    void setState(int s);
    /** 当前在哪一页（0=基础设置，1=资源）。 */
    int  sectionIndex() const;
    void setSectionIndex(int i);

    /** 自测：两页合起来铺了哪几组（面板对拍要合起来看，不然每页都报漏行）。 */
    QStringList rowsForTest() const;
    /** 自测：某一页铺了哪几组。 */
    QStringList rowsForTest(PropSection s) const;
    /**
     * 自测：「新建 CSS 属性」那套右键菜单**够不够得着**。
     *
     * 光测 onCopyAppendState() 能不能加状态是不够的 —— 它是直接调槽，
     * 绕过了界面。真实场景是"右键那一行"，而禁用的控件根本收不到右键事件，
     * 出过一次：只有一个状态时下拉框被置灰，菜单整个点不出来。
     */
    bool stateMenuReachableForTest() const;
    /** 自测：状态下拉框里有几条。 */
    int stateCountForTest() const;

signals:
    void nodeEdited(UiNode *n);

public slots:
    /* CSS 状态的增删。原厂挂在页签的右键菜单上（手册 2.10："右键点击菜单项的
     * CSS 属性_0，选择复制添加"）；本版页签换了含义，这套菜单跟着挪到
     * **状态下拉框**的右键上，动作和原厂一字不差。 */
    void onClearState();           ///< 清除
    void onCopyAppendState();      ///< 复制添加（复制当前项，追加到最后）
    void onCopyInsertState();      ///< 复制插入（复制当前项，插到它后面）
    void onRemoveState();          ///< 删除活动项
    /** 状态下拉框的右键菜单。 */
    void onStateContextMenu(QPoint pos);

private:
    int  stateCount() const;

    QWidget     *m_stateRow = nullptr;   ///< "CSS状态: [▾]" 那一行，右键菜单挂整行
    QComboBox   *m_stateCb = nullptr;
    QTabWidget  *m_tabs = nullptr;
    CssProperty *m_css[SecCount] = { nullptr, nullptr };
    QVBoxLayout *m_pageLay[SecCount] = { nullptr, nullptr };
    UiNode      *m_node = nullptr;
    int          m_state = 0;
};

#endif // DOCKS_H
