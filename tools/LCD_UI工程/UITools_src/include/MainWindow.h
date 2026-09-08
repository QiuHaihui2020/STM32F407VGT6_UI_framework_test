/*
 * MainWindow.h
 *
 * 【接口来源】ui-tools.exe 的 moc 元数据里 MainWindow 只暴露两个私有槽：
 *     ★ void onChangeBackgroud()                     （原程序把 Background 拼错了）
 *     ★ void onDobuleClickedImage(QListWidgetItem *)  （Double 也拼错了）
 * 两个名字都保留原样 —— 改了就对不上元数据。
 *
 * 【布局照原厂复刻】跑原厂 exe 截图比对得到：
 *   - **没有菜单栏**，只有一排"图标+文字"的工具栏按钮，末尾跟一个状态文字标签
 *       新建工程(P) 打开工程(O) 保存工程(S) 另存为(A) │ 新建页面(N) 删除当前页(D)
 *       │ 截屏(P) │ 全局设置 工程缩放 │ 关于(I)      初始化编辑环境完成
 *   - 左边两列 dock：TreeDock（结点/属性/ID号 三列树）+ 第二列（控件列表 + 属性区）
 *   - 右边一列 dock：PageView（每页原尺寸渲染 + 标题）
 *   - 中央画布，当前页**左上角对齐**（不是居中，原厂就是贴左上）
 *   - 也没有状态栏，状态文字在工具栏里
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QListWidgetItem;
class QScrollArea;
class QLabel;
class QComboBox;
class QCloseEvent;
class findDlg;
class QDockWidget;

class CanvasManager;
class TreeDock;
class PageView;
class CompoentControls;
class PropertyTab;
class ComProperty;
class BaseForm;
class UiNode;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void setToolsRoot(const QString &path);
    bool openProject(const QString &path);

    /** 自测用：走和"在对象树里点一下"完全相同的那条路，选中当前页的第 n 个节点。
     *  用来无人值守地复现选中链路上的问题（见 main.cpp 的 --click-test）。 */
    bool selectNthNodeForTest(int n);
    /** 自测/截图用：直接设画布倍率。 */
    void setCanvasZoomForTest(int percent);
    /** 诊断用：把每个控件的内容预览单独存成 PNG，看明暗/位置对不对。 */
    int dumpPreviewForTest(const QString &dir);

    /**
     * 自测用：把"操作逻辑与限制"整套跑一遍（见 main.cpp 的 --ops-test）。
     *
     * 这些规则原厂是靠弹框拦人的，弹框在无人值守下会把进程挂死，所以跑之前
     * 会打开 EditorOps 的无人值守开关：提示只记不弹，确认一律当"确认"，
     * 输入框取默认值。断言的是**被拦时的那句话**和**节点数的变化**。
     * @return 失败项数，0 表示全过；report 里是逐条结果。
     */
    int runOpsTest(QString *report);

protected:
    /** 退出前问一次（原厂："是否真的退出程序?" + 未保存提示）。 */
    void closeEvent(QCloseEvent *e) override;

private slots:
    /** 右键菜单里的"查找对像"：弹 findDlg，按名字/ID号在树里定位。 */
    void onFindObject();
    void onChangeBackgroud();                          ///< ★
    void onDobuleClickedImage(QListWidgetItem *a0);    ///< ★

    void onProjectChanged();
    void onNodeSelected(UiNode *node);
    void onStatusMessage(const QString &msg);
    /** 刷新工具栏上的画面指示。 */
    void refreshScreenLabel();
    void onDumpSty();

private:
    void refreshPropertyContext(const QString &projectJson);
    void buildToolBar();
    void buildDocks();
    void applyFactoryStyle();

    CanvasManager    *m_mgr = nullptr;
    QScrollArea      *m_scroll = nullptr;
    QWidget          *m_canvasHost = nullptr;
    TreeDock         *m_tree = nullptr;
    PageView         *m_pages = nullptr;
    QDockWidget      *m_sideDock = nullptr;    ///< 第二列：控件列表 + 属性区
    CompoentControls *m_components = nullptr;
    PropertyTab      *m_prop = nullptr;
    ComProperty      *m_com = nullptr;
    findDlg          *m_find = nullptr;        ///< 查找对像框，非模态，复用同一个
    QComboBox        *m_zoomBox = nullptr;     ///< 工具栏末尾的缩放下拉（原厂没有）
    QLabel           *m_screenLabel = nullptr; ///< 「画面 2/5 布局_11」
    QLabel           *m_status = nullptr;      ///< 工具栏末尾的状态文字
};

#endif // MAINWINDOW_H
