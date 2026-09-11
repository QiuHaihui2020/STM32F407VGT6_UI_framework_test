#include "MainWindow.h"
#include <QFileInfo>
#include <QApplication>
#include <QMouseEvent>
#include <QAbstractSpinBox>
#include <QWheelEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDir>
#include "Canvas.h"
#include "Docks.h"
#include "Forms.h"
#include "Property.h"
#include "ProjectModel.h"
#include "StyBuilder.h"
#include "StyFile.h"
#include "EditorOps.h"
#include "BuildDate.h"
#include "GlobalSettings.h"
#include "Preview.h"
#include "findDlg.h"
#include "I18nLanguage.h"
#include "ImageFileDialog.h"
#include "ImageListView.h"
#include "ToolBinWindow.h"

#include <QAction>
#include <QFrame>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QDialog>
#include <QIcon>
#include <QKeySequence>
#include <QCloseEvent>

/* 界面配色：
 *   面板绿 #C0DCC0 / 属性区 #CEE2CE / 列表内白底 #F1F1F1 / 画布灰 #F0F0F0 */
static const char *const kAppQss = R"(
QMainWindow, QMainWindow > QWidget { background: #F0F0F0; }
QDockWidget { background: #C0DCC0; }
QDockWidget > QWidget { background: #C0DCC0; }
QDockWidget::title { background: #C0DCC0; padding: 0px; max-height: 10px; }
QTreeWidget { background: #FFFFFF; border: 1px solid #9BBF9B; }
QListWidget { background: #C0DCC0; border: 1px solid #9BBF9B; }
QGroupBox {
    background: #C0DCC0;
    border: 1px solid #9BBF9B;
    margin-top: 14px;
    padding-top: 4px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    padding: 0 4px; background: #C0DCC0;
}
QScrollArea { background: #F1F1F1; border: 1px solid #9BBF9B; }
QScrollArea > QWidget > QWidget { background: #F1F1F1; }
QTabWidget::pane { background: #CEE2CE; border: 1px solid #9BBF9B; }
QTabBar::tab { background: #DCEEDC; border: 1px solid #9BBF9B; padding: 2px 8px; }
QTabBar::tab:selected { background: #CEE2CE; }
/* 工具栏：图标在上、文字在下的大按钮（版式见 temp/Snipaste_2026-09-09_08-46-05.jpg）*/
QToolBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #FBFDFF, stop:1 #E2E9F2);
    border-bottom: 1px solid #B6C4D6;
    spacing: 0px;
    padding: 1px;
}
QToolBar::separator { width: 1px; background: #C8D2DE; margin: 5px 3px; }
/* 【别给 QToolButton 加 min-width】QStyleSheetStyle 会拿它当**实际宽度**用，
 * 不是当下限：加了 min-width:52px 之后每个按钮都被压成 52+padding，标题就被
 * QCommonStylePrivate::toolButtonElideText 从中间截断成「新建…」。让 Qt 自己按文字算。 */
QToolButton { padding: 2px 5px; border: 1px solid transparent; }
QToolButton:hover { border: 1px solid #A0C0E0; background: #EAF2FB; }
QToolButton:pressed { border: 1px solid #7C9EC4; background: #D7E6F7; }
QToolButton:disabled { color: #A0A0A0; }
)";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(tr("UI编辑工具(Build:%1)").arg(common::buildDate()));
    /* 默认窗口尺寸：客户区 1687x969（工具栏 31 + 内容 969），
     * 四列 263 / 232 / 927 / 255。 */
    resize(1694, 1032);

    m_mgr = new CanvasManager(this);

    /* 中央：可滚动的画布宿主。画布贴左上角，不居中。 */
    m_canvasHost = new QWidget(this);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("centralWidget"));
    m_scroll->setWidget(m_canvasHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_scroll->setFrameShape(QFrame::NoFrame);
    setCentralWidget(m_scroll);

    buildDocks();
    buildToolBar();
    applyAppStyle();

    m_mgr->attachHost(m_canvasHost);

    connect(m_mgr, &CanvasManager::projectChanged,  this, &MainWindow::onProjectChanged);
    connect(m_mgr, &CanvasManager::pagesChanged,    this, &MainWindow::onProjectChanged);
    connect(m_mgr, &CanvasManager::currentPageChanged, this,
            [this](int) { onProjectChanged(); });
    connect(m_mgr, &CanvasManager::nodeSelected,    this, &MainWindow::onNodeSelected);
    connect(m_mgr, &CanvasManager::statusMessage,   this, &MainWindow::onStatusMessage);
    /* 右键删/粘/挪层之后，树和页面栏要跟着重来 */
    connect(m_mgr, &CanvasManager::structureChanged, this, &MainWindow::onProjectChanged);
    /* 脏标志一变就刷标题上那个 * —— 拖控件、改参数、保存，走的都是这一条 */
    connect(m_mgr, &CanvasManager::dirtyChanged, this,
            [this](bool) { refreshTitle(); });
    connect(m_mgr, &CanvasManager::findRequested,    this, &MainWindow::onFindObject);
    connect(m_mgr, &CanvasManager::controlDropped, this,
            [this](UiNode *parent, const QString &cls, const QString &type,
                   const QPoint &pos) {
                m_components->createDropped(parent, cls, type, pos);
            });
}

MainWindow::~MainWindow() = default;

void MainWindow::applyAppStyle()
{
    setStyleSheet(QLatin1String(kAppQss));
}

void MainWindow::buildDocks()
{
    /* --- 第一列：对象树 --- */
    m_tree = new TreeDock(this);
    m_tree->setManager(m_mgr);
    m_tree->setMinimumWidth(160);
    addDockWidget(Qt::LeftDockWidgetArea, m_tree);

    /* --- 第二列：控件列表（上） + 属性区（下） --- */
    m_sideDock = new QDockWidget(this);
    m_sideDock->setObjectName(QStringLiteral("SideDock"));
    m_sideDock->setWindowTitle(QString());
    m_sideDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
    m_sideDock->setMinimumWidth(160);

    auto *side = new QWidget(m_sideDock);
    auto *sideLay = new QVBoxLayout(side);
    sideLay->setContentsMargins(2, 2, 2, 2);
    sideLay->setSpacing(4);

    m_components = new CompoentControls(side);
    m_components->setManager(m_mgr);
    m_components->setFixedHeight(304);   // 实测 y=84..388
    sideLay->addWidget(m_components, 0);

    /* 属性区：ID号(ComProperty) -> CSS属性_0(PropertyTab) -> 控件专有属性
     * 上下次序就按这个来。 */
    auto *propArea = new BaseScrollArea(side);
    auto *propHost = new QWidget;
    auto *propLay = new QVBoxLayout(propHost);
    propLay->setContentsMargins(2, 2, 2, 2);
    propLay->setSpacing(3);

    m_com = new ComProperty(propHost);
    m_prop = new PropertyTab(propHost);
    /* ID号 常驻在页签之上 —— 它是控件的身份，切页签不该看不见 */
    propLay->addWidget(m_com, 0);
    propLay->addWidget(m_prop, 0);
    /* 控件专有属性区的两半分别装进「基础设置」和「资源」两页 */
    m_prop->setDynamicSections(m_com->dynamicSection(SecBasic),
                               m_com->dynamicSection(SecResource));
    propLay->addStretch(1);

    propArea->setWidget(propHost);
    sideLay->addWidget(propArea, 1);

    m_sideDock->setWidget(side);
    addDockWidget(Qt::LeftDockWidgetArea, m_sideDock);
    /* 两列并排，而不是上下堆叠 */
    splitDockWidget(m_tree, m_sideDock, Qt::Horizontal);

    /* --- 右列：页面 + 当前页的布局，两列并排 --- */
    m_pages = new PageView(this);
    m_pages->setManager(m_mgr);
    m_pages->setMinimumWidth(320);
    addDockWidget(Qt::RightDockWidgetArea, m_pages);

    /* 实测宽度：树 263 / 第二列 232 / 右栏 340（一列 255 时只放得下页面，
     * 加了布局那一列要两个 128 宽的预览并排，中间还夹了 30px 的大括号）。
     * 中间画布还剩 830 出头，
     * 128x64 的页面就算放到 400% 也才 512，够用。 */
    resizeDocks({ m_tree, m_sideDock }, { 263, 232 }, Qt::Horizontal);
    resizeDocks({ m_pages }, { 370 }, Qt::Horizontal);

    connect(m_tree, &TreeDock::nodeActivated, this, &MainWindow::onNodeSelected);
    connect(m_components, &CompoentControls::nodeCreated, this,
            [this](UiNode *) { onProjectChanged(); });
    /* 【属性面板改了也要算"工程改过"】以前这两条只刷界面，没置模型的脏标志
     * —— UiNode::markDirty() 只管到节点（够回写用），传不到 ProjectModel。
     * 而标题上那个 * 和退出时"要不要保存"两处看的都是 ProjectModel::dirty()，
     * 结果改完参数既不显示 *、退出也不提示，一不小心就白改。 */
    connect(m_prop, &PropertyTab::nodeEdited, this, [this](UiNode *) {
        if (ScenesScreen *s = m_mgr->currentScreen()) {
            s->rebuild();
        }
        m_tree->reload();
        m_pages->reload();
        m_mgr->markDirty();
    });
    connect(m_com, &BaseProperty::nodeEdited, this, [this](UiNode *) {
        m_tree->reload();
        /* 右栏那张页面图也要跟着重画 —— 改的可能就是它画出来的东西 */
        m_pages->reload();
        m_mgr->markDirty();
    });
    /* 【只重画，不置脏】"预览文字"只存在工具配置里，工程数据没动过，
     * 所以不能走 markDirty —— 否则标题平白带上 *、退出还问要不要保存。 */
    /* [全局设置]里改了点阵屏预览配色：右栏那些页面图也要重画一遍 */
    connect(m_mgr, &CanvasManager::previewStyleChanged, this, [this]() {
        m_pages->reload();
    });
    connect(m_com, &BaseProperty::previewOnlyChanged, this, [this]() {
        if (ScenesScreen *s = m_mgr->currentScreen()) {
            s->rebuild();
        }
        m_pages->reload();
    });
}

namespace {

/** 工具栏子控件里用的竖分隔线，和 QToolBar::addSeparator() 画出来的一个样。 */
QWidget *makeVSep(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QStringLiteral("color: #C8D2DE;"));
    line->setContentsMargins(4, 5, 4, 5);
    return line;
}

} // namespace

/** QComboBox 右边那个下拉箭头占的宽度，算下拉框该多宽时要加上。 */
static const int kComboArrowWidth = 22;

/** 工具栏末尾那句状态文字的宽度上限，见 buildToolBar() 里的说明。 */
static const int kStatusMaxWidth = 230;

void MainWindow::buildToolBar()
{
    auto icon = [](const char *n) {
        return QIcon(QStringLiteral(":/icon/icons/%1").arg(QLatin1String(n)));
    };

    QToolBar *tb = addToolBar(tr("主工具栏"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    /* 工具栏用"图标在上、文字在下"的大按钮，一排排到底。
     * 之前做成 16px 小图标 + 文字在右，太挤，也不好认。 */
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(28, 28));
    tb->setFloatable(false);
    tb->setMovable(true);          // 左端留一个可拖的点阵手柄
    tb->setMinimumHeight(58);      // 28 图标 + 文字 + 上下留白

    /* 工具栏按钮 */
    QAction *aNew    = tb->addAction(icon("category_vcs.png"),     tr("新建工程(P)"));
    QAction *aOpen   = tb->addAction(icon("document-open.png"),    tr("打开工程(O)"));
    QAction *aSave   = tb->addAction(icon("Save_Icon.png"),        tr("保存工程(S)"));
    QAction *aSaveAs = tb->addAction(icon("document-save-as.png"), tr("另存为(A)"));
    tb->addSeparator();
    QAction *aNewPage = tb->addAction(icon("canvas-diagram.png"),      tr("新建页面(N)"));
    QAction *aDelPage = tb->addAction(icon("removesubmitfield.png"),   tr("删除页面(D)"));
    tb->addSeparator();
    /* 【资源导出】把导出那一页直接嵌进主窗口（同一个 ToolBinWindow 类、
     * 同一条生成链），改完布局当场就能导出，不用退出去再跑一遍脚本。 */
    QAction *aExport = tb->addAction(icon("build.png"),               tr("资源导出"));
    aExport->setToolTip(QStringLiteral(
        "把当前工程导出成资源文件（project.bin / ename.h / result.bin …），"
        "点了直接跑，不弹界面。等同于 step2 那个 UIToolBin 里的「生成资源文件」。\n"
        "要改工程ID / 调用脚本 / 功能设置，走右键菜单的「资源导出设置…」"));
    aExport->setShortcut(QKeySequence(Qt::Key_F5));
    tb->addSeparator();
    QAction *aShot   = tb->addAction(icon("Screenshot.png"),          tr("截屏(P)"));
    aShot->setToolTip(QStringLiteral("截取程序的界面,并保存成PNG图片"));
    tb->addSeparator();
    QAction *aGlobal = tb->addAction(icon("preferences-system.png"),  tr("全局设置"));
    aGlobal->setToolTip(QStringLiteral("软件的全局设置,需要重启软件后生效."));
    QAction *aZoom   = tb->addAction(icon("interface.png"),           tr("工程缩放"));
    aZoom->setToolTip(QStringLiteral(
        "对当前工程的页面尺寸进行缩放,宽高最好要按比例缩放,不然会出现截断与坐标清零."));
    tb->addSeparator();
    QAction *aAbout  = tb->addAction(icon("mode_help@2x.png"),        tr("关于(I)"));

    tb->addSeparator();

    /* ---- 视图辅助开关，跟在主按钮那一排后面，同一排 ----------------------
     * 点阵屏工程 128x64 在 927px 宽的画布上就是左上角一个指甲盖，没有缩放
     * 基本没法编；一个图层下又常挂着好几个全屏尺寸的互斥布局，不做隔离就是
     * 一团糊。这几个开关是干这个用的。
     *
     * 【为什么装在一个子控件里，而不是直接 tb->addAction】第一排换成
     * "图标在上、文字在下"的大按钮之后，工具栏会把每个直属按钮都撑成那个
     * 高度和宽度，11 个主按钮 + 这几个就一千七百多像素，末尾那句状态文字
     * 直接被挤出窗口。装进一个自带 QHBoxLayout 的 QWidget 里，它们就不受
     * 工具栏的 ToolButtonStyle 管，按各自的文字宽度排，省下一半宽度，
     * 既保住了主按钮那一排的形状，也不用把任何一项挪走或藏起来。 */
    auto *viewBar = new QWidget(tb);
    /* 【必须钉死横向策略】QWidget 默认是 Preferred，QToolBarLayout 会把一排
     * 用剩的宽度全塞给它，末尾那句状态文字就被顶出窗口了。 */
    viewBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *viewLay = new QHBoxLayout(viewBar);
    viewLay->setContentsMargins(2, 0, 2, 0);
    viewLay->setSpacing(2);

    /** 紧凑小按钮：绑 QAction，勾选态/提示都跟着走。 */
    auto compactBtn = [viewBar](QAction *act) {
        auto *b = new QToolButton(viewBar);
        b->setDefaultAction(act);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        return b;
    };

    /* 「预览缩放」标题在上、百分比下拉在下，竖着排 —— 和旁边那排
     * "图标在上、文字在下"的大按钮同一个节奏，也比横着排省宽度。 */
    auto *zoomCol = new QWidget(viewBar);
    auto *zoomLay = new QVBoxLayout(zoomCol);
    zoomLay->setContentsMargins(0, 0, 0, 0);
    zoomLay->setSpacing(1);
    auto *zoomCap = new QLabel(tr("预览缩放"), zoomCol);
    zoomCap->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    /* 【两个都得定死高度】工具栏只按 sizeHint 给这一列高度，不定死的话
     * 下拉框会缩到最小高度、标题的下半截被它盖掉。 */
    zoomCap->setFixedHeight(17);
    zoomLay->addWidget(zoomCap);
    m_zoomBox = new QComboBox(zoomCol);
    m_zoomBox->setEditable(false);
    /* 宽度对齐上面那行标题的文字宽度 —— 这一列看着就是齐的一小块。
     * 别写死像素：字体换了（默认宋体 9pt，别的机器上未必）宽度要跟着走。 */
    m_zoomBox->setFixedWidth(
        zoomCap->fontMetrics().size(Qt::TextSingleLine, zoomCap->text()).width());
    m_zoomBox->setFixedHeight(22);
    /* 收窄之后"适应窗口"在收起态显示不全，但下拉列表不该跟着窄 —— 单独放宽 */
    m_zoomBox->view()->setMinimumWidth(72);
    for (int z : { 25, 50, 75, 100, 150, 200, 300, 400, 600, 800 }) {
        m_zoomBox->addItem(QStringLiteral("%1%").arg(z), z);
    }
    m_zoomBox->addItem(tr("适应窗口"), 0);
    m_zoomBox->setCurrentIndex(m_zoomBox->findData(100));
    m_zoomBox->setToolTip(tr("画布显示倍率（Ctrl+滚轮也行）。只影响显示，坐标始终按 1:1 存盘"));
    zoomLay->addWidget(m_zoomBox);
    viewLay->addWidget(zoomCol);
    connect(m_zoomBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                const int z = m_zoomBox->itemData(i).toInt();
                if (z == 0) {
                    m_mgr->zoomToFit(m_scroll->viewport()->size());
                } else {
                    m_mgr->setZoom(z);
                }
            });
    /* Ctrl+滚轮改了倍率，下拉要跟上；用 QSignalBlocker 免得又绕回去 */
    connect(m_mgr, &CanvasManager::zoomChanged, this, [this](int z) {
        const int i = m_zoomBox->findData(z);
        QSignalBlocker b(m_zoomBox);
        if (i >= 0) {
            m_zoomBox->setCurrentIndex(i);
        } else {
            m_zoomBox->setEditText(QStringLiteral("%1%").arg(z));
        }
    });

    auto *aHidden = new QAction(tr("显示隐藏项"), this);
    aHidden->setCheckable(true);
    aHidden->setToolTip(tr("连属性里标了「默认隐藏」的控件也画出来"));
    connect(aHidden, &QAction::toggled, m_mgr, &CanvasManager::setShowHidden);
    viewLay->addWidget(compactBtn(aHidden));

    auto *aSolo = new QAction(tr("单独预览"), this);
    aSolo->setCheckable(true);
    aSolo->setChecked(true);
    aSolo->setToolTip(tr("只画选中项所属的那一个顶层布局。这套工程一页里有 1~9 个"
                         "整屏布局互相盖死，不这么干什么都看不清"));
    connect(aSolo, &QAction::toggled, m_mgr, &CanvasManager::setSolo);
    viewLay->addWidget(compactBtn(aSolo));

    auto *aChrome = new QAction(tr("隐藏辅助线"), this);
    aChrome->setCheckable(true);
    aChrome->setToolTip(tr("把编辑器自己画的控件描边、选中框、控件名和像素网格"
                           "全藏起来，画布上只剩屏上真会显示的像素。"
                           "放大看真实效果时用，藏起来就点不准控件了，看完记得关掉"));
    connect(aChrome, &QAction::toggled, this,
            [this](bool on) { m_mgr->setShowChrome(!on); });
    viewLay->addWidget(compactBtn(aChrome));

    /* 画面导航：单独预览一次只看一个，得能快速切，不然找"音量界面在哪个布局"
     * 要去树里一个个猜。版式和上面的「预览缩放」一致 —— 标题在上、下拉在下。
     * 【没有左右箭头】下拉本身就能直接跳到任意一个，箭头是多余的一步；
     * CanvasManager::stepScreen() 保留（--ops-test 里在测它，内部走 gotoScreen）。 */
    viewLay->addWidget(makeVSep(viewBar));

    auto *screenCol = new QWidget(viewBar);
    auto *screenLay = new QVBoxLayout(screenCol);
    screenLay->setContentsMargins(0, 0, 0, 0);
    screenLay->setSpacing(1);
    m_screenCap = new QLabel(tr("当前画面"), screenCol);
    m_screenCap->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_screenCap->setFixedHeight(17);
    screenLay->addWidget(m_screenCap);
    m_screenBox = new QComboBox(screenCol);
    m_screenBox->setEditable(false);
    m_screenBox->setFixedHeight(22);
    m_screenBox->setToolTip(tr("当前页里的顶层布局。选一个就等于预览它，"
                               "和左右两个箭头是同一件事"));
    screenLay->addWidget(m_screenBox);
    viewLay->addWidget(screenCol);
    connect(m_screenBox, QOverload<int>::of(&QComboBox::activated),
            m_mgr, &CanvasManager::gotoScreen);
    connect(m_mgr, &CanvasManager::screenListChanged, this, &MainWindow::refreshScreenLabel);


    tb->addWidget(viewBar);

    /* 状态文字挂在工具栏末尾，不做独立状态栏 */
    m_status = new QLabel(tr("初始化编辑环境完成"), tb);
    m_status->setContentsMargins(10, 0, 6, 0);
    m_status->setStyleSheet(QStringLiteral("color: #1A4FA0;"));
    /* 【必须限宽】状态文字长短不定。一旦这一排的合计 sizeHint 超出窗口宽度，
     * QToolBarLayout 就把末尾的它整个收进 >> 溢出菜单，界面上一个字都看不见。
     *
     * 不能用 QSizePolicy::Ignored 来"有多少用多少"—— QWidgetItem::sizeHint()
     * 遇到 Ignored 直接把宽度报 0，标签会被压成零宽，同样什么都看不见。
     * 正确做法是设 maximumWidth：QWidgetItem::sizeHint() 会按 maximumSize 截顶，
     * 于是这一项的宽度有上界，排得下；超长的文字在 onStatusMessage 里加省略号。 */
    m_status->setMinimumWidth(0);
    m_status->setMaximumWidth(kStatusMaxWidth);
    tb->addWidget(m_status);

    aNew->setShortcut(QKeySequence::New);
    aOpen->setShortcut(QKeySequence::Open);
    aSave->setShortcut(QKeySequence::Save);

    connect(aNew,     &QAction::triggered, m_mgr, &CanvasManager::onCreateNewProject);
    connect(aOpen,    &QAction::triggered, m_mgr, &CanvasManager::onOpenProject);
    connect(aSave,    &QAction::triggered, m_mgr, &CanvasManager::onSaveProject);
    connect(aSaveAs,  &QAction::triggered, m_mgr, &CanvasManager::onSaveAsProject);
    connect(aExport,  &QAction::triggered, this,  &MainWindow::onExportResource);
    connect(aShot,    &QAction::triggered, m_mgr, &CanvasManager::onSshoot);
    connect(aGlobal,  &QAction::triggered, m_mgr, &CanvasManager::onGlobalBtn);
    connect(aZoom,    &QAction::triggered, m_mgr, &CanvasManager::onZoomProject);
    connect(aAbout,   &QAction::triggered, m_mgr, &CanvasManager::onAboutBtn);
    /* 这两个是 private slot，用字符串连接走 moc 元调用 */
    connect(aNewPage, SIGNAL(triggered()), m_mgr, SLOT(onCreateNewScenesScreen()));
    connect(aDelPage, SIGNAL(triggered()), m_mgr, SLOT(onDelCurrentScenesScreen()));

    /* 不常用的入口放右键菜单里，不占工具栏的位置 */
    addAction(aNew);
    auto *aDump = new QAction(tr("解析 .sty 并校验往返"), this);
    connect(aDump, &QAction::triggered, this, &MainWindow::onDumpSty);
    addAction(aDump);
    auto *aBg = new QAction(tr("画布背景色…"), this);
    connect(aBg, &QAction::triggered, this, &MainWindow::onChangeBackgroud);
    addAction(aBg);
    auto *aGrid = new QAction(tr("网格开关"), this);
    connect(aGrid, &QAction::triggered, m_mgr, &CanvasManager::onSelectGrid);
    addAction(aGrid);
    auto *aExportUi = new QAction(tr("资源导出设置…"), this);
    aExportUi->setToolTip(tr("弹 UIToolBin 那一页：工程ID / 不重新生成资源 / "
                             "调用脚本 / 旋转 / 版本配置 / 功能设置"));
    connect(aExportUi, &QAction::triggered, this, &MainWindow::onExportResourceDialog);
    addAction(aExportUi);
    auto *aConf = new QAction(tr("工程配置…"), this);
    connect(aConf, SIGNAL(triggered()), m_mgr, SLOT(onConfProject()));
    addAction(aConf);
    setContextMenuPolicy(Qt::ActionsContextMenu);
}

void MainWindow::setToolsRoot(const QString &path)
{
    m_mgr->setToolsRoot(path);
    /* "保存成控件"落到这里，ControlLibrary 下次启动也从这里扫，
     * 两边指同一个目录才不会存了看不见。 */
    EditorOps::setCustomWidgetDir(QDir(path).filePath(QStringLiteral("control/ex")));
    m_components->reload();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    /* 退出前也要提交：不然"改完直接点叉"会把改动连同"要不要保存"的提示
     * 一起吞掉 —— 模型没被标脏，工具会以为没什么可存的。 */
    EditorOps::commitPendingEdit();

    /* ★ 退出要问两次：先"是否真的退出程序?"，再问没保存的改动。
     * 之前是直接关，改了一下午的东西点个叉就没了。 */
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("退出程序"));
    box.setText(QStringLiteral("是否真的退出程序?"));
    QAbstractButton *quit = box.addButton(QStringLiteral("退出"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != quit || !m_mgr->confirmDiscardChanges()) {
        e->ignore();
        return;
    }
    e->accept();
}

void MainWindow::onFindObject()
{
    if (!m_find) {
        m_find = new findDlg(this);
        connect(m_find, &findDlg::findNext, this, [this](const QString &kw) {
            UiNode *hit = nullptr;
            for (UiNode *page : m_mgr->model()->pages()) {
                page->forEach([&](UiNode *n) {
                    if (hit) {
                        return false;
                    }
                    if (n->name.compare(kw, Qt::CaseInsensitive) == 0) {
                        hit = n;
                        return false;
                    }
                    for (const UiProperty &pr : n->props) {
                        if (pr.name == QLatin1String("id")
                            && pr.ename.compare(kw, Qt::CaseInsensitive) == 0) {
                            hit = n;
                            return false;
                        }
                    }
                    return true;
                });
                if (hit) {
                    break;
                }
            }
            if (!hit) {
                /* 提示语原文，前面那个空格是有意留的 */
                QMessageBox::warning(this, QStringLiteral("警告"),
                                     kw + QStringLiteral(" 不存在,或者已经被删除,或者已经被重命名."));
                return;
            }
            onNodeSelected(hit);
        });
    }
    m_find->show();
    m_find->raise();
    m_find->activateWindow();
}

bool MainWindow::openProject(const QString &path)
{
    QString err;
    if (!m_mgr->openProject(path, &err)) {
        QMessageBox::warning(this, tr("打开失败"), err);
        return false;
    }
    refreshPropertyContext(path);
    return true;
}

/* 属性面板里"选图片 / 选文字 / 事件动作"三个子对话框要的上下文。
 * 图片路径要存成相对工程目录的形式，文字列表要读多国语言表，
 * 事件的"对象"下拉要全工程的 ename —— 都在这里一次性备好。 */
void MainWindow::refreshPropertyContext(const QString &projectJson)
{
    PropertyContext &ctx = PropertyContext::instance();
    ctx.projectDir = QFileInfo(projectJson).absolutePath();

    /* 多国语言表：工程里记了就用工程的，否则按约定的目录结构去
     * <工程>/../../../UITools/ 下找唯一一个 .xls */
    QString xls = m_mgr->model()->langExcel();
    if (!xls.isEmpty() && !QFileInfo(xls).isAbsolute()) {
        xls = QDir(ctx.projectDir).absoluteFilePath(xls);
    }
    if (xls.isEmpty() || !QFileInfo::exists(xls)) {
        const QString ut = QDir::cleanPath(
            QDir(ctx.projectDir).absoluteFilePath(QStringLiteral("../../../UITools")));
        const QStringList found = QDir(ut).entryList(QStringList{ QStringLiteral("*.xls") },
                                                     QDir::Files);
        xls = found.isEmpty() ? QString() : QDir(ut).absoluteFilePath(found.first());
    }
    ctx.excelPath = QFileInfo::exists(xls) ? xls : QString();

    /* 内容预览要用到：图片路径相对工程目录，文字要查多国语言表，
     * 字体在工程的 Resbuilder.xml 里。 */
    Preview::setProject(ctx.projectDir, ctx.excelPath);

    ctx.objectNames.clear();
    for (UiNode *page : m_mgr->model()->pages()) {
        page->forEach([&ctx](UiNode *n) {
            for (const UiProperty &p : n->props) {
                if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
                    ctx.objectNames.append(p.ename);
                }
            }
            return true;
        });
    }
    ctx.objectNames.sort();
}

void MainWindow::onProjectChanged()
{
    m_tree->reload();
    m_pages->reload();
    refreshScreenLabel();
    refreshTitle();
}

void MainWindow::refreshTitle()
{
    /* 标题：UI编辑工具(Build:<构建日期>) <工程名>；改过没存就带一个 *。
     * 构建日期用 __DATE__，编译那天定死，不是运行时的今天 —— 这样用户报
     * 问题时报的标题就能对上是哪个版本。 */
    setWindowTitle(tr("UI编辑工具(Build:%1) %2%3")
                   .arg(common::buildDate(), m_mgr->model()->name(),
                        m_mgr->model()->dirty() ? QStringLiteral(" *") : QString()));
}

void MainWindow::setShowChromeForTest(bool on)
{
    m_mgr->setShowChrome(on);
}

void MainWindow::setCanvasZoomForTest(int percent)
{
    m_mgr->setZoom(percent);
}

int MainWindow::dumpPreviewForTest(const QString &dir)
{
    QDir().mkpath(dir);
    int n = 0;
    for (UiNode *pg : m_mgr->model()->pages()) {
        pg->forEach([&](UiNode *x) {
            /* 亮色故意用洋红：白底黑底都看得出来，一眼能判断明暗有没有反 */
            const QPixmap pm = Preview::contentOf(x, QColor(255, 0, 255));
            if (!pm.isNull()) {
                pm.save(QDir(dir).filePath(QStringLiteral("%1_%2_%3.png")
                                           .arg(n, 3, 10, QLatin1Char('0'))
                                           .arg(x->type, x->name)));
                ++n;
            }
            return true;
        });
    }

    /* 顺手把"分隔符不够"的警告气泡也出一张 —— 改过气泡的画法之后，"没崩"
     * 不等于"没画坏"（尾巴指偏、文字被裁都不会崩），得能看一眼。
     * 挑一个时间控件，临时把格式改成要 2 张分隔符的，出完图再改回去。 */
    for (UiNode *pg : m_mgr->model()->pages()) {
        UiNode *tm = nullptr;
        pg->forEach([&](UiNode *x) {
            if (!tm && x->type == QLatin1String("Time")) {
                tm = x;
            }
            return tm == nullptr;
        });
        if (!tm) {
            continue;
        }
        QString keep;
        for (UiProperty &q : tm->props) {
            if (q.name == QLatin1String("format")) {
                keep = q.raw.value(QStringLiteral("default")).toString();
                q.raw.insert(QStringLiteral("default"), QStringLiteral("h:m:s"));
            }
        }
        m_com->showNode(tm);
        const QPixmap tip = m_com->warningTipPixmapForTest();
        if (!tip.isNull()) {
            tip.save(QDir(dir).filePath(QStringLiteral("zz_warning_tip.png")));
            ++n;
        }
        for (UiProperty &q : tm->props) {
            if (q.name == QLatin1String("format")) {
                q.raw.insert(QStringLiteral("default"), keep);
            }
        }
        m_com->showNode(tm);
        break;
    }
    return n;
}

bool MainWindow::selectNthNodeForTest(int n)
{
    ScenesScreen *s = m_mgr->currentScreen();
    if (!s) {
        return false;
    }
    QVector<UiNode *> all;
    for (UiNode *page : m_mgr->model()->pages()) {
        page->forEach([&all, page](UiNode *x) {
            if (x != page) {
                all.append(x);
            }
            return true;
        });
    }
    if (n < 0 || n >= all.size()) {
        return false;
    }
    UiNode *node = all.at(n);

    /* 【先翻到它所在的页】节点是跨页收集的，画布却只显示当前页。不翻页的话
     * 选中的是别页的节点、画布上什么都不会变 —— 之前拿 --select 截图排查
     * 问题，一直截到的是第 1 页，白看半天。 */
    for (UiNode *a = node; a; a = a->parent) {
        if (!a->parent) {                       // 走到页节点
            const int pi = m_mgr->model()->pages().indexOf(a);
            if (pi >= 0 && pi != m_mgr->currentPage()) {
                m_mgr->setCurrentPage(pi);
                s = m_mgr->currentScreen();
                if (!s) {
                    return false;
                }
            }
            break;
        }
    }

    // 路径一：在对象树里点一下（TreeDock::onItemPressed 就是这一句）
    s->selectNode(node);

    // 路径二：在画布上直接点那个控件 —— 发真的鼠标事件，把
    // BaseForm::mousePressEvent + ScenesScreen::eventFilter 一起覆盖到
    if (BaseForm *f = s->formFor(node)) {
        const QPoint c(f->width() / 2, f->height() / 2);
        QMouseEvent press(QEvent::MouseButtonPress, c, f->mapToGlobal(c),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, c, f->mapToGlobal(c),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(f, &press);
        QApplication::sendEvent(f, &release);
    }
    return true;
}

/* ===================== --make-sample ===================== */

int MainWindow::makeSampleProject(const QString &path, const QString &picDir,
                                  QString *report)
{
    QStringList log;
    EditorOps::setSilent(true);

    /* 【边跑边落盘的脚印】这套流程要连着调几十次建控件，中途炸了的话
     * 报告根本回不来。每一步立刻 append 到 <out>.trace，崩了也知道停在哪。 */
    const QString tracePath = path + QStringLiteral(".trace");
    QFile::remove(tracePath);
    auto trace = [&tracePath](const QString &m) {
        QFile f(tracePath);
        if (f.open(QIODevice::Append | QIODevice::WriteOnly)) {
            f.write(m.toUtf8());
            f.putChar('\n');
        }
    };
    trace(QStringLiteral("start"));

    /* 工程目录得先有，控件里引用的图片按相对这个目录算 */
    const QString projDir = QFileInfo(path).absolutePath();
    QDir().mkpath(projDir);

    /* 找几张真图 —— 图片类控件不给图，生成出来是空的，验不到像素那一段 */
    QStringList pics;
    {
        QDir pd(QDir(projDir).filePath(picDir));
        const QStringList filters = { QStringLiteral("*.bmp"), QStringLiteral("*.png") };
        for (const QFileInfo &fi : pd.entryInfoList(filters, QDir::Files, QDir::Name)) {
            pics << QDir(projDir).relativeFilePath(fi.absoluteFilePath());
            if (pics.size() >= 8) {
                break;
            }
        }
    }
    log << QStringLiteral("图片素材 %1 张（目录 %2）").arg(pics.size()).arg(picDir);

    trace(QStringLiteral("newProject"));
    m_mgr->newProjectForTest(QStringLiteral("AllCtrl"), QSize(128, 64));
    trace(QStringLiteral("newProject ok, %1 页").arg(m_mgr->model()->pages().size()));

    auto firstOf = [](UiNode *pg, const char *cls) -> UiNode * {
        UiNode *found = nullptr;
        if (!pg) {
            return nullptr;
        }
        pg->forEach([&](UiNode *x) {
            if (!found && x->cls == QLatin1String(cls)) {
                found = x;
            }
            return found == nullptr;
        });
        return found;
    };
    /* 【新建的页面是空的】「新建页面」只建页节点，图层和布局要自己加
     * （新建图层是"永远加到当前页"，和选中什么无关）。
     * 这里照着补齐，否则后面拿 firstLayout() 会拿到空指针。 */
    auto pageOf = [&](int i) -> UiNode * {
        while (m_mgr->model()->pages().size() <= i) {
            m_mgr->addPageForTest();
        }
        m_mgr->setCurrentPage(i);
        UiNode *pg = m_mgr->model()->pages().at(i);
        if (!firstOf(pg, "NewLayer")) {
            onNodeSelected(pg);
            m_components->onCreateNewLayer();
            if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
        }
        UiNode *la = firstOf(pg, "NewLayer");
        if (la && !firstOf(pg, "NewLayout")) {
            onNodeSelected(la);
            m_components->onCreateNewLayout();
            if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
        }
        return pg;
    };
    auto firstLayout = [&](UiNode *pg) -> UiNode * {
        return firstOf(pg, "NewLayout");
    };
    int picCursor = 0;
    auto dressImage = [&](UiNode *n) {
        if (pics.isEmpty()) {
            return;
        }
        const QString rel = pics.at(picCursor++ % pics.size());
        for (UiProperty &p : n->props) {
            if (p.name != QLatin1String("normal_image")
                && p.name != QLatin1String("image")) {
                continue;
            }
            QJsonArray lst;
            lst.append(rel);
            p.raw.insert(QStringLiteral("list"), lst);
            p.dirty = true;
            n->markDirty();
        }
    };
    auto place = [](UiNode *n, int x, int y, int w, int h) {
        n->rect = QRect(x, y, w, h);
        n->setRectOf(0, n->rect);
        n->markDirty();
    };

    const ControlLibrary *lib = m_mgr->library();

    trace(QStringLiteral("phase0"));
    /* ---- 页 0：每种控件各来一个，平铺 ---- */
    {
        UiNode *pg = pageOf(0);
        UiNode *lo = firstLayout(pg);
        if (!lo) {
            *report = QStringLiteral("新建工程没有布局，造不下去");
            return 0;
        }
        int x = 0, y = 0;
        for (const ControlTemplate &t : lib->controls()) {
            if (t.type == QLatin1String("NewLayer")
                || t.type == QLatin1String("NewLayout")) {
                continue;
            }
            onNodeSelected(lo);
            const int was = lo->children.size();
            m_components->createControl(t.cls, t.type, t.caption);
            if (lo->children.size() != was + 1) {
                log << QStringLiteral("× 建不出 %1").arg(t.caption);
                continue;
            }
            UiNode *n = lo->children.last().second;
            place(n, x, y, 16, 16);
            dressImage(n);
            trace(QStringLiteral("  页0 建了 %1").arg(t.type));
            log << QStringLiteral("页0 %1 (%2)").arg(t.caption, t.type);
            x += 16;
            if (x >= 128) {
                x = 0;
                y += 16;
            }
        }
    }

    trace(QStringLiteral("phase1"));
    /* ---- 页 1：容器组合 —— 列表/表格带项，项里再放控件 ---- */
    {
        UiNode *pg = pageOf(1);
        UiNode *lo = firstLayout(pg);
        if (!lo) {
            log << QStringLiteral("× 页1 没有布局，跳过容器组合");
        }
        struct BoxCase { QString cls; QString type; int y; int h; };
        const QVector<BoxCase> boxes = {
            { QStringLiteral("NewList"), QStringLiteral("VerticalList"), 0, 32 },
            { QStringLiteral("NewList"), QStringLiteral("HorizontalList"), 32, 16 },
            { QStringLiteral("NewGrid"), QStringLiteral("NewGrid"), 48, 16 },
        };
        for (const BoxCase &b : boxes) {
            if (!lo) {
                break;
            }
            onNodeSelected(lo);
            const int boxWas = lo->children.size();
            m_components->createDropped(lo, b.cls, b.type, QPoint(0, b.y));
            if (lo->children.size() != boxWas + 1) {
                log << QStringLiteral("× 建不出 %1").arg(b.type);
                continue;
            }
            UiNode *box = lo->children.last().second;
            place(box, 0, b.y, 128, b.h);
            if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }

            for (int i = 0; i < 2; ++i) {
                onNodeSelected(box);
                m_components->onCreateNewLayout();
                if (box->children.size() != i + 1) {
                    log << QStringLiteral("× %1 加不出第 %2 项").arg(b.type).arg(i);
                    break;
                }
                UiNode *item = box->children.last().second;
                if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
                /* 每一项里放一张图 + 一段文字 */
                onNodeSelected(item);
                m_components->createControl(QStringLiteral("NewFrame"),
                                            QStringLiteral("ImageList"),
                                            QStringLiteral("图片"));
                if (!item->children.isEmpty()) {
                    UiNode *im = item->children.last().second;
                    place(im, 0, 0, qMin(16, item->rect.width()),
                          qMin(16, item->rect.height()));
                    dressImage(im);
                }
                if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
                onNodeSelected(item);
                m_components->createControl(QStringLiteral("NewFrame"),
                                            QStringLiteral("Text"),
                                            QStringLiteral("文字"));
                if (item->children.size() >= 2) {
                    UiNode *tx = item->children.last().second;
                    place(tx, qMin(16, item->rect.width() - 1), 0,
                          qMax(1, item->rect.width() - 16),
                          qMin(16, item->rect.height()));
                }
                if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
            }
            trace(QStringLiteral("  页1 %1 完").arg(b.type));
            log << QStringLiteral("页1 %1 带 %2 项").arg(b.type)
                   .arg(box->children.size());
        }
    }

    trace(QStringLiteral("phase2"));
    /* ---- 页 2：布局套布局 + 一堆控件混排 ---- */
    {
        UiNode *pg = pageOf(2);
        UiNode *lo = firstLayout(pg);
        if (!lo) {
            log << QStringLiteral("× 页2 没有布局，跳过");
            *report = log.join(QChar(QLatin1Char('\n')));
            return 0;
        }
        onNodeSelected(lo);
        m_components->onCreateNewLayout();             // 布局套布局
        UiNode *inner = lo->children.isEmpty() ? nullptr : lo->children.last().second;
        if (inner) {
            place(inner, 0, 0, 128, 32);
            if (ScenesScreen *ss = m_mgr->currentScreen()) { ss->rebuild(); }
            int x = 0;
            for (const ControlTemplate &t : lib->controls()) {
                if (t.cls != QLatin1String("NewFrame")) {
                    continue;                          // 只放叶子控件
                }
                onNodeSelected(inner);
                const int was = inner->children.size();
                m_components->createControl(t.cls, t.type, t.caption);
                if (inner->children.size() != was + 1) {
                    continue;
                }
                UiNode *n = inner->children.last().second;
                place(n, x, 0, 16, 16);
                dressImage(n);
                x = (x + 16) % 128;
            }
            log << QStringLiteral("页2 嵌套布局带 %1 个控件")
                   .arg(inner->children.size());
        }
        /* 同一个布局里再并排放一批，测"多个控件组合" */
        int x2 = 0;
        for (int round = 0; round < 2; ++round) {
            for (const ControlTemplate &t : lib->controls()) {
                if (t.cls != QLatin1String("NewFrame")) {
                    continue;
                }
                onNodeSelected(lo);
                const int was = lo->children.size();
                m_components->createControl(t.cls, t.type, t.caption);
                if (lo->children.size() != was + 1) {
                    continue;
                }
                UiNode *n = lo->children.last().second;
                place(n, x2, 32 + round * 16, 16, 16);
                dressImage(n);
                x2 = (x2 + 16) % 128;
            }
        }
        log << QStringLiteral("页2 布局共 %1 个孩子").arg(lo->children.size());
    }

    trace(QStringLiteral("save"));
    int nodes = 0;
    for (UiNode *pg : m_mgr->model()->pages()) {
        pg->forEach([&nodes](UiNode *) { ++nodes; return true; });
    }

    QString err;
    if (!m_mgr->model()->save(path, &err)) {
        *report = QStringLiteral("存不下来: %1").arg(err);
        return 0;
    }
    log << QStringLiteral("共 %1 个节点，已写入 %2").arg(nodes).arg(path);
    *report = log.join(QLatin1Char('\n'));
    EditorOps::setSilent(false);
    return nodes;
}

/* ===================== --ops-test ===================== */

namespace {

int countNodes(UiNode *page)
{
    int n = 0;
    if (page) {
        page->forEach([&n](UiNode *) { ++n; return true; });
    }
    return n - 1;                       // 页节点自己不算
}

/** 在页里找第一个 -class 等于 cls 的节点。 */
UiNode *firstOfClass(UiNode *page, const char *cls)
{
    UiNode *hit = nullptr;
    if (page) {
        page->forEach([&](UiNode *n) {
            if (!hit && n->cls == QLatin1String(cls)) {
                hit = n;
            }
            return hit == nullptr;
        });
    }
    return hit;
}

/** n 在父节点 children 里的下标。 */
int indexInParent(UiNode *n)
{
    if (!n || !n->parent) {
        return -1;
    }
    for (int i = 0; i < n->parent->children.size(); ++i) {
        if (n->parent->children.at(i).second == n) {
            return i;
        }
    }
    return -1;
}

} // namespace

int MainWindow::runOpsTest(QString *report)
{
    EditorOps::setSilent(true);

    QStringList log;
    int fail = 0;
    auto check = [&](const QString &what, bool ok, const QString &detail = QString()) {
        log << QStringLiteral("%1 %2%3")
               .arg(ok ? QStringLiteral("[通过]") : QStringLiteral("[失败]"), what,
                    detail.isEmpty() ? QString() : QStringLiteral("  —— ") + detail);
        if (!ok) {
            ++fail;
        }
    };

    /* 【自测不许弄脏工程的配置】ui-config 现在跟着工程走（见 §18），
     * 测试里改预览文字、改配色会直接写进工程目录下那个**受版本管理**的文件。
     * 这里整体重定向到临时沙箱，跑完再指回去。真实路径先记下来，18z 要用它
     * 断言"配置确实落在工程目录下"。 */
    const QString kRealCfgPath = GlobalSettings::filePath();
    const QString kProjCfgDir =
        QFileInfo(m_mgr->model()->filePath()).absolutePath();
    const QString kCfgSandbox = QDir::temp().filePath(QStringLiteral("uitools_ops_cfg"));
    QDir(kCfgSandbox).removeRecursively();
    QDir().mkpath(kCfgSandbox);
    GlobalSettings::setProjectDir(kCfgSandbox);

    /* 【别死盯当前页】activePage 是工程里存的，用户上次停在哪一页就是哪一页 ——
     * 新建一个空页面存盘之后，当前页就是那个空页，整套测试直接报"缺样本"。
     * 这里挑第一个凑齐图层/布局/控件样本的页来测。 */
    ScenesScreen *sc = nullptr;
    UiNode *page = nullptr;
    UiNode *layer = nullptr, *layout = nullptr, *frame = nullptr, *list = nullptr;
    for (int pi = 0; pi < m_mgr->model()->pages().size(); ++pi) {
        ScenesScreen *cand = m_mgr->screen(pi);
        if (!cand || !cand->page()) {
            continue;
        }
        UiNode *pg = cand->page();
        UiNode *la = firstOfClass(pg, "NewLayer");
        UiNode *lo = firstOfClass(pg, "NewLayout");
        UiNode *fr = firstOfClass(pg, "NewFrame");
        if (!la || !lo || !fr) {
            continue;
        }
        m_mgr->setCurrentPage(pi);
        sc = cand;
        page = pg;
        layer = la;
        layout = lo;
        frame = fr;
        list = firstOfClass(pg, "NewList");
        break;
    }
    if (!sc) {
        *report = QStringLiteral("工程里没有一页凑齐图层/布局/控件样本，测不了");
        return 1;
    }

    /* --- 1. 建控件：选中控件时加到它的兄弟位置 ---
     * 规则：选中是 NewFrame/NewList/NewGrid 时，容器取 sel 的**父级**。
     * 一度写成"直接拦下报错"，那样太严，正常操作也被挡掉了。 */
    int before = countNodes(page);
    UiNode *const frameHost = frame->parent;
    const int hostKidsWas = frameHost ? frameHost->children.size() : -1;
    onNodeSelected(frame);                       // 选中的是控件，不是布局
    EditorOps::clearLastMessage();
    m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                QStringLiteral("文字"));
    check(QStringLiteral("选中控件时建控件：建得出来（不拦）"),
          countNodes(page) == before + 1
          && EditorOps::lastMessage().isEmpty(),
          EditorOps::lastMessage());
    check(QStringLiteral("选中控件时建控件：加到它的父级，不是钻进控件里"),
          frameHost && frameHost->children.size() == hostKidsWas + 1
          && frame->children.isEmpty(),
          QStringLiteral("父级 %1 -> %2，控件自己 %3 个孩子")
          .arg(hostKidsWas).arg(frameHost ? frameHost->children.size() : -1)
          .arg(frame->children.size()));

    /* 选中图层建控件才是真拦 —— isClass("NewLayer") 判死 */
    before = countNodes(page);
    onNodeSelected(layer);
    EditorOps::clearLastMessage();
    m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                QStringLiteral("文字"));
    check(QStringLiteral("选中图层时建控件被拦下"),
          countNodes(page) == before
          && EditorOps::lastMessage() == QStringLiteral("请选择一个布局或者新建一个并选中它."),
          EditorOps::lastMessage());

    /* --- 2. 选中布局就能建，而且键/属性要对 --- */
    onNodeSelected(layout);
    EditorOps::clearLastMessage();
    const int kidsBefore = layout->children.size();
    m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                QStringLiteral("文字"));
    const bool grew = layout->children.size() == kidsBefore + 1;
    UiNode *made = grew ? layout->children.last().second : nullptr;
    check(QStringLiteral("选中布局后能建控件"), grew);
    check(QStringLiteral("新控件挂在 layout 键下（不是 widget）"),
          grew && layout->children.last().first == QLatin1String("layout"),
          grew ? layout->children.last().first : QString());
    check(QStringLiteral("新控件带着模板的 element_css"),
          made && made->cssStateCount() > 0);

    /* --- 3. 建布局：图层下、布局里套一层，都行 ---
     * 规则：NewLayout 和 NewLayer 都是"挂到自己"，NewFrame/NewList 挂到
     * 父级，其余静默。一度写成"只能挂图层下"，于是布局套布局建不出来
     * —— 而既有工程里就有 3 例这种嵌套。 */
    before = countNodes(page);
    const int layoutKidsWas = layout->children.size();
    onNodeSelected(layout);                      // 选中的是布局
    EditorOps::clearLastMessage();
    m_components->onCreateNewLayout();
    check(QStringLiteral("选中布局建布局：套在它里面（既有工程里有 3 例这种嵌套）"),
          countNodes(page) == before + 1
          && layout->children.size() == layoutKidsWas + 1,
          EditorOps::lastMessage());

    before = countNodes(page);
    onNodeSelected(layer);
    EditorOps::clearLastMessage();
    m_components->onCreateNewLayout();
    check(QStringLiteral("选中图层后能建布局"), countNodes(page) == before + 1);

    /* --- 4. 复制/粘贴的容器限制 --- */
    sc->rebuild();
    EditorOps::copyToClip(frame);
    BaseForm *frameForm = sc->formFor(frame);
    check(QStringLiteral("画布上找得到样本控件"), frameForm != nullptr);
    if (frameForm) {
        before = countNodes(page);
        EditorOps::clearLastMessage();
        frameForm->doPaste();                    // 往控件上贴
        check(QStringLiteral("往非布局上粘贴被拦下"),
              countNodes(page) == before
              && EditorOps::lastMessage()
                 == QStringLiteral("当前的选中的对像不支持剪切板里的对像粘贴,请选择一个<布局>对像."),
              EditorOps::lastMessage());
    }
    if (BaseForm *layoutForm = sc->formFor(layout)) {
        const int n0 = layout->children.size();
        layoutForm->doPaste();                   // 往布局上贴
        check(QStringLiteral("往布局上粘贴成功"), layout->children.size() == n0 + 1);
    }

    /* --- 5. Z 序 --- */
    sc->rebuild();
    UiNode *z = layout->children.isEmpty() ? nullptr : layout->children.first().second;
    if (z && layout->children.size() >= 2) {
        EditorOps::moveZ(z, EditorOps::ZTop);
        check(QStringLiteral("移到顶层"), indexInParent(z) == layout->children.size() - 1,
              QStringLiteral("下标=%1").arg(indexInParent(z)));
        EditorOps::moveZ(z, EditorOps::ZBottom);
        check(QStringLiteral("移到底层"), indexInParent(z) == 0);
        EditorOps::moveZ(z, EditorOps::ZUp);
        check(QStringLiteral("移上一层"), indexInParent(z) == 1);
        EditorOps::moveZ(z, EditorOps::ZDown);
        check(QStringLiteral("移下一层"), indexInParent(z) == 0);
    } else {
        check(QStringLiteral("Z 序"), false, QStringLiteral("样本不足"));
    }

    /* --- 6. 删除 --- */
    sc->rebuild();
    if (!layout->children.isEmpty()) {
        UiNode *victim = layout->children.last().second;
        const int n0 = layout->children.size();
        if (BaseForm *f = sc->formFor(victim)) {
            f->onDeleteMe();
            check(QStringLiteral("删除（确认框在无人值守下按<删除>算）"),
                  layout->children.size() == n0 - 1);
        } else {
            check(QStringLiteral("删除"), false, QStringLiteral("画布上找不到它"));
        }
    }

    /* 【补一次事件循环】上面那串增删操作用的是 deleteLater()，真实交互里
     * 事件循环一直在转、旧对象立刻就回收了；--ops-test 是一口气跑完的，
     * 不补这一下，后面抓画布时旧控件还堆着。 */
    QApplication::processEvents();

    /* --- 18e. 「隐藏辅助线」要真的把辅助线藏掉 ---
     * 判据：关掉之后整页画出来的像素**只能是**点亮色或熄灭色 —— 虚线描边是
     * 半透明白、选中框和 8 个缩放手柄是蓝色、像素网格是灰，都不属于这两种。
     * 开着的时候则必然有别的颜色。
     *
     * 【抓整页，别抓单个 BaseForm】BaseForm 没开 autoFillBackground，
     * QWidget::grab() 不会先清底，没画到的地方是未初始化内存 —— 拿它算像素
     * 等于测垃圾，开关翻不翻数字都一样。ScenesScreen 会 fillRect(m_bg)。
     * 页面四周那圈 QFrame::Box 边框不是内容，往里缩 2px 避开。 */
    if (ScenesScreen *sc2 = m_mgr->currentScreen()) {
        auto offPalette = [](const QImage &im) {
            const QRgb l = Preview::monoLit().rgb();
            const QRgb d = Preview::monoDark().rgb();
            int n = 0;
            for (int y = 2; y < im.height() - 2; ++y) {
                for (int x = 2; x < im.width() - 2; ++x) {
                    const QRgb c = im.pixel(x, y) | 0xFF000000u;
                    if (c != l && c != d) {
                        ++n;
                    }
                }
            }
            return n;
        };
        /* 【别在这儿改倍率】setZoom() 会让每一页 rebuild()，把所有 BaseForm
         * 删了重建 —— 本函数里别处还攥着 BaseForm 指针，改完就是野指针，
         * 实测直接 0xC0000005/0xC0000374。像素网格要 >=300% 才画，这里看不到，
         * 但虚线描边和 8 个手柄在 100% 一样在，够判定了。 */
        const QVector<UiNode *> allScreens2 = sc2->screens();
        if (!allScreens2.isEmpty()) {
            sc2->selectNode(allScreens2.first());
        }
        m_mgr->setShowChrome(true);
        const int withChrome = offPalette(sc2->grab().toImage());
        m_mgr->setShowChrome(false);
        const int without = offPalette(sc2->grab().toImage());
        m_mgr->setShowChrome(true);
        check(QStringLiteral("辅助线开着时画面上有非亮/灭的像素（描边/手柄）"),
              withChrome > 0,
              QStringLiteral("%1 个").arg(withChrome));
        check(QStringLiteral("隐藏辅助线之后整页只剩亮/灭两种像素"),
              without == 0,
              QStringLiteral("还剩 %1 个非亮/灭像素").arg(without));
    }

    /* --- 7. 宽高不能为零 --- */
    sc->rebuild();
    if (BaseForm *f = sc->formFor(frame)) {
        const QRect g0 = f->geometry();
        QSpinBox spin;
        spin.setObjectName(QStringLiteral("spinW"));
        spin.setRange(0, 9999);
        /* 先摆到非 0，再往 0 敲 —— QSpinBox 默认值就是 0，直接 setValue(0)
         * 不会发 valueChanged，测的就成了空气。 */
        spin.setValue(g0.width() > 0 ? g0.width() : 7);
        QObject::connect(&spin, QOverload<int>::of(&QSpinBox::valueChanged),
                         f, &BaseForm::onXYWHChangedValue);
        EditorOps::clearLastMessage();
        spin.setValue(0);
        check(QStringLiteral("宽度设 0 被拦下且几何没变"),
              f->geometry() == g0
              && EditorOps::lastMessage() == QStringLiteral("宽高不能设置为零."),
              QStringLiteral("原几何=%1x%2 现=%3x%4 提示=[%5]")
                  .arg(g0.width()).arg(g0.height())
                  .arg(f->geometry().width()).arg(f->geometry().height())
                  .arg(EditorOps::lastMessage()));
        spin.setValue(20);
        check(QStringLiteral("宽度设正常值生效"), f->geometry().width() == 20,
              QStringLiteral("宽=%1").arg(f->geometry().width()));
    }

    /* --- 8. 列表加行：加出来的必须是 NewLayout，挂 listwidget 键 --- */
    if (list) {
        sc->rebuild();
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            const int n0 = list->children.size();
            lf->onAddManyLine();
            const bool ok = list->children.size() == n0 + 1;
            check(QStringLiteral("列表加行"), ok);
            check(QStringLiteral("新行挂 listwidget 键且是 NewLayout"),
                  ok && list->children.last().first == QLatin1String("listwidget")
                  && list->children.last().second->cls == QLatin1String("NewLayout"),
                  ok ? QStringLiteral("%1/%2").arg(list->children.last().first,
                                                   list->children.last().second->cls)
                     : QString());
        } else {
            check(QStringLiteral("列表加行"), false, QStringLiteral("画布上找不到列表"));
        }
    }

    /* --- 9. CSS 状态的增删（属性页签上那四个按钮） --- */
    onNodeSelected(frame);
    {
        const int st0 = frame->cssStateCount();
        m_prop->setState(0);
        m_prop->onCopyAppendState();
        check(QStringLiteral("复制添加一个 CSS 状态"),
              frame->cssStateCount() == st0 + 1,
              QStringLiteral("%1 -> %2").arg(st0).arg(frame->cssStateCount()));
        m_prop->onCopyInsertState();
        check(QStringLiteral("复制插入一个 CSS 状态"),
              frame->cssStateCount() == st0 + 2);
        m_prop->onRemoveState();
        m_prop->onRemoveState();
        check(QStringLiteral("删除活动项回到原状态数"),
              frame->cssStateCount() == st0,
              QStringLiteral("现在 %1").arg(frame->cssStateCount()));
        /* 只剩一个时不许再删 —— 删光了控件既没几何也没样式 */
        while (frame->cssStateCount() > 1) {
            m_prop->onRemoveState();
        }
        EditorOps::clearLastMessage();
        m_prop->onRemoveState();
        check(QStringLiteral("最后一个 CSS 状态删不掉"),
              frame->cssStateCount() == 1 && !EditorOps::lastMessage().isEmpty(),
              EditorOps::lastMessage());
    }

    /* --- 10. 拖放建控件（手册里的主要手势） --- */
    sc->rebuild();
    {
        BaseForm *lf = sc->formFor(layout);
        check(QStringLiteral("画布上找得到目标布局"), lf != nullptr);
        if (lf) {
            /* 落点取布局自己的中心：取 (3,4) 那种边角容易压在别的控件上，
             * 归属会算到那个控件所属的布局去（那也是对的行为，只是不好断言）。 */
            const QPoint at = lf->mapTo(sc, lf->rect().center());
            UiNode *landed = nullptr;
            const bool taken = sc->simulateDropForTest(at, QStringLiteral("NewFrame"),
                                                       QStringLiteral("Text"), &landed);
            check(QStringLiteral("拖到画布上能建出控件"),
                  taken && landed != nullptr,
                  QStringLiteral("落点=(%1,%2) 收下=%3 归到=%4")
                      .arg(at.x()).arg(at.y()).arg(taken)
                      .arg(landed ? landed->name : QStringLiteral("(无)")));
            check(QStringLiteral("落点归到的一定是个布局"),
                  landed && EditorOps::isLayout(landed),
                  landed ? landed->cls : QString());
            if (landed && !landed->children.isEmpty()) {
                UiNode *drop = landed->children.last().second;
                /* 【必须重新取 BaseForm】拖放建出了新节点 = 结构变了，画布会
                 * 整体 rebuild()，上面那个 lf 已经从画布上摘下来了 ——
                 * 再拿它 mapTo(sc, …) 会一路往上找不到 sc，走到空指针。 */
                BaseForm *lf2 = sc->formFor(layout);
                BaseForm *landedForm = sc->formFor(landed);
                check(QStringLiteral("结构变化后画布上还找得到这两个布局"),
                      lf2 != nullptr && landedForm != nullptr);
                if (lf2 && landedForm) {
                    const QPoint want = lf2->mapTo(sc, lf2->rect().center())
                                        - landedForm->mapTo(sc, QPoint(0, 0));
                    check(QStringLiteral("落点写进了新控件的 rect"),
                          drop->rectOf(0).topLeft() == want,
                          QStringLiteral("rect=(%1,%2) 期望=(%3,%4)")
                              .arg(drop->rectOf(0).x()).arg(drop->rectOf(0).y())
                              .arg(want.x()).arg(want.y()));
                }
            }
        }

        /* 收不了的地方要明确拒绝。
         * 【别拿页面 (0,0) 当"空白"】图层和第一个布局都是从 (0,0) 铺开的，
         * 那里压根不空 —— 我第一版就这么写，结果控件真被建出来了，测试却
         * "通过"（因为我只比了节点总数，而它确实变了才对）。取一个在所有
         * 控件之外的点才是真的空白。 */
        const int n1 = countNodes(page);
        UiNode *bad = nullptr;
        const bool refused = !sc->simulateDropForTest(QPoint(9000, 9000),
                                                      QStringLiteral("NewFrame"),
                                                      QStringLiteral("Text"), &bad);
        check(QStringLiteral("拖到所有布局之外被拒"),
              refused && bad == nullptr && countNodes(page) == n1,
              QStringLiteral("拒绝=%1 归到=%2 节点 %3->%4")
                  .arg(refused).arg(bad ? bad->name : QStringLiteral("(无)"))
                  .arg(n1).arg(countNodes(page)));

        /* 图层不管拖到哪儿都落到页上 —— 手册："点击图层并拖动到绘制面板" */
        const int layers = page->children.size();
        const bool okLayer = sc->simulateDropForTest(QPoint(5, 5),
                                                     QStringLiteral("NewLayer"),
                                                     QStringLiteral("NewLayer"));
        check(QStringLiteral("拖图层落到页上"),
              okLayer && page->children.size() == layers + 1,
              QStringLiteral("收下=%1 图层数 %2->%3")
                  .arg(okLayer).arg(layers).arg(page->children.size()));
    }

    /* --- 11. CSS 字段回写（这一页以前除了位置坐标全是只读的） --- */
    {
        const QString before = frame->cssField(0, QStringLiteral("background_color"),
                                               QStringLiteral("background-color")).toString();
        const bool ok = frame->setCssField(0, QStringLiteral("background_color"),
                                           QStringLiteral("background-color"),
                                           QStringLiteral("#123456"));
        check(QStringLiteral("背景色能写回 element_css"),
              ok && frame->cssField(0, QStringLiteral("background_color"),
                                    QStringLiteral("background-color")).toString()
                    == QStringLiteral("#123456"));
        frame->setCssField(0, QStringLiteral("background_color"),
                           QStringLiteral("background-color"), before);
        check(QStringLiteral("写不存在的属性名要失败（不能悄悄吞掉）"),
              !frame->setCssField(0, QStringLiteral("没有这个属性"),
                                  QStringLiteral("default"), 1));
    }

    /* --- 12. 网格开关真的影响画布 --- */
    {
        const bool g0 = sc->showGrid();
        sc->setShowGrid(!g0);
        check(QStringLiteral("网格开关能改到画布上"), sc->showGrid() != g0);
        sc->setShowGrid(g0);
    }

    /* --- 13. 列表滚轮翻行（只动显示，不许动数据） --- */
    sc->rebuild();
    if (list) {
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            const QByteArray before = m_mgr->model()->toJsonBytes();
            const int f0 = lf->firstVisible();
            lf->setFirstVisible(f0 + 1);
            check(QStringLiteral("滚轮能翻行"), lf->firstVisible() == f0 + 1,
                  QStringLiteral("%1 -> %2").arg(f0).arg(lf->firstVisible()));
            check(QStringLiteral("翻行不改工程数据"),
                  m_mgr->model()->toJsonBytes() == before);
            lf->setFirstVisible(0);
            lf->setFirstVisible(-5);
            check(QStringLiteral("翻到头就停住，不会翻出负数"),
                  lf->firstVisible() == 0,
                  QStringLiteral("现在 %1").arg(lf->firstVisible()));
        }
    }

    /* --- 14. 负坐标要能原样留住 ---
     * left/top 是**有符号**的，SmallColor_oled.json 里就有 (0,-11) 的控件
     * —— 让内容从父容器上边缘露出去是常用手法。
     * 一度在拖动里把坐标夹到 [0,父级尺寸]，
     * 那是凭空多出来的限制，会把用户已有的负坐标改掉。这条守着它别再回来。 */
    sc->rebuild();
    if (BaseForm *f = sc->formFor(frame)) {
        const QRect g0 = f->geometry();
        f->setGeometry(QRect(-7, -11, qMax(1, g0.width()), qMax(1, g0.height())));
        f->syncRectToNode();
        check(QStringLiteral("负坐标能写进节点（允许，别钳）"),
              frame->rectOf(0).topLeft() == QPoint(-7, -11),
              QStringLiteral("rect=(%1,%2)")
                  .arg(frame->rectOf(0).x()).arg(frame->rectOf(0).y()));
        QString tmpErr;
        const QString tmpF = QDir::temp().filePath(QStringLiteral("uitools_neg.json"));
        if (m_mgr->model()->save(tmpF, &tmpErr)) {
            ProjectModel re;
            if (re.load(tmpF, &tmpErr)) {
                UiNode *back = nullptr;
                for (UiNode *pg : re.pages()) {
                    pg->forEach([&](UiNode *x) {
                        if (!back && x->rectOf(0).topLeft() == QPoint(-7, -11)) {
                            back = x;
                        }
                        return back == nullptr;
                    });
                }
                check(QStringLiteral("负坐标存盘再读回来还在"), back != nullptr);
            }
            QFile::remove(tmpF);
        }
        f->setGeometry(g0);
        f->syncRectToNode();
    }

    /* --- 15. 树上的显示名 ---
     * 有些节点的 -name 在 json 里是**裸的类型名**（比如 "VerticalList"），
     * 直接拿去显示，树上就冒出个英文名。所以 -name 没被正经命名过时，
     * 改成拿 caption+序号 现算，显示成"垂直列表_16"这种。 */
    {
        UiNode *bare = nullptr;
        int bareSeq = -1;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                if (!bare && x->parent && !x->type.isEmpty() && x->name == x->type) {
                    bare = x;
                    bareSeq = m_mgr->model()->nodeSeq(x);
                }
                return bare == nullptr;
            });
            if (bare) {
                break;
            }
        }
        if (bare) {
            const QString want = QStringLiteral("%1_%2").arg(bare->caption).arg(bareSeq);
            check(QStringLiteral("裸类型名的节点显示成 caption_序号（不显示英文名）"),
                  m_mgr->model()->displayName(bare) == want,
                  QStringLiteral("-name=%1 显示=%2 期望=%3")
                      .arg(bare->name, m_mgr->model()->displayName(bare), want));
        }
        /* 用户自己起的名字要原样显示，不能被 caption_序号 覆盖 */
        check(QStringLiteral("正常命名的节点原样显示"),
              m_mgr->model()->displayName(frame) == frame->name,
              QStringLiteral("-name=%1 显示=%2")
                  .arg(frame->name, m_mgr->model()->displayName(frame)));

        /* 序号是跨页全局的：页0 最后一个节点的序号 + 1 = 页1 第一个节点的序号 */
        if (m_mgr->model()->pages().size() >= 2) {
            UiNode *p0last = nullptr;
            m_mgr->model()->pages().at(0)->forEach([&](UiNode *x) {
                if (x->parent) {
                    p0last = x;
                }
                return true;
            });
            UiNode *p1first = m_mgr->model()->pages().at(1)->children.isEmpty()
                              ? nullptr
                              : m_mgr->model()->pages().at(1)->children.first().second;
            if (p0last && p1first) {
                check(QStringLiteral("节点序号跨页连着走（不是每页各数各的）"),
                      m_mgr->model()->nodeSeq(p1first)
                          == m_mgr->model()->nodeSeq(p0last) + 1,
                      QStringLiteral("页0末=%1 页1首=%2")
                          .arg(m_mgr->model()->nodeSeq(p0last))
                          .arg(m_mgr->model()->nodeSeq(p1first)));
            }
        }
    }

    /* --- 16. 画布预览：默认隐藏 / 单独预览 / 画面翻页 / 眼睛 / 缩放 ---
     * 一个图层下常挂着好几个**全屏尺寸**的互斥布局（有的页就有 5 个
     * (0,0,128,64)），全画出来最上面那个把下面全盖死，什么都编不了。 */
    sc->rebuild();
    {
        /* 找一个标了"默认隐藏"的布局 */
        UiNode *hidden = nullptr;
        page->forEach([&](UiNode *x) {
            if (!hidden && x->parent && x->isDefaultHidden()) {
                hidden = x;
            }
            return hidden == nullptr;
        });
        /* 【没样本就跳过，不算失败】oled 那份工程页0 里一个"默认隐藏"都没有，
         * 写成硬性检查的话它会红一条，可那是测试前提不成立，不是功能坏。 */
        if (hidden) {
            /* 先选个别处，确认它是藏着的 */
            onNodeSelected(layer);
            BaseForm *hf = sc->formFor(hidden);
            check(QStringLiteral("默认隐藏的节点画布上不显示"),
                  hf && !hf->isVisible());

            /* 选中它 -> 要显出来 */
            onNodeSelected(hidden);
            check(QStringLiteral("一选中它就显出来（否则树上点得到、画布上摸不着）"),
                  hf && hf->isVisible());

            /* 单独预览：同级其它顶层布局**一个都不画**（不是淡出 —— 整屏、
             * 背景不透明，淡到 18% 照样把下面搅浑）。连它们的子孙也不能画。 */
            UiNode *top = hidden;
            while (top && top->parent && top->parent->cls != QLatin1String("NewLayer")) {
                top = top->parent;
            }
            int shown = 0, sibs = 0;
            if (top && top->parent) {
                for (const auto &sib : top->parent->children) {
                    if (sib.second == top) {
                        continue;
                    }
                    ++sibs;
                    sib.second->forEach([&](UiNode *x) {
                        if (BaseForm *sf = sc->formFor(x)) {
                            if (sf->isVisible()) {
                                ++shown;
                            }
                        }
                        return true;
                    });
                }
            }
            check(QStringLiteral("单独预览：同级其它画面一个控件都不画"),
                  sibs > 0 && shown == 0,
                  QStringLiteral("同级 %1 个画面，还露着 %2 个控件").arg(sibs).arg(shown));
            check(QStringLiteral("当前画面自己整棵都在"),
                  sc->formFor(top) && sc->formFor(top)->isVisible());

            /* 关掉单独预览 + 开「显示隐藏项」-> 全都画出来（回到老样子） */
            onNodeSelected(layer);
            sc->setSolo(false);
            sc->setShowHidden(true);
            check(QStringLiteral("关掉单独预览并开「显示隐藏项」后全都画出来"),
                  hf && hf->isVisible());
            sc->setShowHidden(false);
            sc->setSolo(true);

            /* 画面翻页 */
            const QVector<UiNode *> allScreens = sc->screens();
            check(QStringLiteral("数得出当前页有几个画面"), allScreens.size() >= 2,
                  QStringLiteral("%1 个").arg(allScreens.size()));
            if (allScreens.size() >= 2) {
                sc->selectNode(allScreens.first());
                check(QStringLiteral("画面序号从 0 开始"), sc->currentScreenIndex() == 0,
                      QStringLiteral("现在 %1").arg(sc->currentScreenIndex()));
                m_mgr->stepScreen(+1);
                check(QStringLiteral("能翻到下一个画面"), sc->currentScreenIndex() == 1,
                      QStringLiteral("现在 %1").arg(sc->currentScreenIndex()));
                m_mgr->stepScreen(-1);
                check(QStringLiteral("能翻回上一个"), sc->currentScreenIndex() == 0);
                m_mgr->stepScreen(-1);
                check(QStringLiteral("翻到头就停住"), sc->currentScreenIndex() == 0);
            }
        }

        /* 眼睛：手动藏了之后，点别的节点不能把它放回来 */
        if (BaseForm *ff = sc->formFor(frame)) {
            onNodeSelected(frame);
            sc->toggleUserHidden(frame);
            check(QStringLiteral("眼睛能把节点藏起来"),
                  sc->isUserHidden(frame) && !ff->isVisible());
            onNodeSelected(layout);          // 选别处，触发一次可见性重算
            check(QStringLiteral("重算可见性不会把眼睛藏的又放回来"),
                  !ff->isVisible(), QStringLiteral("可见=%1").arg(ff->isVisible()));
            sc->toggleUserHidden(frame);
            onNodeSelected(frame);
            check(QStringLiteral("眼睛再点一次放回来"), ff->isVisible());
        }

        /* 缩放只影响显示，坐标不能被乘进去 */
        const QByteArray beforeZoom = m_mgr->model()->toJsonBytes();
        const QRect r1 = frame->rectOf(0);
        m_mgr->setZoom(400);
        check(QStringLiteral("缩放 400% 生效"), m_mgr->zoom() == 400);
        check(QStringLiteral("缩放不改坐标数据"),
              frame->rectOf(0) == r1 && m_mgr->model()->toJsonBytes() == beforeZoom);
        if (BaseForm *ff2 = sc->formFor(frame)) {
            check(QStringLiteral("画布上的控件真的放大了 4 倍"),
                  ff2->width() == qMax(1, r1.width() * 4),
                  QStringLiteral("宽 %1 -> %2（期望 %3）")
                      .arg(r1.width()).arg(ff2->width()).arg(r1.width() * 4));
        }
        m_mgr->setZoom(100);

        /* 【改倍率不能把选中弄丢】rebuild() 会清 m_selected（结构变更时那些
         * 裸指针确实会变野），可改倍率**模型一个字节都没动**。清掉之后单独
         * 预览失去目标，会退回去显示默认那个布局 —— 表现就是"一缩放就跳回
         * 第一个布局"。挑一个**非默认**的画面来验，不然默认那个本来就在，
         * 测不出区别。 */
        {
            const QVector<UiNode *> scr = sc->screens();
            UiNode *other = nullptr;
            for (UiNode *s : scr) {
                if (s != sc->screens().value(sc->currentScreenIndex())) {
                    other = s;
                    break;
                }
            }
            if (other && scr.size() >= 2) {
                sc->setSolo(true);
                onNodeSelected(other);
                const int idxBefore = sc->currentScreenIndex();
                m_mgr->setZoom(200);
                check(QStringLiteral("改倍率不会把选中的控件弄丢"),
                      sc->selectedNode() == other,
                      QStringLiteral("选中 %1")
                          .arg(sc->selectedNode()
                               ? m_mgr->model()->displayName(sc->selectedNode())
                               : QStringLiteral("(空)")));
                check(QStringLiteral("改倍率不会跳回第一个布局"),
                      sc->currentScreenIndex() == idxBefore,
                      QStringLiteral("画面 %1 -> %2")
                          .arg(idxBefore).arg(sc->currentScreenIndex()));
                m_mgr->setZoom(100);
                check(QStringLiteral("缩回去也还停在原来那个布局"),
                      sc->currentScreenIndex() == idxBefore && sc->selectedNode() == other,
                      QStringLiteral("画面 %1").arg(sc->currentScreenIndex()));
            }
        }

        /* 【列表的行高也得跟着倍率走】sizehw/space 是 1:1 逻辑像素，
         * relayoutRows() 摆的却是屏幕坐标。以前没乘倍率，放大后一滚轮就
         * 露馅：行高还是原尺寸，和周围放大过的控件对不上。 */
        {
            NewList *lst = nullptr;
            UiNode *lstNode = nullptr;
            page->forEach([&](UiNode *x) {
                if (!lst) {
                    if (auto *f = qobject_cast<NewList *>(sc->formFor(x))) {
                        if (f->subForms().size() >= 2) {
                            lst = f;
                            lstNode = x;
                        }
                    }
                }
                return lst == nullptr;
            });
            if (lst && lstNode) {
                const int sizehw = lstNode->extraValue(QStringLiteral("sizehw")).toInt(16);
                m_mgr->setZoom(400);
                /* 重建过了，得重新拿一次 */
                lst = qobject_cast<NewList *>(sc->formFor(lstNode));
                if (lst && lst->subForms().size() >= 2) {
                    lst->setFirstVisible(1);       // 走一次 relayoutRows
                    lst->setFirstVisible(0);
                    const QVector<BaseForm *> rows = lst->subForms();
                    const bool vert = rows.at(0)->y() != rows.at(1)->y();
                    const int got = vert ? rows.at(0)->height() : rows.at(0)->width();
                    check(QStringLiteral("放大后列表行高跟着倍率走"),
                          got == qMax(1, sizehw * 4),
                          QStringLiteral("sizehw=%1 倍率 400 实际 %2（期望 %3）")
                              .arg(sizehw).arg(got).arg(sizehw * 4));
                    const int step = vert
                        ? qAbs(rows.at(1)->y() - rows.at(0)->y())
                        : qAbs(rows.at(1)->x() - rows.at(0)->x());
                    const int space = lstNode->extraValue(QStringLiteral("space")).toInt(0);
                    check(QStringLiteral("放大后列表行间距也跟着倍率走"),
                          step == qMax(1, sizehw * 4) + space * 4,
                          QStringLiteral("实际步长 %1（期望 %2）")
                              .arg(step).arg(qMax(1, sizehw * 4) + space * 4));
                }
                m_mgr->setZoom(100);
            }
        }
    }

    /* --- 17. 单色语义 + 同名属性不能串写 ---
     * 文字控件里 property[4] 和 property[5] **都叫 "color"**，靠 caption
     * 区分"文字颜色"和"高亮颜色"。属性面板以前按名字定位，改高亮颜色会
     * 写进文字颜色。下游 StyBuilder 一直是按序号取的（colorAt(0)/colorAt(1)），
     * 面板必须对齐。 */
    {
        UiNode *txt = nullptr;
        page->forEach([&](UiNode *x) {
            if (!txt && x->type == QLatin1String("Text")) {
                txt = x;
            }
            return txt == nullptr;
        });
        if (txt) {
            int first = -1, second = -1;
            for (int i = 0; i < txt->props.size(); ++i) {
                if (txt->props.at(i).name == QLatin1String("color")) {
                    if (first < 0) {
                        first = i;
                    } else if (second < 0) {
                        second = i;
                    }
                }
            }
            check(QStringLiteral("文字控件确实有两条同名的 color 属性"),
                  first >= 0 && second >= 0,
                  QStringLiteral("下标 %1 / %2").arg(first).arg(second));
            if (first >= 0 && second >= 0) {
                const QString keep = txt->props.at(first).raw
                                     .value(QStringLiteral("color")).toString();
                /* 改"高亮颜色"（第二条），"文字颜色"（第一条）必须纹丝不动 */
                txt->props[second].raw.insert(QStringLiteral("color"),
                                              QStringLiteral("#ffaaa555"));
                txt->props[second].dirty = true;
                check(QStringLiteral("改高亮颜色不会串写到文字颜色"),
                      txt->props.at(first).raw.value(QStringLiteral("color")).toString()
                          == keep,
                      QStringLiteral("文字颜色现在=%1 原=%2")
                          .arg(txt->props.at(first).raw
                               .value(QStringLiteral("color")).toString(), keep));
            }

            /* 单色语义：三个魔数要判对 */
            check(QStringLiteral("#ff555aaa 判为「填充」"),
                  Preview::fillOf(QStringLiteral("#ff555aaa")) == Preview::MonoFill::Set);
            check(QStringLiteral("其它颜色一律判为「不填充」"),
                  Preview::fillOf(QStringLiteral("#D9EE94")) == Preview::MonoFill::None
                  && Preview::fillOf(QString()) == Preview::MonoFill::None);
            check(QStringLiteral("#ffaaa555 判为「反显」"),
                  Preview::textModeOf(QStringLiteral("#ffaaa555"))
                      == Preview::MonoText::Invert);
            check(QStringLiteral("#ff555aaa 文字判为「不显示」"),
                  Preview::textModeOf(QStringLiteral("#ff555aaa"))
                      == Preview::MonoText::Hidden);
            check(QStringLiteral("边框的判断方向和背景相反"),
                  !Preview::borderVisible(QStringLiteral("#ff555aaa"))
                  && Preview::borderVisible(QStringLiteral("#ffffffff")));
            check(QStringLiteral("这套工程的图层认作单色(OSD1)"),
                  Preview::isMonoLayer(txt));

            /* 内容预览真的画得出东西。
             * 【要挑一个真配了文字列表的】前面的用例往同一个布局里新建过
             * "文字"控件，模板里 str.list 是空的；前序遍历可能先撞上它，
             * 那当然画不出东西 —— 那是测试挑错了目标，不是功能坏。
             * 另外 code=ascii/text 的控件文字是程序运行时给的，工程里本来
             * 就没有内容可预览，同样要排除。 */
            UiNode *withStr = nullptr;
            page->forEach([&](UiNode *x) {
                if (withStr || x->type != QLatin1String("Text")) {
                    return true;
                }
                for (const UiProperty &pr : x->props) {
                    if (pr.name == QLatin1String("str")
                        && !pr.raw.value(QStringLiteral("list")).toArray().isEmpty()) {
                        withStr = x;
                        break;
                    }
                }
                return withStr == nullptr;
            });
            if (withStr) {
                check(QStringLiteral("配了文字列表的控件画得出内容"),
                      !Preview::contentOf(withStr, QColor(Qt::white)).isNull(),
                      withStr->name);
            }
            /* 没配文字列表的（新建的 / ascii 的）就该是空的，不能凭空造内容 */
            UiNode *noStr = nullptr;
            page->forEach([&](UiNode *x) {
                if (noStr || x->type != QLatin1String("Text")) {
                    return true;
                }
                bool has = false;
                for (const UiProperty &pr : x->props) {
                    if (pr.name == QLatin1String("str")
                        && !pr.raw.value(QStringLiteral("list")).toArray().isEmpty()) {
                        has = true;
                    }
                }
                if (!has) {
                    noStr = x;
                }
                return noStr == nullptr;
            });
            if (noStr) {
                check(QStringLiteral("没配文字的控件预览为空（不凭空造内容）"),
                      Preview::contentOf(noStr, QColor(Qt::white)).isNull(),
                      noStr->name);
            }
        }
        UiNode *pic = nullptr;
        page->forEach([&](UiNode *x) {
            if (!pic && x->type == QLatin1String("ImageList")) {
                pic = x;
            }
            return pic == nullptr;
        });
        if (pic) {
            check(QStringLiteral("图片控件画得出内容"),
                  !Preview::contentOf(pic, QColor(Qt::white)).isNull());
        }
    }

    /* --- 18. 属性栏的滚轮不能改值 ---
     * 属性栏又长又密，滚页面时鼠标必然从一堆 spin/combo 上扫过去。Qt 默认把
     * 滚轮当"改值"，一不留神坐标就被改了，而且这工具没有撤消，很难发现。 */
    onNodeSelected(frame);
    {
        /* 【三块都要扫】m_dyn 不是 m_com 的子对象（它排在 CSS属性 页签下面，
         * 见 ComProperty 构造里的说明）。只扫 m_com 的话，动态区那些
         * spin / 下拉框一个都测不到 —— 而"滚轮误改"最容易出事的恰恰是那儿
         * （默认高亮行号、滚动方式、点亮/反显…）。 */
        QVector<QWidget *> roots{ m_com, m_prop };
        roots += m_com->dynamicSections();
        int rolled = 0, total = 0;
        auto roll = [](QWidget *w) {
            QWheelEvent we(QPointF(5, 5), w->mapToGlobal(QPoint(5, 5)),
                           QPoint(0, 120), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(w, &we);
        };
        for (QWidget *root : roots) {
            if (!root) {
                continue;
            }
            for (QAbstractSpinBox *sb : root->findChildren<QAbstractSpinBox *>()) {
                auto *sp = qobject_cast<QSpinBox *>(sb);
                if (!sp) {
                    continue;
                }
                ++total;
                const int before = sp->value();
                roll(sp);
                if (sp->value() != before) {
                    ++rolled;
                    sp->setValue(before);
                }
            }
            /* 下拉框被滚轮换掉选项一样是误改，而且它改的是枚举语义，
             * 比坐标差一像素更难发现。 */
            for (QComboBox *cb : root->findChildren<QComboBox *>()) {
                if (cb->count() < 2) {
                    continue;
                }
                ++total;
                const int before = cb->currentIndex();
                roll(cb);
                if (cb->currentIndex() != before) {
                    ++rolled;
                    cb->setCurrentIndex(before);
                }
            }
        }
        check(QStringLiteral("属性栏里滚轮扫过不会改数值"),
              total > 0 && rolled == 0,
              QStringLiteral("%1 个可改控件，被滚动改掉 %2 个").arg(total).arg(rolled));
    }

    /* --- 18b. "点亮/反显/不显示"下拉框必须真写进节点 ---
     * 前面第 17 条改的是 json，验的是数据模型；这里改的是**控件本身** ——
     * 用户在面板上点一下，到底有没有落到 props[] 上，只有驱动真控件才知道。
     * 单色屏下这三个选项写的是固件认的魔数（Preview::kMonoLit / kMonoInvert
     * / kMonoFillOn），下游 StyBuilder 按序号取 colorAt(0)/colorAt(1)，
     * 所以"第二个下拉框"必须落到第二条 color 上。 */
    {
        UiNode *txt = nullptr;
        page->forEach([&](UiNode *x) {
            if (!txt && x->type == QLatin1String("Text")) {
                txt = x;
            }
            return txt == nullptr;
        });
        if (txt && Preview::isMonoLayer(txt)) {
            onNodeSelected(txt);
            QVector<int> ci;                    // 两条 color 属性的下标
            for (int i = 0; i < txt->props.size(); ++i) {
                if (txt->props.at(i).name == QLatin1String("color")) {
                    ci << i;
                }
            }
            /* 面板上"点亮/反显/不显示"那种下拉框，按选项文字认出来 */
            QVector<QComboBox *> cbs;
            /* m_dyn 不是 m_com 的子对象（它排在 CSS属性 页签下面，见
             * ComProperty 构造里的说明），得从 dynamicSection() 里找 */
            for (QComboBox *cb : m_com->dynCombosForTest()) {
                if (cb->count() == 3
                    && cb->itemText(0) == QStringLiteral("点亮")
                    && cb->itemText(1) == QStringLiteral("反显")) {
                    cbs << cb;
                }
            }
            check(QStringLiteral("文字控件的两条颜色都铺成了下拉框"),
                  ci.size() >= 2 && cbs.size() >= 2,
                  QStringLiteral("属性 %1 条，下拉框 %2 个")
                      .arg(ci.size()).arg(cbs.size()));
            if (ci.size() >= 2 && cbs.size() >= 2) {
                const QString keep0 = txt->props.at(ci.at(0)).raw
                                      .value(QStringLiteral("color")).toString();
                /* 点第二个下拉框 = 高亮颜色，选"反显" */
                cbs.at(1)->setCurrentIndex(1);
                check(QStringLiteral("下拉框选反显会真写进高亮颜色"),
                      txt->props.at(ci.at(1)).raw.value(QStringLiteral("color"))
                          .toString() == QLatin1String(Preview::kMonoInvert),
                      QStringLiteral("高亮颜色=%1")
                          .arg(txt->props.at(ci.at(1)).raw
                               .value(QStringLiteral("color")).toString()));
                check(QStringLiteral("点高亮下拉框不会碰到文字颜色"),
                      txt->props.at(ci.at(0)).raw.value(QStringLiteral("color"))
                          .toString() == keep0,
                      QStringLiteral("文字颜色现在=%1 原=%2")
                          .arg(txt->props.at(ci.at(0)).raw
                               .value(QStringLiteral("color")).toString(), keep0));
                /* 点第一个 = 文字颜色，选"不显示" */
                const QString keep1 = txt->props.at(ci.at(1)).raw
                                      .value(QStringLiteral("color")).toString();
                cbs.at(0)->setCurrentIndex(2);
                check(QStringLiteral("下拉框选不显示会真写进文字颜色"),
                      txt->props.at(ci.at(0)).raw.value(QStringLiteral("color"))
                          .toString() == QLatin1String(Preview::kMonoFillOn)
                          && txt->props.at(ci.at(1)).raw
                             .value(QStringLiteral("color")).toString() == keep1,
                      QStringLiteral("文字=%1 高亮=%2")
                          .arg(txt->props.at(ci.at(0)).raw
                               .value(QStringLiteral("color")).toString(),
                               txt->props.at(ci.at(1)).raw
                               .value(QStringLiteral("color")).toString()));
                /* 选回"点亮"，别把工程留在一个奇怪的状态上 */
                cbs.at(0)->setCurrentIndex(0);
            }
        }
    }

    /* --- 18d. 列表类参数必须和预览对得上 ---
     * 每个列表类属性下面都要跟一个条目下拉框（图片"缩略图+文件名"、
     * 文字"内容#ResID"），而且**停在预览真正画的那一条上**。以前这里只有
     * 一个按钮，面板上根本看不出画布上那张图是列表里的哪一条 —— 参数和
     * 预览对不上就是这么来的。 */
    UiNode *txtWithStr = nullptr;        // 18e 还要用它，提到外面来
    {
        UiNode *img = nullptr;
        UiNode *txt = nullptr;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                if (!img && x->type == QLatin1String("ImageList")) {
                    for (const UiProperty &q : x->props) {
                        if (q.name == QLatin1String("normal_image")
                            && q.raw.value(QStringLiteral("list")).toArray().size() >= 2) {
                            img = x;
                            break;
                        }
                    }
                }
                if (!txt && x->type == QLatin1String("Text")) {
                    for (const UiProperty &q : x->props) {
                        if (q.name == QLatin1String("str")
                            && !q.raw.value(QStringLiteral("list")).toArray().isEmpty()) {
                            txt = x;
                            break;
                        }
                    }
                }
                return !(img && txt);
            });
            if (img && txt) {
                break;
            }
        }

        if (img) {
            onNodeSelected(img);
            /* 下拉框里的条目数要和列表条目数一致，且停在"默认高亮"那一条 */
            int wantN = 0, wantIdx = 0;
            for (const UiProperty &q : img->props) {
                if (q.name == QLatin1String("normal_image")) {
                    wantN = q.raw.value(QStringLiteral("list")).toArray().size();
                }
                if (q.name == QLatin1String("highlight")) {
                    wantIdx = q.raw.value(QStringLiteral("default")).toInt();
                }
            }
            QComboBox *entry = nullptr;
            for (QComboBox *cb : m_com->dynCombosForTest()) {
                if (cb->count() == wantN && wantN > 0 && !cb->itemIcon(0).isNull()) {
                    entry = cb;
                    break;
                }
            }
            check(QStringLiteral("图片列表下面有条目下拉框（带缩略图）"),
                  entry != nullptr,
                  QStringLiteral("列表 %1 条").arg(wantN));
            if (entry) {
                check(QStringLiteral("图片条目下拉框停在「默认高亮」那一条"),
                      entry->currentIndex() == qBound(0, wantIdx, wantN - 1),
                      QStringLiteral("下拉框第 %1 条，默认高亮=%2")
                          .arg(entry->currentIndex()).arg(wantIdx));
            }
        }

        if (txt) {
            onNodeSelected(txt);
            QString wantId;
            QStringList ids;
            for (const UiProperty &q : txt->props) {
                if (q.name != QLatin1String("str")) {
                    continue;
                }
                wantId = q.raw.value(QStringLiteral("default")).toString();
                for (const QJsonValue &v : q.raw.value(QStringLiteral("list")).toArray()) {
                    ids << v.toString();
                }
                break;
            }
            check(QStringLiteral("文字列表的 default 就在列表里"),
                  !ids.isEmpty() && ids.contains(wantId),
                  QStringLiteral("default=%1 列表 %2 条").arg(wantId).arg(ids.size()));
            QComboBox *entry = nullptr;
            for (QComboBox *cb : m_com->dynCombosForTest()) {
                /* 文字条目显示成"内容#ResID"，认这个 # 后缀 */
                if (cb->count() == ids.size() && !ids.isEmpty()
                    && cb->itemText(0).endsWith(QLatin1Char('#') + ids.at(0))) {
                    entry = cb;
                    break;
                }
            }
            check(QStringLiteral("文字列表下面有条目下拉框（内容#ResID）"),
                  entry != nullptr,
                  entry ? entry->itemText(0) : QStringLiteral("没找到"));
            if (entry) {
                check(QStringLiteral("文字条目下拉框停在 default 那一条"),
                      entry->currentIndex() == ids.indexOf(wantId),
                      QStringLiteral("下拉框第 %1 条，default 是第 %2 条")
                          .arg(entry->currentIndex()).arg(ids.indexOf(wantId)));
            }
            txtWithStr = txt;
        }
    }

    /* --- 18e. 编码格式是固定三种，得给下拉框 ---
     * 固件 ui_text.c 硬编码只认 strpic/text/ascii，填别的控件在屏上是空白
     * 且无任何报错。以前这里是自由文本框，正好是最容易拼错的地方。 */
    if (txtWithStr) {
        onNodeSelected(txtWithStr);
        QComboBox *codeCb = nullptr;
        for (QComboBox *cb : m_com->dynCombosForTest()) {
            if (cb->findText(QStringLiteral("strpic")) >= 0
                && cb->findText(QStringLiteral("text")) >= 0
                && cb->findText(QStringLiteral("ascii")) >= 0) {
                codeCb = cb;
                break;
            }
        }
        check(QStringLiteral("编码格式是下拉框（固件只认 strpic/text/ascii）"),
              codeCb != nullptr,
              codeCb ? QStringLiteral("当前 %1").arg(codeCb->currentText())
                     : QStringLiteral("没找到"));
        if (codeCb) {
            QString cur;
            for (const UiProperty &q : txtWithStr->props) {
                if (q.name == QLatin1String("code")) {
                    cur = q.raw.value(QStringLiteral("default")).toString();
                    break;
                }
            }
            check(QStringLiteral("编码格式下拉框停在工程里的那个值"),
                  codeCb->currentText() == cur,
                  QStringLiteral("下拉框=%1 工程=%2").arg(codeCb->currentText(), cur));
            /* ascii 下资源里的文字列表不显示（固件把 attrs.str 置 NULL） */
            const QString keep = cur;
            /* 【只有 strpic 画得出资源里的文字】text/ascii 的内容都由程序
             * 运行时写（ui_text_set_text_by_id 那几个接口）：
             *   ascii  init 时 attrs.str 就是 NULL，不写就不画
             *   text   attrs.str 指向的是 u16 的 ResID 数组，字库把它当字符
             *          渲染出来是乱码，不是资源里那句话
             * 所以两种都不该按资源里的文字去画。 */
            for (const char *bad : { "ascii", "text" }) {
                codeCb->setCurrentIndex(codeCb->findText(QLatin1String(bad)));
                check(QStringLiteral("编码格式 %1 时预览不画资源里的文字")
                          .arg(QLatin1String(bad)),
                      Preview::contentOf(txtWithStr, Qt::white).isNull());
                check(QStringLiteral("编码格式 %1 配了文字列表要告警")
                          .arg(QLatin1String(bad)),
                      m_com->warningTextForTest().contains(QStringLiteral("程序")),
                      m_com->warningTextForTest().left(24));
            }
            /* 【预览文字】text/ascii 的内容运行时才有，画布上本来是空的。
             * 配一句只用于预览的假文字顶上，方便看排版 —— 但它只能存在工具
             * 配置里：写进工程就改动了工程文件的内容，而且**不能**把工程标记
             * 成改过（标题带 * / 退出问保存都是看这个）。 */
            {
                /* 先把 code 切到 ascii，**再**抓基准 —— 切 code 本身会改
                 * json（"text" 4 字节 vs "ascii" 5 字节），那是它该改的，
                 * 不能算到"预览文字"头上。 */
                codeCb->setCurrentIndex(codeCb->findText(QStringLiteral("ascii")));
                const QByteArray before = m_mgr->model()->toJsonBytes();
                m_mgr->setDirty(false);           // 上一句改的是 code，先归零

                const QString sample = QStringLiteral("预览文字ABC");
                Preview::setPresetText(txtWithStr, sample);
                check(QStringLiteral("配了预览文字后 text/ascii 也画得出来"),
                      !Preview::contentOf(txtWithStr, Qt::white).isNull());
                check(QStringLiteral("预览文字不写进工程 json"),
                      m_mgr->model()->toJsonBytes() == before,
                      QStringLiteral("字节数 %1 -> %2").arg(before.size())
                          .arg(m_mgr->model()->toJsonBytes().size()));
                check(QStringLiteral("配预览文字不会把工程标记成改过"),
                      !m_mgr->model()->dirty());
                check(QStringLiteral("预览文字只给 text/ascii 用"),
                      Preview::needsPresetText(txtWithStr));

                Preview::setPresetText(txtWithStr, QString());
                check(QStringLiteral("清掉预览文字又回到空"),
                      Preview::contentOf(txtWithStr, Qt::white).isNull());
            }

            codeCb->setCurrentIndex(codeCb->findText(keep));
            check(QStringLiteral("改回 strpic 预览又画得出来"),
                  !Preview::contentOf(txtWithStr, Qt::white).isNull(),
                  QStringLiteral("已改回 %1").arg(keep));
            check(QStringLiteral("改回 strpic 告警消失"),
                  m_com->warningTextForTest().isEmpty(),
                  m_com->warningTextForTest().left(24));
            check(QStringLiteral("strpic 不给预览文字这一栏"),
                  !Preview::needsPresetText(txtWithStr));
        }
    }

    /* --- 18f. 时间/数字的格式：模板串 + 分隔符耗尽即截断 ---
     * 规则抄自固件（ui_time.c:65 / ui_number.c:76），这几条以前是错的：
     *   Y 是 4 位年，不是 2 位；
     *   '/' 是普通字面字符（照样吃一张分隔符图），不是"结束符"；
     *   分隔符图片用完之后，后面的内容**整段不画**。 */
    {
        UiNode *tm = nullptr;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                if (!tm && x->type == QLatin1String("Time")) {
                    for (const UiProperty &q : x->props) {
                        if (q.name == QLatin1String("number")
                            && q.raw.value(QStringLiteral("list")).toArray().size() == 10) {
                            tm = x;
                            break;
                        }
                    }
                }
                return tm == nullptr;
            });
            if (tm) {
                break;
            }
        }
        if (tm) {
            auto setFmt = [tm](const QString &f) {
                for (UiProperty &q : tm->props) {
                    if (q.name == QLatin1String("format")) {
                        q.raw.insert(QStringLiteral("default"), f);
                        return;
                    }
                }
            };
            auto delimCount = [tm]() {
                for (const UiProperty &q : tm->props) {
                    if (q.name == QLatin1String("delimiter")) {
                        return q.raw.value(QStringLiteral("list")).toArray().size();
                    }
                }
                return 0;
            };
            QString keep;
            for (const UiProperty &q : tm->props) {
                if (q.name == QLatin1String("format")) {
                    keep = q.raw.value(QStringLiteral("default")).toString();
                }
            }
            const int nDelim = delimCount();
            const int w0 = Preview::contentOf(tm, Qt::white).width();

            /* Y=4 位：单独一个 Y 要比单独一个 m 宽一倍 */
            setFmt(QStringLiteral("Y"));
            Preview::invalidate();
            const int wY = Preview::contentOf(tm, Qt::white).width();
            setFmt(QStringLiteral("m"));
            Preview::invalidate();
            const int wM = Preview::contentOf(tm, Qt::white).width();
            check(QStringLiteral("时间格式 Y 是 4 位年（不是 2 位）"),
                  wY > 0 && wM > 0 && wY == wM * 2,
                  QStringLiteral("Y 宽 %1，m 宽 %2").arg(wY).arg(wM));

            /* '/' 是普通字面字符：配得起分隔符时它要画出来 */
            if (nDelim >= 1) {
                setFmt(QStringLiteral("m"));
                Preview::invalidate();
                const int a = Preview::contentOf(tm, Qt::white).width();
                setFmt(QStringLiteral("m/"));
                Preview::invalidate();
                const int b = Preview::contentOf(tm, Qt::white).width();
                check(QStringLiteral("时间格式里的 / 会画成分隔符（不是结束符）"),
                      b > a,
                      QStringLiteral("\"m\" 宽 %1，\"m/\" 宽 %2").arg(a).arg(b));

                /* 分隔符只有 1 张时，第二个分隔符处整段截断 */
                setFmt(QStringLiteral("h:m:s"));
                Preview::invalidate();
                const int trunc = Preview::contentOf(tm, Qt::white).width();
                setFmt(QStringLiteral("h:m"));
                Preview::invalidate();
                const int full = Preview::contentOf(tm, Qt::white).width();
                check(QStringLiteral("分隔符不够时后面整段不画"),
                      nDelim >= 2 ? trunc > full : trunc == full,
                      QStringLiteral("分隔符 %1 张，\"h:m:s\" 宽 %2，\"h:m\" 宽 %3")
                          .arg(nDelim).arg(trunc).arg(full));
            }
            setFmt(keep);
            Preview::invalidate();
            check(QStringLiteral("格式改回去预览也回得来"),
                  Preview::contentOf(tm, Qt::white).width() == w0,
                  QStringLiteral("宽 %1（原 %2）")
                      .arg(Preview::contentOf(tm, Qt::white).width()).arg(w0));

            /* 格式框 = 常用预设 + "自定义…"。预设选中要真写进节点；
             * 工程里是预设之外的写法时，要落到"自定义"并把原值原样放出来，
             * 不许替用户改成某个近似的预设。 */
            onNodeSelected(tm);
            QComboBox *fmtCb = nullptr;
            for (QComboBox *cb : m_com->dynCombosForTest()) {
                if (cb->count() > 1
                    && cb->itemText(cb->count() - 1) == QStringLiteral("自定义…")) {
                    fmtCb = cb;
                    break;
                }
            }
            check(QStringLiteral("格式框是「常用预设 + 自定义…」下拉框"),
                  fmtCb != nullptr,
                  fmtCb ? QStringLiteral("%1 个预设，当前 %2")
                          .arg(fmtCb->count() - 1).arg(fmtCb->currentText())
                        : QStringLiteral("没找到"));
            if (fmtCb) {
                check(QStringLiteral("格式框停在工程里的那个值"),
                      fmtCb->currentText() == keep
                          || fmtCb->currentText() == QStringLiteral("自定义…"),
                      QStringLiteral("下拉框=%1 工程=%2")
                          .arg(fmtCb->currentText(), keep));
                /* 选一个预设：要真写进节点 */
                const int pick = fmtCb->findText(QStringLiteral("Y/M/D"));
                if (pick >= 0) {
                    fmtCb->setCurrentIndex(pick);
                    QString now;
                    for (const UiProperty &q : tm->props) {
                        if (q.name == QLatin1String("format")) {
                            now = q.raw.value(QStringLiteral("default")).toString();
                        }
                    }
                    check(QStringLiteral("选预设会真写进格式参数"),
                          now == QLatin1String("Y/M/D"),
                          QStringLiteral("格式现在=%1").arg(now));
                }
                /* 切到"自定义…"不能把原值清掉 */
                fmtCb->setCurrentIndex(fmtCb->count() - 1);
                QString afterCustom;
                for (const UiProperty &q : tm->props) {
                    if (q.name == QLatin1String("format")) {
                        afterCustom = q.raw.value(QStringLiteral("default")).toString();
                    }
                }
                check(QStringLiteral("切到「自定义…」不会把格式清空"),
                      !afterCustom.isEmpty(),
                      QStringLiteral("格式还是=%1").arg(afterCustom));
            }
            setFmt(keep);
            Preview::invalidate();

            /* 分隔符不够时的警告：面板里只留一个 ⚠（窄栏塞不下整句话），
             * 正文走带尾巴的浮动气泡，气泡按文字算尺寸、不会显示不全。 */
            if (fmtCb) {
                const int need2 = fmtCb->findText(QStringLiteral("h:m:s"));
                if (need2 >= 0 && nDelim < 2) {
                    fmtCb->setCurrentIndex(need2);
                    check(QStringLiteral("分隔符不够会出警告"),
                          !m_com->warningTextForTest().isEmpty(),
                          m_com->warningTextForTest().left(28));
                    QLabel *mark = nullptr;
                    for (QLabel *l : m_com->dynLabelsForTest()) {
                        if (l->text() == QStringLiteral("⚠")) {
                            mark = l;
                            break;
                        }
                    }
                    check(QStringLiteral("窄面板里只放一个 ⚠ 标记"),
                          mark != nullptr && !mark->toolTip().isEmpty(),
                          mark ? QStringLiteral("提示 %1 字").arg(mark->toolTip().size())
                               : QStringLiteral("没找到 ⚠"));
                    check(QStringLiteral("警告气泡放得下整句话（不会显示不全）"),
                          m_com->warningTipFitsForTest());
                }
                /* 换成够用的格式，警告要消失 */
                const int ok2 = fmtCb->findText(QStringLiteral("m"));
                if (ok2 >= 0) {
                    fmtCb->setCurrentIndex(ok2);
                    check(QStringLiteral("格式改对了警告会消失"),
                          m_com->warningTextForTest().isEmpty(),
                          m_com->warningTextForTest());
                }
            }
            setFmt(keep);
            Preview::invalidate();
        }
    }

    /* --- 18h. 两个列表编辑对话框里也要有预览 ---
     * 「图片编辑」要把每张位图画出来，「显示列表」要显示"内容#ResID"。
     * 以前这两个都退化成了
     * 纯文件名 / 纯 ResID：图片那边 QFileSystemModel 给的是通用文件图标，
     * 一屏两百多个一模一样的小方块，只能靠文件名猜。 */
    {
        const PropertyContext &ctx = PropertyContext::instance();
        /* 找一张工程里真实存在的图片来验 */
        QString relPic;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                for (const UiProperty &q : x->props) {
                    if (q.type != QLatin1String("piclist")
                        && q.type != QLatin1String("arrlist")) {
                        continue;
                    }
                    for (const QJsonValue &v : q.raw.value(QStringLiteral("list")).toArray()) {
                        const QString r = v.toString();
                        if (!r.isEmpty()
                            && QFileInfo::exists(QDir(ctx.projectDir).filePath(r))) {
                            relPic = r;
                            return false;
                        }
                    }
                }
                return true;
            });
            if (!relPic.isEmpty()) {
                break;
            }
        }
        check(QStringLiteral("工程里找得到真实存在的图片来验缩略图"),
              !relPic.isEmpty(), relPic);
        if (!relPic.isEmpty()) {
            ImageFileDialog dlg;
            dlg.setProjectDir(ctx.projectDir);
            dlg.setSelected(QStringList{ relPic });
            check(QStringLiteral("图片编辑：已选列表带缩略图"),
                  dlg.selectedIconIsRealForTest(0),
                  relPic);
            /* 值不能被显示改坏 —— 存回去的还得是那条相对路径 */
            check(QStringLiteral("图片编辑：加了缩略图不影响存回去的路径"),
                  dlg.selected() == QStringList{ relPic },
                  dlg.selected().join(QLatin1Char(',')));

            /* 【目录不能当图片】QFileSystemModel 光设 QDir::Files 挡不住
             * 子目录，双击就能把一个目录加进图片列表 —— 存进工程是一条指向
             * 目录的"图片路径"，要到出资源那步才炸，很难回溯。
             * 挑一个**有子目录**的地方来验。 */
            QString dirWithSub;
            const QString cfg = QDir(ctx.projectDir).filePath(QStringLiteral("config"));
            for (const QString &d : QDir(cfg).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString abs = QDir(cfg).filePath(d);
                if (!QDir(abs).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
                    dirWithSub = abs;
                    break;
                }
            }
            if (dirWithSub.isEmpty()) {
                dirWithSub = cfg;          // config 自己底下全是子目录
            }
            check(QStringLiteral("图片编辑：列表里不会出现子目录"),
                  dlg.dirRowsForTest(dirWithSub) == 0,
                  QStringLiteral("%1 里列出了 %2 个目录")
                      .arg(QFileInfo(dirWithSub).fileName())
                      .arg(dlg.dirRowsForTest(dirWithSub)));
            check(QStringLiteral("图片编辑：把目录喂给「添加」也加不进去"),
                  dlg.tryAddForTest(dirWithSub) == 0);
            check(QStringLiteral("图片编辑：真图还是加得进去"),
                  dlg.tryAddForTest(QDir(ctx.projectDir).filePath(relPic)) == 1);
        }

        if (!ctx.excelPath.isEmpty() && QFileInfo::exists(ctx.excelPath)) {
            I18nLanguage dlg;
            QString err;
            if (dlg.loadExcel(ctx.excelPath, &err)) {
                dlg.setSelected(QStringList{ QStringLiteral("m1") });
                const QString shown = dlg.selectedLabelForTest(0);
                check(QStringLiteral("显示列表：已选那条显示成「内容#ResID」"),
                      shown.endsWith(QStringLiteral("#m1")) && shown != QStringLiteral("m1"),
                      shown);
                check(QStringLiteral("显示列表：显示变了但值还是纯 ResID"),
                      dlg.selected() == QStringList{ QStringLiteral("m1") },
                      dlg.selected().join(QLatin1Char(',')));
            }
        }
    }

    /* --- 18c. 右栏的页面视图也要有内容预览 ---
     * 【这里坏了很久】右栏原本是另一套画法：按层级深浅填绿/蓝/紫色块，
     * 既没有图片文字，也不是单色屏的样子 —— 画布那边早就改成"黑底白点 +
     * 真内容"了，右栏一直停在老版本。断言两条：
     *   1) 真有点亮的像素（= 内容画出来了，不是一片空白）
     *   2) 每个像素非亮即灭（= 没有残留的彩色块；屏上只有这两种状态）
     *
     * 【不能写死黑白】「点亮/熄灭」的颜色现在是[全局设置]里可配的
     * （见 docs/UI_BEHAVIOR.md 8.4），配成绿底蓝字这条断言照样得成立。
     * 所以拿 Preview::monoLit()/monoDark() 当基准比，而不是比灰阶。 */
    if (m_pages) {
        const QImage img = m_pages->grabPageForTest(m_mgr->currentPage());
        check(QStringLiteral("右栏页面视图不是空图"),
              !img.isNull() && img.width() > 1 && img.height() > 1,
              QStringLiteral("%1x%2").arg(img.width()).arg(img.height()));
        if (!img.isNull()) {
            const QRgb litRgb = Preview::monoLit().rgb();
            const QRgb darkRgb = Preview::monoDark().rgb();
            int lit = 0, other = 0;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    const QRgb c = img.pixel(x, y) | 0xFF000000u;
                    if (c == litRgb) {
                        ++lit;
                    } else if (c != darkRgb) {
                        ++other;               // 既不是亮也不是灭 = 有残留
                    }
                }
            }
            check(QStringLiteral("右栏页面视图画出了内容（有点亮像素）"),
                  lit > 20,
                  QStringLiteral("点亮 %1 个像素").arg(lit));
            check(QStringLiteral("右栏页面视图只有亮/灭两种像素"),
                  other == 0,
                  QStringLiteral("既非亮也非灭的像素 %1 个").arg(other));
        }
    }

    /* --- 18i. 背景图片：弹窗给相对路径，而且画布真的画出来 ---
     * 出过的问题：这一行点开的是系统文件对话框，回来的是**绝对路径**，
     * 而工程 json 里存的是 "config/pic_lcd/v_block.bmp" 这种相对工程目录的
     * 形式；再加上画布压根没画 background-image —— 用户选完图什么都没发生，
     * 看着就是"设置不了"。这里点开的是 ImageListView（标题
     * "图片编辑(双击选中图片并更新到控件)"，见 docs/UI_BEHAVIOR.md 8.6）。 */
    {
        const QString projDir = QFileInfo(m_mgr->model()->filePath()).absolutePath();
        ImageListView dlg(this);
        dlg.setProjectDir(projDir);
        check(QStringLiteral("背景图片弹窗列得出图片"),
              dlg.imageCountForTest() > 0,
              QStringLiteral("%1 张").arg(dlg.imageCountForTest()));
        const bool picked = dlg.pickForTest(0);
        check(QStringLiteral("双击能选中一张图"), picked, dlg.selected());
        const QString rel = dlg.selected();
        check(QStringLiteral("弹窗给的是相对工程目录的路径，不是绝对路径"),
              !rel.isEmpty() && !QFileInfo(rel).isAbsolute()
              && QFileInfo(QDir(projDir).filePath(rel)).exists(),
              rel);

        /* 写进布局的 css，画布上必须多出点亮像素 */
        auto litCount = [](const QImage &im) {
            const QRgb l = Preview::monoLit().rgb();
            int n = 0;
            for (int y = 0; y < im.height(); ++y) {
                for (int x = 0; x < im.width(); ++x) {
                    if ((im.pixel(x, y) | 0xFF000000u) == l) {
                        ++n;
                    }
                }
            }
            return n;
        };
        const QString keep = layout->cssField(0, QStringLiteral("background_image"),
                                              QStringLiteral("background-image")).toString();
        const int litBefore = litCount(sc->grab().toImage());
        const bool wrote = layout->setCssField(0, QStringLiteral("background_image"),
                                               QStringLiteral("background-image"), rel);
        check(QStringLiteral("背景图片写得进 css"), wrote);
        if (wrote) {
            sc->rebuild();
            const int litAfter = litCount(sc->grab().toImage());
            check(QStringLiteral("设了背景图片，画布上真的多出点亮像素"),
                  litAfter > litBefore,
                  QStringLiteral("设之前 %1 -> 设之后 %2").arg(litBefore).arg(litAfter));
            /* 还原，别把这一项留在工程里 */
            layout->setCssField(0, QStringLiteral("background_image"),
                                QStringLiteral("background-image"), keep);
            sc->rebuild();
        }
    }

    /* --- 18f. 进出[全局设置]不许冲掉当前的预览状态 ---
     * 出过这个 bug：对话框关掉之后无条件读 canvas/defaultZoom、canvas/grid
     * 两个**没人写**的键，等于每次点确定都把缩放拉回 100%、网格复位。
     * 用户放大到 400% 编到一半进去看一眼，出来就白放大了。 */
    {
        const int zoomBefore = 250;
        m_mgr->setZoom(zoomBefore);
        const bool gridBefore = m_mgr->showGrid();
        m_mgr->applyGlobalSettings();
        check(QStringLiteral("进出全局设置不改预览缩放"),
              m_mgr->zoom() == zoomBefore,
              QStringLiteral("进去前 %1%，出来 %2%").arg(zoomBefore).arg(m_mgr->zoom()));
        check(QStringLiteral("进出全局设置不改网格开关"),
              m_mgr->showGrid() == gridBefore,
              QStringLiteral("进去前 %1，出来 %2")
                  .arg(gridBefore ? QStringLiteral("开") : QStringLiteral("关"),
                       m_mgr->showGrid() ? QStringLiteral("开") : QStringLiteral("关")));
        m_mgr->setZoom(100);
    }

    /* --- 18g. 改过参数要算"工程改过" ---
     * 标题上那个 * 和退出时"要不要保存"看的都是 ProjectModel::dirty()。
     * UiNode::markDirty() 只置节点自己的标志（够回写用），**传不到模型** ——
     * 所以属性面板改完参数以前既不显示 *、退出也不提示，一不小心就白改。 */
    {
        m_mgr->setDirty(false);
        refreshTitle();
        check(QStringLiteral("干净工程标题上没有 *"),
              !windowTitle().contains(QLatin1Char('*')), windowTitle());
        /* 标题格式：UI编辑工具(Build:YYYY-MM-DD) <工程名> */
        check(QStringLiteral("标题带构建日期、不带重建字样"),
              windowTitle().contains(QStringLiteral("Build:"))
                  && !windowTitle().contains(QStringLiteral("重建")),
              windowTitle());

        /* 走**真的属性面板**改一个值，不是直接改 json */
        UiNode *any = nullptr;
        page->forEach([&](UiNode *x) {
            if (!any && x != page && x->cssStateCount() > 0) {
                any = x;
            }
            return any == nullptr;
        });
        if (any) {
            onNodeSelected(any);
            /* 挑一个**还能往上加**的框。坐标 X 的范围是 [0, 父宽-自身宽]
             * （见 Position::setBounds()），控件铺满父容器时它就是
             * [0,0] —— 拿它 +1 加不动，会误判成"没标脏"。 */
            QSpinBox *sp = nullptr;
            for (QSpinBox *s : m_prop->findChildren<QSpinBox *>()) {
                if (s->isVisible() && s->value() < s->maximum()) {
                    sp = s;
                    break;
                }
            }
            if (sp) {
                sp->setValue(sp->value() + 1);
                check(QStringLiteral("改参数会把工程标记成改过"),
                      m_mgr->model()->dirty(),
                      QStringLiteral("dirty=%1").arg(m_mgr->model()->dirty()));
                check(QStringLiteral("改参数后标题会带 *"),
                      windowTitle().contains(QLatin1Char('*')), windowTitle());
                /* 有脏标志时退出才会问"要不要保存"（这里只验条件，不弹框） */
                check(QStringLiteral("有改动时退出要问保存"),
                      m_mgr->model()->dirty());
                sp->setValue(sp->value() - 1);
            }
        }
        m_mgr->setDirty(false);
        refreshTitle();
        check(QStringLiteral("存过之后 * 会消失"),
              !windowTitle().contains(QLatin1Char('*')), windowTitle());
    }

    /* --- 18j. 新建的控件必须自带一个唯一的 ID 号 ---
     * 建出来的控件"唯一ID号"必须是填好的（BaseForm / BaseForm_1 / …），
     * 空着的话生成资源时它拿不到 ename.h 里的宏，业务代码引用不到。 */
    {
        onNodeSelected(layout);
        const int n0 = countNodes(page);
        m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                    QStringLiteral("文字"));
        check(QStringLiteral("建出来了"), countNodes(page) == n0 + 1);
        UiNode *made = layout->children.isEmpty() ? nullptr
                                                  : layout->children.last().second;
        QString madeEname;
        if (made) {
            for (const UiProperty &pp : made->props) {
                if (pp.name == QLatin1String("id")) {
                    madeEname = pp.ename;
                    break;
                }
            }
        }
        check(QStringLiteral("新建控件自带默认 ID 号（不是空的）"),
              !madeEname.isEmpty(), madeEname);
        /* 全工程不许重名 —— 重了就是两个宏撞一起 */
        QHash<QString, int> seen;
        int dup = 0;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                for (const UiProperty &pp : x->props) {
                    if (pp.name == QLatin1String("id") && !pp.ename.isEmpty()) {
                        if (++seen[pp.ename.toUpper()] > 1) {
                            ++dup;
                        }
                    }
                }
                return true;
            });
        }
        QStringList dupNames;
        for (auto it = seen.constBegin(); it != seen.constEnd(); ++it) {
            if (it.value() > 1) {
                dupNames << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
            }
        }
        check(QStringLiteral("ID 号全工程唯一（大小写不敏感）"), dup == 0,
              dupNames.join(QLatin1String(", ")));
    }

    /* --- 18k. 改完不点别处就保存，也得算数 ---
     * 属性面板的输入框是 editingFinished 才写回模型，而工具栏按钮是 NoFocus，
     * 点"保存"不会让输入框失焦 —— 用户改完直接点保存，值就丢了。
     * 保存/导出/退出前统一调 EditorOps::commitPendingEdit() 兜住。 */
    {
        onNodeSelected(frame);
        const QString probe = QStringLiteral("OPSTEST_ENAME_1");
        m_com->typeIdForTest(probe);
        auto enameOfFrame = [frame]() {
            for (const UiProperty &pp : frame->props) {
                if (pp.name == QLatin1String("id")) {
                    return pp.ename;
                }
            }
            return QString();
        };
        check(QStringLiteral("刚敲完还没提交时，模型里确实还是旧值"),
              enameOfFrame() != probe, enameOfFrame());
        EditorOps::commitPendingEdit();
        check(QStringLiteral("保存前的强制提交能把输入框里的值写进模型"),
              enameOfFrame() == probe, enameOfFrame());
    }

    /* --- 18l. 控件参数的取值范围要真的限住 ---
     * 取值范围是数据驱动的：写在 control.json 的属性里（min/max/maxlength），
     * 提示语 "请输入%1~%2的整数" / "请输入 0~9999 内的整数"。
     * 之前数字框一律 ±9999 / ±99999，等于没限 —— 往 int8 字段里写
     * 30000 也收，生成资源时被截断成别的数。
     *
     * 判据：把面板铺出来，逐个 QSpinBox 看它的 range 是不是收敛的。
     *
     * 位置坐标那四个框单独判（见 Position::setBounds() 的注释）：
     *     宽 <= 父宽、高 <= 父高（无条件）；
     *     NewLayout / NewLayer：X/Y 都是 -999..999；
     *     其余控件：X 是 0..(父宽-自身宽)、Y 是 0..(父高-自身高)。 */
    {
        onNodeSelected(frame);
        QApplication::processEvents();
        int wide = 0, total = 0;
        for (QSpinBox *sp : findChildren<QSpinBox *>()) {
            if (!sp->isVisibleTo(this)) {
                continue;
            }
            const QString on = sp->objectName();
            if (on == QLatin1String("spinX") || on == QLatin1String("spinY")) {
                continue;                       // 坐标单独判
            }
            ++total;
            if (sp->minimum() <= -99999 || sp->maximum() >= 99999) {
                ++wide;
            }
        }
        check(QStringLiteral("数字参数都有收敛的取值范围（没有 ±99999 这种）"),
              wide == 0,
              QStringLiteral("%1/%2 个还是敞开的").arg(wide).arg(total));

        /* 位置坐标：分别拿一个容器（NewLayout/NewLayer）和一个叶子控件来验。 */
        auto geoRange = [this](UiNode *node) {
            onNodeSelected(node);
            QApplication::processEvents();
            /* 旧面板是 deleteLater 拆的，processEvents 不管 DeferredDelete，
             * 不冲一下就会在对象树里撞到上一个节点那份 spinX。 */
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
            struct R { int xlo, xhi, ylo, yhi, wlo, whi, hlo, hhi; bool ok; } r{};
            auto pick = [this](const char *name) -> QSpinBox * {
                for (QSpinBox *s : findChildren<QSpinBox *>(QLatin1String(name))) {
                    if (s->isVisibleTo(this)) {
                        return s;
                    }
                }
                return nullptr;
            };
            QSpinBox *sx = pick("spinX");
            QSpinBox *sy = pick("spinY");
            QSpinBox *sw = pick("spinW");
            QSpinBox *sh = pick("spinH");
            r.ok = sx && sy && sw && sh;
            if (r.ok) {
                r.xlo = sx->minimum(); r.xhi = sx->maximum();
                r.ylo = sy->minimum(); r.yhi = sy->maximum();
                r.wlo = sw->minimum(); r.whi = sw->maximum();
                r.hlo = sh->minimum(); r.hhi = sh->maximum();
            }
            return r;
        };

        /* 只在**当前页**里找 —— onNodeSelected 选不了别的页上的控件，
         * 跨页去选的话量到的还是上一个控件的面板。 */
        UiNode *container = nullptr;
        UiNode *leaf = nullptr;
        page->forEach([&](UiNode *x) {
            const bool isBox = (x->type == QLatin1String("NewLayout")
                                || x->type == QLatin1String("NewLayer"));
            if (x == page || !x->parent || x->parent->rectOf(0).size().isEmpty()) {
                return true;
            }
            if (isBox && !container) {
                container = x;
            }
            if (!isBox && !leaf && !x->rectOf(0).isEmpty()) {
                leaf = x;
            }
            return !(container && leaf);
        });

        if (container) {
            const QSize p = container->parent->rectOf(0).size();
            const auto r = geoRange(container);
            check(QStringLiteral("容器(NewLayout/NewLayer)：宽<=父宽、高<=父高"),
                  r.ok && r.whi == p.width() && r.hhi == p.height(),
                  QStringLiteral("宽上限 %1(父 %2)，高上限 %3(父 %4)")
                      .arg(r.whi).arg(p.width()).arg(r.hhi).arg(p.height()));
            check(QStringLiteral("容器：X/Y 是 -999..999（列表行要能排到父容器外）"),
                  r.ok && r.xlo == -999 && r.xhi == 999
                       && r.ylo == -999 && r.yhi == 999,
                  QStringLiteral("X %1..%2  Y %3..%4")
                      .arg(r.xlo).arg(r.xhi).arg(r.ylo).arg(r.yhi));
        }
        if (leaf) {
            const QSize p = leaf->parent->rectOf(0).size();
            const QSize o = leaf->rectOf(0).size();
            const auto r = geoRange(leaf);
            check(QStringLiteral("叶子控件：宽<=父宽、高<=父高"),
                  r.ok && r.whi == p.width() && r.hhi == p.height(),
                  QStringLiteral("宽上限 %1(父 %2)，高上限 %3(父 %4)")
                      .arg(r.whi).arg(p.width()).arg(r.hhi).arg(p.height()));
            check(QStringLiteral("叶子控件：X/Y 从 0 起，且整块要留在父容器里"),
                  r.ok && r.xlo == 0 && r.ylo == 0
                       && r.xhi == qMax(0, p.width() - o.width())
                       && r.yhi == qMax(0, p.height() - o.height()),
                  QStringLiteral("X %1..%2(期望 0..%3)  Y %4..%5(期望 0..%6)")
                      .arg(r.xlo).arg(r.xhi).arg(qMax(0, p.width() - o.width()))
                      .arg(r.ylo).arg(r.yhi).arg(qMax(0, p.height() - o.height())));
        }

        /* 范围限制要让人看得见 —— 每个数字框都得有气泡提示，
         * 而且提示里要出现它自己的上下限，不能是句放之四海皆准的空话。 */
        {
            onNodeSelected(leaf ? leaf : frame);
            QApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
            int noTip = 0, noRange = 0, seen = 0;
            QString sample;
            for (QSpinBox *sp : findChildren<QSpinBox *>()) {
                if (!sp->isVisibleTo(this)) {
                    continue;
                }
                ++seen;
                const QString tip = sp->toolTip();
                if (tip.isEmpty()) {
                    ++noTip;
                    continue;
                }
                if (!tip.contains(QString::number(sp->maximum()))) {
                    ++noRange;
                }
                if (sample.isEmpty() && sp->objectName() == QLatin1String("spinX")) {
                    sample = tip;
                }
            }
            check(QStringLiteral("每个数字框都有气泡提示"), noTip == 0,
                  QStringLiteral("%1/%2 个没有").arg(noTip).arg(seen));
            check(QStringLiteral("气泡提示里带着这个框自己的上限"), noRange == 0,
                  QStringLiteral("%1/%2 个没带").arg(noRange).arg(seen));
            check(QStringLiteral("坐标提示还说清了上限的来历"),
                  sample.contains(QChar('\n')),
                  sample.isEmpty() ? QStringLiteral("(没取到 X 的提示)")
                                   : QString(sample).replace(QChar('\n'),
                                                             QStringLiteral(" / ")));
        }

        onNodeSelected(frame);
        QApplication::processEvents();

        /* 模板写了 min/max 的，面板上必须照着来。z_order 是 int8 0..128。 */
        UiNode *probe = nullptr;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                if (!probe && x->cssField(0, QStringLiteral("z_order"),
                                          QStringLiteral("default")).isDouble()) {
                    probe = x;
                }
                return !probe;
            });
            if (probe) {
                break;
            }
        }
        if (probe) {
            onNodeSelected(probe);
            QApplication::processEvents();
            bool found = false;
            for (QSpinBox *sp : findChildren<QSpinBox *>()) {
                if (sp->isVisibleTo(this) && sp->minimum() == 0 && sp->maximum() == 128) {
                    found = true;
                    break;
                }
            }
            check(QStringLiteral("z轴坐标按模板限成 0~128（int8）"), found);
        }
    }

    /* --- 18m. 右栏第二列：当前页的布局预览 ---
     * 这一页的价值全在"能看见看不见的东西"上：实测两个真实工程里
     * 74%~81% 的顶层布局是"默认隐藏"的，画布和右栏页面图都画不出来，
     * 以前只能靠工具栏「当前画面」下拉框一个个切名字去认。
     *
     * 所以断言的重点是：隐藏的布局也得画出真内容（forceRoot 生效），
     * 而且这一列和下拉框是同一份数据、点了就同步。 */
    {
        ScenesScreen *sc0 = m_mgr->currentScreen();
        const QVector<UiNode *> screens = sc0 ? sc0->screens() : QVector<UiNode *>();
        check(QStringLiteral("右列张数 = 当前页的顶层布局数"),
              m_pages && m_pages->layoutCountForTest() == screens.size(),
              QStringLiteral("右列 %1 张 / 布局 %2 个")
                  .arg(m_pages ? m_pages->layoutCountForTest() : -1).arg(screens.size()));

        bool sameNodes = true;
        for (int i = 0; i < screens.size(); ++i) {
            if (m_pages->layoutNodeForTest(i) != screens.at(i)) {
                sameNodes = false;
                break;
            }
        }
        check(QStringLiteral("右列的顺序和「当前画面」下拉框一致"), sameNodes);

        /* 默认隐藏的布局在画布和页面预览里都看不到 —— 这一列的全部意义就是
         * 把它们画出来。逐个抓图数亮点：只要有一个非空，forceRoot 就是在起
         * 作用的。不要求每一个都非空 —— 工程里确实可能有空布局，而且这一节
         * 跑在被前面用例改过的工程上。 */
        const QRgb dark = Preview::monoDark().rgb();
        auto litOf = [&](const QImage &img) {
            int lit = 0;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (img.pixel(x, y) != dark) {
                        ++lit;
                    }
                }
            }
            return lit;
        };
        int hidden = -1, hiddenTot = 0, hiddenDrawn = 0, best = 0;
        QString bestName;
        for (int i = 0; i < screens.size(); ++i) {
            if (!screens.at(i)->isDefaultHidden()) {
                continue;
            }
            ++hiddenTot;
            if (hidden < 0) {
                hidden = i;
            }
            const int lit = litOf(m_pages->grabLayoutForTest(i));
            if (lit > 0) {
                ++hiddenDrawn;
            }
            if (lit > best) {
                best = lit;
                bestName = screens.at(i)->name;
            }
        }
        if (hiddenTot > 0) {
            check(QStringLiteral("默认隐藏的布局也画得出内容（否则这一列没意义）"),
                  hiddenDrawn > 0,
                  QStringLiteral("%1/%2 个隐藏布局画出了内容，最多的是 %3（%4 点）")
                      .arg(hiddenDrawn).arg(hiddenTot).arg(bestName).arg(best));
        }
        if (hidden >= 0) {

            /* 点它 = 切到那个画面，和下拉框走同一条路 */
            m_pages->onLayoutClicked(nullptr);          // 空指针不该崩
            emit m_pages->layoutActivated(hidden);      // 只是确认信号存在
            m_mgr->gotoScreen(hidden);
            QApplication::processEvents();
            ScenesScreen *sc1 = m_mgr->currentScreen();
            check(QStringLiteral("点右列那张 = 切到那个画面（和下拉框同步）"),
                  sc1 && sc1->currentScreenIndex() == hidden,
                  QStringLiteral("现在是第 %1 个")
                      .arg(sc1 ? sc1->currentScreenIndex() : -1));
        }

        /* 【必须真的画一遍】两列中间那个大括号是自己 QPainter 画的，坐标要跨
         * 控件换算。第一版用 QWidget::mapTo() 映射到兄弟控件，直接 0xC0000005
         * 崩在 paintEvent 里 —— 只建不画的测试一条都没报。grab() 会强制走
         * 一遍 paintEvent。 */
        {
            const QPixmap pm = m_pages->grab();
            check(QStringLiteral("右栏（含两列之间的大括号）能画出来不崩"),
                  !pm.isNull() && pm.width() > 0 && pm.height() > 0,
                  QStringLiteral("%1x%2").arg(pm.width()).arg(pm.height()));
        }

        /* 换页之后右列要整列换掉 */
        if (m_mgr->model()->pages().size() > 1) {
            const int p0 = m_mgr->currentPage();
            const int p1 = (p0 + 1) % m_mgr->model()->pages().size();
            m_mgr->setCurrentPage(p1);
            m_pages->reloadLayouts();
            QApplication::processEvents();
            ScenesScreen *s1 = m_mgr->currentScreen();
            const int want = s1 ? s1->screens().size() : -1;
            check(QStringLiteral("换页之后右列跟着换"),
                  m_pages->layoutCountForTest() == want,
                  QStringLiteral("页%1 有 %2 个布局，右列 %3 张")
                      .arg(p1).arg(want).arg(m_pages->layoutCountForTest()));
            m_mgr->setCurrentPage(p0);
            m_pages->reloadLayouts();
            QApplication::processEvents();
        }
    }

    /* --- 18n. 像素网格要盖在内容之上 ---
     * 网格原来画在 ScenesScreen::paintEvent 里，那是父控件的背景，Qt 之后才画
     * 子控件；点亮的像素不透明，就把网格盖掉了 —— 只有熄灭的地方看得见网格。
     * 现在挪到铺满画布的覆盖层上、raise 到最上面。
     *
     * 判据不看颜色（亮/灭两种颜色是[全局设置]里可配的），改成**对比**：
     * 同一画面关网格抓一张、开网格抓一张，凡是关网格时点亮的那些像素里，
     * 必须有一部分被网格改掉了 —— 否则就是又被内容盖住了。 */
    {
        ScenesScreen *sc = m_mgr->currentScreen();
        if (sc) {
            const int  z0 = sc->zoom();
            const bool g0 = sc->showGrid();
            sc->setZoom(400);                    // 低于 300% 不画网格
            sc->setShowGrid(false);
            QApplication::processEvents();
            const QImage off = sc->grab().toImage().convertToFormat(QImage::Format_RGB32);
            sc->setShowGrid(true);
            QApplication::processEvents();
            const QImage on = sc->grab().toImage().convertToFormat(QImage::Format_RGB32);

            const QRgb lit = Preview::monoLit().rgb();
            const QRgb dark = Preview::monoDark().rgb();
            const QRgb grid = Preview::monoGrid().rgb();
            int litTot = 0, litGrid = 0, darkTot = 0, darkGrid = 0, wrongColor = 0;
            const int w = qMin(off.width(), on.width());
            const int h = qMin(off.height(), on.height());
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const QRgb a = off.pixel(x, y);
                    const bool changed = (on.pixel(x, y) != a);
                    if (a == lit) {
                        ++litTot;
                        if (changed) {
                            ++litGrid;
                            if (on.pixel(x, y) != grid) {
                                ++wrongColor;
                            }
                        }
                    } else if (a == dark) {
                        ++darkTot;
                        if (changed) {
                            ++darkGrid;
                            if (on.pixel(x, y) != grid) {
                                ++wrongColor;
                            }
                        }
                    }
                }
            }
            check(QStringLiteral("点亮的像素上也画得到网格（不能被内容盖住）"),
                  litTot > 0 && litGrid > 0,
                  QStringLiteral("点亮 %1 点，其中 %2 点被网格改到").arg(litTot).arg(litGrid));
            check(QStringLiteral("熄灭的像素上照旧有网格"),
                  darkTot > 0 && darkGrid > 0,
                  QStringLiteral("熄灭 %1 点，其中 %2 点被网格改到").arg(darkTot).arg(darkGrid));
            /* 亮区暗区必须是**同一个**颜色 —— 早先用 XOR 时两边不同色
             * （暗区 #2C2C2C、亮区 #C3C3C3），现在是[全局设置]里配的实色。 */
            check(QStringLiteral("网格线在点亮和熄灭的像素上是同一个颜色"),
                  wrongColor == 0,
                  QStringLiteral("网格色 %1，%2/%3 点画成了别的颜色")
                      .arg(Preview::monoGrid().name())
                      .arg(wrongColor).arg(litGrid + darkGrid));
            sc->setShowGrid(g0);
            sc->setZoom(z0);
            QApplication::processEvents();
        }
    }

    /* --- 18o. 工具自己建的节点，生成出来不能是零尺寸 ---
     * 用户实测到"页 3 加了背景图片，烧录后整屏不显示"。查下来是旧版
     * 「新建页面」只设了内存里的 UiNode::rect、没建对应的 property，存盘时
     * 页节点连 "property" 键都没写出去；重新打开后页面没有尺寸，StyBuilder
     * 拿不到父矩形，那一页所有控件的 css 左/上/宽/高全留 0 —— 万分比的 0
     * 就是零尺寸，固件照着画什么都不画，而且从编辑器到生成器没有一处报错
     * （编辑器画布对无效 rect 有 128x64 的兜底，所以界面上还看着正常）。
     *
     * 这一条把整条链走一遍：建工程 -> 建页 -> 每种控件各拖一个 -> 存盘 ->
     * 真的跑 StyBuilder -> 回头读 .sty 里每个控件的 css。 */
    {
        const QString dir = QDir::temp().filePath(QStringLiteral("uitools_geom"));
        QDir().mkpath(dir);
        const QString jf = QDir(dir).filePath(QStringLiteral("geom.json"));

        ProjectModel pm;
        /* 和「新建工程」按钮走同一条路：图层/布局从 control.json 模板克隆 */
        const ControlTemplate *lt = m_mgr->library()->byType(QStringLiteral("NewLayer"));
        const ControlTemplate *ot = m_mgr->library()->byType(QStringLiteral("NewLayout"));
        check(QStringLiteral("控件库里有图层/布局的模板"), lt && ot);
        pm.createDefault(QStringLiteral("geom"), QSize(128, 64),
                         lt ? lt->raw : QJsonObject(), ot ? ot->raw : QJsonObject());
        QString err;
        const bool saved = pm.save(jf, &err);
        check(QStringLiteral("新建的空工程存得下来"), saved, err);

        if (saved) {
            sty::Builder b;
            QString e2;
            const bool loaded = b.loadProject(jf, &e2);
            check(QStringLiteral("新建的空工程 StyBuilder 读得进"), loaded, e2);
            /* 控件类型码在 UITools/config/ini/option.ini 里，不加载的话
             * 每个节点都是"认不出控件类型"，记录长度全按 16 兜底，测出来没意义 */
            QString e3;
            const QString ini = QDir(m_mgr->toolsRoot())
                                .filePath(QStringLiteral("config/ini/option.ini"));
            check(QStringLiteral("类型码表 option.ini 读得进"),
                  b.loadOptionIni(ini, &e3), e3);
            if (loaded) {
                sty::Options opt;
                const sty::Output o = b.build(opt);
                check(QStringLiteral("新建的空工程生成得出 .sty"), o.ok, o.error);
                if (o.ok) {
                    /* 逐个控件读 css 的宽/高（万分比），0 就是零尺寸 */
                    const QByteArray &raw = o.sty;
                    int zero = 0, total = 0;
                    QString first;
                    const int npg = quint8(raw.at(17));
                    for (int pi = 0; pi < npg; ++pi) {
                        const int te = 24 + pi * 20;
                        const quint32 off = *reinterpret_cast<const quint32 *>(raw.constData() + te);
                        const quint32 len = *reinterpret_cast<const quint32 *>(raw.constData() + te + 4);
                        int p = int(off) + 28;                 // 跳过窗口记录
                        while (p + 16 < int(off + len) && p + 16 < raw.size()) {
                            const int recLen = quint8(raw.at(p + 3));
                            if (recLen == 0) {
                                break;
                            }
                            const quint32 cssOff =
                                *reinterpret_cast<const quint32 *>(raw.constData() + p + 12);
                            const int a = int(off + cssOff);
                            ++total;
                            if (cssOff == 0) {
                                /* 连 css 块都没有 —— 固件没有几何也没有样式，
                                 * 和几何全 0 一样，屏上什么都不会画 */
                                ++zero;
                                if (first.isEmpty()) {
                                    first = QStringLiteral("页%1 记录@0x%2 没有 css 块")
                                            .arg(pi).arg(p, 0, 16);
                                }
                            } else if (a + 20 <= raw.size()) {
                                const qint32 w =
                                    *reinterpret_cast<const qint32 *>(raw.constData() + a + 12);
                                const qint32 h =
                                    *reinterpret_cast<const qint32 *>(raw.constData() + a + 16);
                                if (w == 0 && h == 0) {
                                    ++zero;
                                    if (first.isEmpty()) {
                                        first = QStringLiteral("页%1 记录@0x%2 css 宽高为 0")
                                                .arg(pi).arg(p, 0, 16);
                                    }
                                }
                            }
                            p += recLen;
                        }
                    }
                    check(QStringLiteral("新建工程：每个控件都有 css 且尺寸非 0（否则屏上不显示）"),
                          total > 0 && zero == 0,
                          QStringLiteral("%1/%2 个是 0，首个 %3").arg(zero).arg(total).arg(first));
                    check(QStringLiteral("新建工程：生成过程没有'取不到父级尺寸'的警告"),
                          o.warnings.filter(QStringLiteral("取不到父级尺寸")).isEmpty(),
                          o.warnings.filter(QStringLiteral("取不到父级尺寸"))
                          .join(QStringLiteral(" / ")));
                    /* 新建工程的图层/布局也得带 ID 号 —— 空的写出来就是
                     * `#define  0X...`，而且业务代码引用不到它。 */
                    check(QStringLiteral("新建工程：每个控件都有唯一 ID 号"),
                          o.warnings.filter(QStringLiteral("没有唯一ID号")).isEmpty(),
                          o.warnings.filter(QStringLiteral("没有唯一ID号"))
                          .join(QStringLiteral(" / ")));
                }
            }
        }
    }

    /* --- 18p. 选中要活过 rebuild() 和树重载 ---
     * 用户实测：改一个图片参数按回车、往当前布局里拖一个新控件，画布当场
     * 散成"这一页所有布局叠在一起"。根因是 rebuild() 会把 m_selected 清掉，
     * 而 applyVisibility() 的"选中即隔离"没了目标就退回全画。要命的是
     * rebuild() 的触发点大多**根本没动结构**：改属性、加控件、换倍率、
     * 改预览配色，全走这一条 —— 等于编辑器一动就散架。 */
    {
        sc->setSolo(true);
        sc->setShowHidden(false);

        /* 找到 layout 所属的那个顶层布局（父节点是图层的那一层），
         * 隔离是按这一层算的。 */
        UiNode *top = layout;
        while (top && top->parent && top->parent->cls != QLatin1String("NewLayer")) {
            top = top->parent;
        }
        auto visibleSiblings = [&]() {
            int shown = 0;
            if (top && top->parent) {
                for (const auto &sib : top->parent->children) {
                    if (sib.second == top) {
                        continue;
                    }
                    sib.second->forEach([&](UiNode *x) {
                        if (BaseForm *sf = sc->formFor(x)) {
                            if (sf->isVisible()) {
                                ++shown;
                            }
                        }
                        return true;
                    });
                }
            }
            return shown;
        };

        onNodeSelected(layout);
        check(QStringLiteral("测试前提：画布上确实选中了这个布局"),
              sc->selectedNode() == layout);
        const int isolated = visibleSiblings();

        /* 1) 光是重建一次，选中不能丢 */
        sc->rebuild();
        check(QStringLiteral("rebuild() 之后选中还在（丢了就退回所有布局叠一起）"),
              sc->selectedNode() == layout);
        check(QStringLiteral("rebuild() 之后画布上那个控件还是选中态"),
              sc->formFor(layout) && sc->formFor(layout)->isSelected());
        check(QStringLiteral("rebuild() 之后隔离还在（同级布局露出来的控件数没变）"),
              visibleSiblings() == isolated,
              QStringLiteral("之前 %1，现在 %2").arg(isolated).arg(visibleSiblings()));

        /* 2) 改属性走的那条路：画布重建 + 树重载。树上的高亮也要留住 ——
         *    高亮一没，用户看到的就是"改个参数选中自己跑了"。 */
        m_tree->selectNode(layout);
        sc->rebuild();
        m_tree->reload();
        check(QStringLiteral("树重载之后高亮还在原来那一项"),
              m_tree->currentNodeForTest() == layout);
        check(QStringLiteral("改属性那条路走完，画布选中还在"),
              sc->selectedNode() == layout);

        /* 3) 换倍率也一样（这条以前是单独实现的，现在并到 rebuild() 里了） */
        const int z0 = sc->zoom();
        sc->setZoom(z0 >= 400 ? 200 : 400);
        check(QStringLiteral("改倍率之后选中还在"), sc->selectedNode() == layout);
        sc->setZoom(z0);

        /* 4) 新建控件：建完要选中**新的那个**。不选的话，什么都没选中时
         *    往布局里拖一个控件，这一页的布局会全画出来叠成一团，
         *    刚拖进去的那个反而看不见。 */
        onNodeSelected(layout);
        const int kidsWas = layout->children.size();
        m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                    QStringLiteral("文字"));
        UiNode *fresh = layout->children.size() == kidsWas + 1
                        ? layout->children.last().second : nullptr;
        check(QStringLiteral("新建的控件建完就是选中的"),
              fresh && sc->selectedNode() == fresh);
        check(QStringLiteral("新建控件之后隔离还在"),
              visibleSiblings() == isolated,
              QStringLiteral("之前 %1，现在 %2").arg(isolated).arg(visibleSiblings()));
        check(QStringLiteral("新建的控件在画布上看得见"),
              fresh && sc->formFor(fresh) && sc->formFor(fresh)->isVisible());
    }

    /* --- 18q. 列表/表格是容器，下面放得进东西 ---
     * 用户实测："为什么我的垂直列表下面不能放其他控件？"
     * 本版的 EditorOps::acceptsChild() 只认 NewLayout，于是列表怎么都收不到
     * 孩子，永远是空的。而既有的两个工程里，列表下面一共挂着 165 个布局
     * （键 listwidget）—— 那就是列表的行/项模板，控件再放进这些行布局里。 */
    if (list) {
        sc->rebuild();
        const QString key = EditorOps::childKeyFor(list);
        check(QStringLiteral("列表的子键是 listwidget（不是 layout）"),
              key == QLatin1String("listwidget"), key);
        check(QStringLiteral("列表装得下布局"), EditorOps::acceptsLayout(list));
        check(QStringLiteral("列表装不下裸控件（孩子必须是行布局）"),
              !EditorOps::acceptsWidget(list));
        check(QStringLiteral("控件谁都装不下"),
              !EditorOps::acceptsLayout(frame) && !EditorOps::acceptsWidget(frame));

        /* 拖一个布局落在列表上 -> 容器就是列表本身，不许被上溯到父布局。
         * 这里直接问规则，不走像素命中 —— 工程里的列表常被父布局裁得
         * 一个像素都点不到，那是 Qt 的裁剪，和落点规则是两回事。 */
        {
            UiNode *t = nullptr;
            const bool ok = sc->resolveDropTargetForTest(
                list, QStringLiteral("NewLayout"), &t);
            check(QStringLiteral("布局能拖进列表里（列表的行就是布局）"),
                  ok && t == list,
                  t ? QStringLiteral("落到了 %1").arg(t->name)
                    : QStringLiteral("落不下去"));

            t = nullptr;
            const bool ok2 = sc->resolveDropTargetForTest(
                list, QStringLiteral("NewFrame"), &t);
            check(QStringLiteral("控件拖到列表上会落到列表的父布局，不塞进列表"),
                  ok2 && t == list->parent,
                  t ? t->name : QStringLiteral("落不下去"));
        }

        /* ★ 选中列表点「新建布局」：建进列表里。
         * 另一种做法是加到列表的父级、加行只能走右键菜单「添加行」，
         * 这里选前者，直觉上更顺。 */
        {
            const int rowsWas0 = list->children.size();
            const int sibsWas = list->parent ? list->parent->children.size() : -1;
            onNodeSelected(list);
            m_components->onCreateNewLayout();
            check(QStringLiteral("选中列表点「新建布局」：布局落进列表里"),
                  list->children.size() == rowsWas0 + 1
                  && list->parent && list->parent->children.size() == sibsWas,
                  QStringLiteral("列表 %1->%2，父级 %3->%4")
                  .arg(rowsWas0).arg(list->children.size()).arg(sibsWas)
                  .arg(list->parent ? list->parent->children.size() : -1));
            check(QStringLiteral("这样建出来的行也挂 listwidget 键"),
                  list->children.last().first == QLatin1String("listwidget")
                  && list->children.last().second->cls == QLatin1String("NewLayout"),
                  QStringLiteral("%1/%2").arg(list->children.last().first,
                                              list->children.last().second->cls));
        }

        /* 再走一遍拖放那条路 */
        const int rowsWas = list->children.size();
        m_components->createDropped(list, QStringLiteral("NewLayout"),
                                    QStringLiteral("NewLayout"), QPoint(0, 0));
        const bool rowMade = list->children.size() == rowsWas + 1;
        check(QStringLiteral("列表下面建得出行布局"), rowMade);
        if (rowMade) {
            check(QStringLiteral("行布局挂在 listwidget 键下"),
                  list->children.last().first == QLatin1String("listwidget"),
                  list->children.last().first);
            UiNode *row = list->children.last().second;
            sc->rebuild();
            onNodeSelected(row);
            const int inRow = row->children.size();
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("Text"), QStringLiteral("文字"));
            check(QStringLiteral("行布局里放得进控件"),
                  row->children.size() == inRow + 1);

            /* 选中列表本身点「新建控件」：加到列表的**父级**，
             * 不是钻进列表（同 hostForNewControl 里那三条 isClass）。*/
            UiNode *listHost = list->parent;
            const int hostWas = listHost ? listHost->children.size() : -1;
            const int listWas = list->children.size();
            onNodeSelected(list);
            EditorOps::clearLastMessage();
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("Text"), QStringLiteral("文字"));
            check(QStringLiteral("选中列表建控件：加到列表的父级，不钻进列表"),
                  listHost && listHost->children.size() == hostWas + 1
                  && list->children.size() == listWas,
                  QStringLiteral("父级 %1->%2，列表 %3->%4")
                  .arg(hostWas).arg(listHost ? listHost->children.size() : -1)
                  .arg(listWas).arg(list->children.size()));
        }

        /* --- 存盘 / 重新打开 / 生成资源，形状不能变 ---
         * 用户的要求：打开的 json 工程和生成的 UI 资源都要保持兼容。
         * 列表加行这条路走完，落盘的还得是兼容的那个形状：
         * NewList --listwidget--> NewLayout，而且生成出来的 .sty 里
         * 这些行都得有非零几何（零尺寸 = 屏上不显示，见 18o）。 */
        {
            const QString dir = QDir::temp().filePath(QStringLiteral("uitools_list"));
            QDir().mkpath(dir);
            const QString jf = QDir(dir).filePath(QStringLiteral("list.json"));
            QString err;
            const bool saved = m_mgr->model()->save(jf, &err);
            check(QStringLiteral("加过行的工程存得下来"), saved, err);

            if (saved) {
                ProjectModel re;
                const bool reloaded = re.load(jf, &err);
                check(QStringLiteral("加过行的工程重新打开得了"), reloaded, err);
                if (reloaded) {
                    int lists = 0, rows = 0, badKey = 0, badCls = 0;
                    for (UiNode *pg : re.pages()) {
                        pg->forEach([&](UiNode *x) {
                            if (x->cls != QLatin1String("NewList")) {
                                return true;
                            }
                            ++lists;
                            for (const auto &c : x->children) {
                                ++rows;
                                if (c.first != QLatin1String("listwidget")) {
                                    ++badKey;
                                }
                                if (c.second->cls != QLatin1String("NewLayout")) {
                                    ++badCls;
                                }
                            }
                            return true;
                        });
                    }
                    check(QStringLiteral("重新打开后：列表的孩子全在 listwidget 键下"),
                          lists > 0 && rows > 0 && badKey == 0,
                          QStringLiteral("%1 个列表 %2 行，键不对 %3")
                          .arg(lists).arg(rows).arg(badKey));
                    check(QStringLiteral("重新打开后：列表的孩子全是 NewLayout（只这一种）"),
                          badCls == 0, QStringLiteral("类不对 %1 个").arg(badCls));
                }

                sty::Builder b;
                QString e2;
                const bool built = b.loadProject(jf, &e2);
                check(QStringLiteral("加过行的工程生成得了资源"), built, e2);
                if (built) {
                    sty::Options opt;
                    const sty::Output o = b.build(opt);
                    check(QStringLiteral("生成资源没报错"), o.ok, o.error);
                    check(QStringLiteral("生成过程没有'取不到父级尺寸'的警告"),
                          o.warnings.filter(QStringLiteral("取不到父级尺寸")).isEmpty(),
                          o.warnings.join(QStringLiteral(" / ")));
                }
            }
        }
    }

    /* --- 18r. 水平列表 / 表格控件走的是同一套容器规则 ---
     * 18q 只验了工程里现成的垂直列表。水平列表的 -class 也是 NewList，
     * 天然同路；表格是另一个类，得单独验一遍。两个工程都没有表格样本，
     * 所以这里现场建一个。
     * 表格和列表同为 option.ini 类型码 5，固件 ui_grid.h 里每一项也是
     * `struct layout_info *` —— 和列表的行是同一种东西，规则没道理分家。 */
    {
        auto probeBox = [&](const QString &cls, const QString &type,
                            const QString &label) {
            onNodeSelected(layout);
            const int was = layout->children.size();
            m_components->createDropped(layout, cls, type, QPoint(0, 0));
            if (layout->children.size() != was + 1) {
                check(QStringLiteral("%1：建得出来").arg(label), false,
                      QStringLiteral("%1 -> %2").arg(was).arg(layout->children.size()));
                return;
            }
            UiNode *box = layout->children.last().second;
            check(QStringLiteral("%1：类对得上").arg(label), box->cls == cls, box->cls);
            check(QStringLiteral("%1：装得下布局").arg(label),
                  EditorOps::acceptsLayout(box));
            check(QStringLiteral("%1：装不下裸控件（项必须是布局）").arg(label),
                  !EditorOps::acceptsWidget(box));
            check(QStringLiteral("%1：子键是 listwidget").arg(label),
                  EditorOps::childKeyFor(box) == QLatin1String("listwidget"),
                  EditorOps::childKeyFor(box));

            UiNode *t = nullptr;
            check(QStringLiteral("%1：布局拖上去进容器").arg(label),
                  sc->resolveDropTargetForTest(box, QStringLiteral("NewLayout"), &t)
                  && t == box,
                  t ? t->name : QStringLiteral("落不下去"));
            t = nullptr;
            check(QStringLiteral("%1：控件拖上去落到它的父布局").arg(label),
                  sc->resolveDropTargetForTest(box, QStringLiteral("NewFrame"), &t)
                  && t == box->parent,
                  t ? t->name : QStringLiteral("落不下去"));

            sc->rebuild();
            onNodeSelected(box);
            const int rows = box->children.size();
            m_components->onCreateNewLayout();
            const bool grew = box->children.size() == rows + 1;
            check(QStringLiteral("%1：选中它点「新建布局」，布局落进去了").arg(label),
                  grew, QStringLiteral("%1 -> %2").arg(rows).arg(box->children.size()));
            if (grew) {
                check(QStringLiteral("%1：这一项挂 listwidget 键且是 NewLayout").arg(label),
                      box->children.last().first == QLatin1String("listwidget")
                      && box->children.last().second->cls == QLatin1String("NewLayout"),
                      QStringLiteral("%1/%2").arg(box->children.last().first,
                                                  box->children.last().second->cls));
            }

            /* 选中它点「新建控件」：落到它的父级，不钻进去 */
            UiNode *host = box->parent;
            const int hostWas = host ? host->children.size() : -1;
            const int inBox = box->children.size();
            onNodeSelected(box);
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("Text"), QStringLiteral("文字"));
            check(QStringLiteral("%1：选中它点「新建控件」落到父级").arg(label),
                  host && host->children.size() == hostWas + 1
                  && box->children.size() == inBox,
                  QStringLiteral("父级 %1->%2，它自己 %3->%4")
                  .arg(hostWas).arg(host ? host->children.size() : -1)
                  .arg(inBox).arg(box->children.size()));
        };

        probeBox(QStringLiteral("NewList"), QStringLiteral("HorizontalList"),
                 QStringLiteral("水平列表"));
        probeBox(QStringLiteral("NewGrid"), QStringLiteral("NewGrid"),
                 QStringLiteral("表格控件"));

        /* 这两种新容器也得存得下来、读得回去、生成得出资源 */
        {
            const QString dir = QDir::temp().filePath(QStringLiteral("uitools_box"));
            QDir().mkpath(dir);
            const QString jf = QDir(dir).filePath(QStringLiteral("box.json"));
            QString err;
            const bool saved = m_mgr->model()->save(jf, &err);
            check(QStringLiteral("带水平列表/表格的工程存得下来"), saved, err);
            if (saved) {
                ProjectModel re;
                const bool reloaded = re.load(jf, &err);
                check(QStringLiteral("带水平列表/表格的工程重新打开得了"), reloaded, err);
                if (reloaded) {
                    int boxes = 0, items = 0, bad = 0;
                    for (UiNode *pg : re.pages()) {
                        pg->forEach([&](UiNode *x) {
                            if (x->cls != QLatin1String("NewList")
                                && x->cls != QLatin1String("NewGrid")) {
                                return true;
                            }
                            ++boxes;
                            for (const auto &c : x->children) {
                                ++items;
                                if (c.first != QLatin1String("listwidget")
                                    || c.second->cls != QLatin1String("NewLayout")) {
                                    ++bad;
                                }
                            }
                            return true;
                        });
                    }
                    check(QStringLiteral("重新打开后：列表和表格的项全是 listwidget/NewLayout"),
                          boxes > 0 && items > 0 && bad == 0,
                          QStringLiteral("%1 个容器 %2 项，不合规 %3")
                          .arg(boxes).arg(items).arg(bad));
                }
                sty::Builder b;
                QString e2;
                const bool ok = b.loadProject(jf, &e2);
                check(QStringLiteral("带水平列表/表格的工程生成得了资源"), ok, e2);
                if (ok) {
                    sty::Options opt;
                    const sty::Output o = b.build(opt);
                    check(QStringLiteral("生成没报错"), o.ok, o.error);
                    check(QStringLiteral("生成没有'取不到父级尺寸'的警告"),
                          o.warnings.filter(QStringLiteral("取不到父级尺寸")).isEmpty(),
                          o.warnings.join(QStringLiteral(" / ")));
                }
            }
        }
    }

    /* --- 18s. 水平列表 -> 行布局 -> 图片：预览要画出来，资源要带上 ---
     * 用户实测："水平列表里面建一个布局，布局下面放一个图片，预览看不到内容,
     * 也不确定会不会导出到资源里面"。两头各查一遍：
     *   预览 —— Preview::contentOf() 出不出图 + 画布上那块区域有没有点亮像素；
     *   资源 —— 这张图有没有进 Resbuilder.xml（那就是交给 ResBuilder 的清单）。 */
    {
        onNodeSelected(layout);
        const int was = layout->children.size();
        m_components->createDropped(layout, QStringLiteral("NewList"),
                                    QStringLiteral("HorizontalList"), QPoint(0, 0));
        UiNode *hl = layout->children.size() == was + 1
                     ? layout->children.last().second : nullptr;
        check(QStringLiteral("18s 前提：建得出水平列表"), hl != nullptr);

        UiNode *row = nullptr;
        if (hl) {
            sc->rebuild();
            onNodeSelected(hl);
            m_components->onCreateNewLayout();
            row = hl->children.isEmpty() ? nullptr : hl->children.last().second;
            check(QStringLiteral("18s 前提：水平列表里建得出行布局"), row != nullptr);
        }

        UiNode *img = nullptr;
        if (row) {
            sc->rebuild();
            onNodeSelected(row);
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("ImageList"),
                                        QStringLiteral("图片"));
            img = row->children.isEmpty() ? nullptr : row->children.last().second;
            check(QStringLiteral("18s 前提：行布局里建得出图片控件"), img != nullptr);
        }

        QString rel;
        if (img) {
            /* 挑工程里现成的一张图，走和属性面板同一个弹窗 */
            ImageListView dlg(this);
            dlg.setProjectDir(QFileInfo(m_mgr->model()->filePath()).absolutePath());
            if (dlg.pickForTest(0)) {
                rel = dlg.selected();
            }
            check(QStringLiteral("18s 前提：挑得到一张图"), !rel.isEmpty(), rel);
            for (UiProperty &p : img->props) {
                if (p.name != QLatin1String("normal_image")) {
                    continue;
                }
                QJsonArray lst;
                lst.append(rel);
                p.raw.insert(QStringLiteral("list"), lst);
                p.dirty = true;
                img->markDirty();
                break;
            }
        }

        if (img && !rel.isEmpty()) {
            /* ---- 预览这一头 ---- */
            const QPixmap pm = Preview::contentOf(img, Preview::monoLit());
            check(QStringLiteral("图片控件出得了预览图"),
                  !pm.isNull() && pm.width() > 0 && pm.height() > 0,
                  QStringLiteral("%1x%2").arg(pm.width()).arg(pm.height()));

            sc->rebuild();
            onNodeSelected(img);
            BaseForm *hf = sc->formFor(hl);
            BaseForm *rf = sc->formFor(row);
            BaseForm *imf = sc->formFor(img);
            check(QStringLiteral("画布上三层都建出来了"),
                  hf && rf && imf);
            if (hf && rf && imf) {
                check(QStringLiteral("水平列表在画布上有尺寸"),
                      hf->width() > 0 && hf->height() > 0 && hf->isVisible(),
                      QStringLiteral("%1,%2 %3x%4 可见=%5")
                      .arg(hf->x()).arg(hf->y()).arg(hf->width()).arg(hf->height())
                      .arg(hf->isVisible()));
                check(QStringLiteral("行布局在画布上有尺寸"),
                      rf->width() > 0 && rf->height() > 0 && rf->isVisible(),
                      QStringLiteral("%1,%2 %3x%4 可见=%5")
                      .arg(rf->x()).arg(rf->y()).arg(rf->width()).arg(rf->height())
                      .arg(rf->isVisible()));
                check(QStringLiteral("图片控件在画布上有尺寸"),
                      imf->width() > 0 && imf->height() > 0 && imf->isVisible(),
                      QStringLiteral("%1,%2 %3x%4 可见=%5")
                      .arg(imf->x()).arg(imf->y()).arg(imf->width()).arg(imf->height())
                      .arg(imf->isVisible()));

                check(QStringLiteral("18s 诊断：三层的逻辑矩形 / 倍率 / sizehw"),
                      true,
                      QStringLiteral("列表 %1x%2  行 %3x%4  图片 %5x%6  倍率 %7  sizehw %8")
                      .arg(hl->rect.width()).arg(hl->rect.height())
                      .arg(row->rect.width()).arg(row->rect.height())
                      .arg(img->rect.width()).arg(img->rect.height())
                      .arg(sc->zoom())
                      .arg(hl->extraValue(QStringLiteral("sizehw")).toInt(-1)));

                /* 【看用户实际看到的那一块】抓图片控件自己没用 —— Qt 会把它
                 * 裁在行布局、行布局又裁在列表里，控件自己那张图是完整的，
                 * 用户看到的却可能只剩一条边。所以抓**列表**这一层。 */
                const QImage listShot = hf->grab().toImage();
                const QRgb lrgb = Preview::monoLit().rgb();
                int litOnList = 0;
                for (int y = 0; y < listShot.height(); ++y) {
                    for (int x = 0; x < listShot.width(); ++x) {
                        if ((listShot.pixel(x, y) | 0xFF000000u) == lrgb) {
                            ++litOnList;
                        }
                    }
                }
                check(QStringLiteral("列表这一层上看得见图片内容（用户看到的就是这块）"),
                      litOnList > 0,
                      QStringLiteral("%1x%2 点亮 %3 个")
                      .arg(listShot.width()).arg(listShot.height()).arg(litOnList));

                const QImage shot = imf->grab().toImage();
                const QRgb l = Preview::monoLit().rgb();
                int lit = 0;
                for (int y = 0; y < shot.height(); ++y) {
                    for (int x = 0; x < shot.width(); ++x) {
                        if ((shot.pixel(x, y) | 0xFF000000u) == l) {
                            ++lit;
                        }
                    }
                }
                check(QStringLiteral("图片控件自己画出了内容（有点亮像素）"),
                      lit > 0,
                      QStringLiteral("%1x%2 点亮 %3 个")
                      .arg(shot.width()).arg(shot.height()).arg(lit));
            }

            /* ---- 资源这一头 ---- */
            const QString dir = QDir::temp().filePath(QStringLiteral("uitools_hlist"));
            QDir().mkpath(dir);
            const QString jf = QDir(dir).filePath(QStringLiteral("hlist.json"));
            QString err;
            if (m_mgr->model()->save(jf, &err)) {
                sty::Builder b;
                QString e2;
                const bool ok = b.loadProject(jf, &e2);
                check(QStringLiteral("带这张图的工程生成得了资源"), ok, e2);
                if (ok) {
                    sty::Options opt;
                    const sty::Output o = b.build(opt);
                    check(QStringLiteral("生成没报错"), o.ok, o.error);
                    const QString xml = QString::fromUtf8(o.resbuilderXml);
                    const QString leaf = QFileInfo(rel).fileName();
                    check(QStringLiteral("这张图进了 Resbuilder.xml（会被打进资源）"),
                          xml.contains(leaf, Qt::CaseInsensitive),
                          QStringLiteral("找 %1，清单 %2 字节").arg(leaf).arg(xml.size()));
                }
            } else {
                check(QStringLiteral("带这张图的工程存得下来"), false, err);
            }
        }
    }

    /* --- 18t. 列表格子参数改完，预览当场跟着变；加的行叫中文名 ---
     * 用户实测两条：
     *   "列表右键参数设置，例如行高或者间隔修改后，预览没有立刻生效"
     *   "列表控件添加的行或者列，控件名字是英文名，正常新建控件是中文名字"
     * 前者根因：那几个处理函数只 setExtra() + update() —— 没重排各行
     * （relayoutRows 当时只有滚轮翻行才跑）、没更新各行节点的 rect、
     * 也没往上发信号，右栏预览和脏标志都不动。
     * 后者根因：onAddManyLine() 的退化分支直接现搭了个 -name "NewLayout"。 */
    if (list) {
        sc->rebuild();

        /* 至少得有两行才看得出"格子变了" */
        if (list->children.size() < 2) {
            if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
                lf->onAddManyLine();
                sc->rebuild();
            }
        }

        auto cellsMatch = [&]() {
            for (int i = 0; i < list->children.size(); ++i) {
                if (list->children.at(i).second->rect
                    != EditorOps::cellRectFor(list, i)) {
                    return false;
                }
            }
            return !list->children.isEmpty();
        };

        const int size0 = list->extraValue(QStringLiteral("sizehw")).toInt(16);
        const int space0 = list->extraValue(QStringLiteral("space")).toInt(0);
        QVector<QRect> before;
        for (const auto &c : list->children) {
            before.append(c.second->rect);
        }

        /* ---- 改行高 ---- */
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            lf->setCellSize(size0 + 7);
        }
        QVector<QRect> after;
        for (const auto &c : list->children) {
            after.append(c.second->rect);
        }
        check(QStringLiteral("改行高之后各行的矩形当场就变了"),
              !before.isEmpty() && after != before,
              QStringLiteral("改前 %1 改后 %2")
              .arg(before.isEmpty() ? QString() : QString::number(before.first().height()))
              .arg(after.isEmpty() ? QString() : QString::number(after.first().height())));
        check(QStringLiteral("各行矩形 == 按新参数算出来的格子"), cellsMatch());

        /* 画布上的行控件也得跟着变（倍率 100 时就是 1:1） */
        sc->rebuild();
        if (!list->children.isEmpty()) {
            BaseForm *rf = sc->formFor(list->children.first().second);
            const QRect want = EditorOps::cellRectFor(list, 0);
            const int z = qMax(1, sc->zoom());
            check(QStringLiteral("画布上第一行的尺寸也跟着变"),
                  rf && rf->width() == want.width() * z / 100
                     && rf->height() == want.height() * z / 100,
                  rf ? QStringLiteral("控件 %1x%2  期望 %3x%4  倍率 %5")
                       .arg(rf->width()).arg(rf->height())
                       .arg(want.width() * z / 100).arg(want.height() * z / 100).arg(z)
                     : QStringLiteral("画布上找不到这一行"));
        }

        /* ---- 改间隔，而且要真的落到资源里 ----
         * 【间距是怎么生效的】固件那边**没有** sizehw / space 这两个字段：
         * 类型码 5 的资源结构体是
         *     struct ui_grid_info { head; u8 page_mode; s8 highlight_index;
         *                           action*; layout_info *info; }
         * StyBuilder 写的也
         * 正是这四样（rec[16]=scroll_mode、rec[17]=highlight_index、
         * @20 action、@24 子指针）。`interval` 那个字段是 ui_browser_info /
         * ui_animation_info 的，不是列表的。
         *
         * 所以行距在设备上完全由**每一行自己的 rect** 决定 —— sizehw/space
         * 只是编辑器算格子用的，算完落在行的矩形上。这也正是这个 bug 要命的
         * 地方：改完不重排行，那这个参数**对资源一点影响都没有**。
         * 下面就是拿生成出来的 .sty 字节来证明它现在真的生效了。 */
        auto buildSty = [&]() -> QByteArray {
            const QString d = QDir::temp().filePath(QStringLiteral("uitools_space"));
            QDir().mkpath(d);
            const QString jf = QDir(d).filePath(QStringLiteral("x.json"));
            QString e;
            if (!m_mgr->model()->save(jf, &e)) {
                return QByteArray();
            }
            sty::Builder b;
            if (!b.loadProject(jf, &e)) {
                return QByteArray();
            }
            sty::Options opt;
            const sty::Output o = b.build(opt);
            return o.ok ? o.sty : QByteArray();
        };
        const QByteArray styBefore = buildSty();

        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            lf->setCellSpace(space0 + 3);
        }
        check(QStringLiteral("改间隔之后各行也重新排过"), cellsMatch());

        const QByteArray styAfter = buildSty();
        check(QStringLiteral("改间隔真的改到资源里了（.sty 字节变了）"),
              !styBefore.isEmpty() && !styAfter.isEmpty() && styBefore != styAfter,
              QStringLiteral("改前 %1 字节 / 改后 %2 字节 / %3")
              .arg(styBefore.size()).arg(styAfter.size())
              .arg(styBefore == styAfter ? QStringLiteral("一模一样")
                                         : QStringLiteral("有差异")));
        check(QStringLiteral("而且生成两遍是稳定的（同样的模型出同样的字节）"),
              buildSty() == styAfter);

        /* ---- 换滚动方向：格子从"整宽 x sizehw"变成"sizehw x 整高" ---- */
        const bool vert0 = list->extraValue(QStringLiteral("orientation")).toString()
                           != QLatin1String("Horizontal");
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            lf->setOrientation(!vert0);
        }
        check(QStringLiteral("换滚动方向之后各行的格子也重算了"), cellsMatch());
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            lf->setOrientation(vert0);
            lf = qobject_cast<NewList *>(sc->formFor(list));
            if (lf) {
                lf->setCellSpace(space0);
            }
            lf = qobject_cast<NewList *>(sc->formFor(list));
            if (lf) {
                lf->setCellSize(size0);
            }
        }
        check(QStringLiteral("参数还原之后格子也跟着还原"), cellsMatch());

        /* ---- 加的行要叫中文名 ---- */
        sc->rebuild();
        if (auto *lf = qobject_cast<NewList *>(sc->formFor(list))) {
            const int n0 = list->children.size();
            lf->onAddManyLine();
            const bool grew = list->children.size() == n0 + 1;
            check(QStringLiteral("添加行加得进去"), grew);
            if (grew) {
                UiNode *row = list->children.last().second;
                check(QStringLiteral("添加的行叫中文名（布局_N），不是 NewLayout"),
                      !row->name.contains(QLatin1String("NewLayout"))
                      && row->name.startsWith(QStringLiteral("布局")),
                      row->name);
                check(QStringLiteral("添加的行 caption 也是「布局」"),
                      row->caption == QStringLiteral("布局"), row->caption);
                check(QStringLiteral("添加的行在同一个列表里不重名"),
                      [&]() {
                          QSet<QString> seen;
                          for (const auto &c : list->children) {
                              if (seen.contains(c.second->name)) {
                                  return false;
                              }
                              seen.insert(c.second->name);
                          }
                          return true;
                      }());
            }
        }
    }

    /* --- 18u. 三条路建出来的布局，参数集必须一模一样 ---
     * 用户实测："右键添加行新增的布局参数比正常的少，列表下点新建布局的
     * 参数又和图层下的一样"。
     * 先把既有工程里是什么样查清楚：167 个列表行 vs 65 个图层下的布局，
     * property 名单（id / element_css / action）、element_css 的字段
     * （align / invisible / flags / rect / background_color /
     * background_image / border）和状态数（都是 1）**完全一致** ——
     * 行布局和普通布局就是同一套参数，没有"列表专属"的那一份。
     * 所以本版三条建布局的路（图层下新建 / 列表下新建 / 右键添加行）
     * 必须给出同一个参数集，而且要和工程里现成的那些行对得上。 */
    {
        auto sig = [](UiNode *n) {
            QStringList names, css;
            int states = 0;
            for (const UiProperty &p : n->props) {
                names << (p.name.isEmpty() ? QStringLiteral("(裸)") : p.name);
                if (p.name != QLatin1String("element_css")) {
                    continue;
                }
                const QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
                states = st.size();
                if (!st.isEmpty()) {
                    for (const QJsonValue &f : st.at(0).toArray()) {
                        css << f.toObject().value(QStringLiteral("-name")).toString();
                    }
                }
            }
            return QStringLiteral("[%1] css[%2] 状态%3")
                   .arg(names.join(QLatin1Char('/')), css.join(QLatin1Char('/')))
                   .arg(states);
        };

        /* 基准：图层下点「新建布局」 */
        onNodeSelected(layer);
        const int layerKids = layer->children.size();
        m_components->onCreateNewLayout();
        UiNode *refRow = layer->children.size() == layerKids + 1
                         ? layer->children.last().second : nullptr;
        check(QStringLiteral("18u 前提：图层下建得出布局"), refRow != nullptr);

        if (refRow) {
            const QString ref = sig(refRow);

            /* 工程里现成的行 —— 新建出来的必须和它一样 */
            if (list && !list->children.isEmpty()) {
                UiNode *existingRow = list->children.first().second;
                check(QStringLiteral("列表行和图层下的布局是同一套参数"),
                      sig(existingRow) == ref,
                      QStringLiteral("现成行 %1  图层下 %2").arg(sig(existingRow), ref));
            }

            /* 路 A：空列表右键「添加行」（走模板那条，以前是现搭空壳） */
            onNodeSelected(layout);
            m_components->createDropped(layout, QStringLiteral("NewList"),
                                        QStringLiteral("VerticalList"), QPoint(0, 0));
            UiNode *emptyList = layout->children.last().second;
            check(QStringLiteral("18u 前提：建得出一个空列表"),
                  emptyList && emptyList->cls == QLatin1String("NewList")
                  && emptyList->children.isEmpty(),
                  emptyList ? QStringLiteral("%1 个孩子").arg(emptyList->children.size())
                            : QStringLiteral("没建出来"));
            sc->rebuild();
            if (auto *ef = qobject_cast<NewList *>(sc->formFor(emptyList))) {
                ef->onAddManyLine();
            }
            check(QStringLiteral("空列表加得出第一行"),
                  emptyList && emptyList->children.size() == 1);
            if (emptyList && emptyList->children.size() == 1) {
                UiNode *rowA = emptyList->children.first().second;
                check(QStringLiteral("空列表「添加行」的参数集 == 图层下新建的"),
                      sig(rowA) == ref,
                      QStringLiteral("添加行 %1").arg(sig(rowA)));
            }

            /* 路 B：同一个列表上再「添加行」（走克隆那条） */
            sc->rebuild();
            if (auto *ef = qobject_cast<NewList *>(sc->formFor(emptyList))) {
                ef->onAddManyLine();
            }
            if (emptyList && emptyList->children.size() == 2) {
                check(QStringLiteral("克隆出来的第二行参数集也一样"),
                      sig(emptyList->children.last().second) == ref,
                      sig(emptyList->children.last().second));
            }

            /* 路 C：选中列表点「新建布局」 */
            sc->rebuild();
            onNodeSelected(emptyList);
            const int was = emptyList ? emptyList->children.size() : 0;
            m_components->onCreateNewLayout();
            if (emptyList && emptyList->children.size() == was + 1) {
                check(QStringLiteral("列表下点「新建布局」的参数集也一样"),
                      sig(emptyList->children.last().second) == ref,
                      sig(emptyList->children.last().second));
            } else {
                check(QStringLiteral("列表下点「新建布局」建得出来"), false);
            }

            /* 三条路建出来的都得有 ID 号，否则生成资源时引用不到 */
            int noEname = 0;
            if (emptyList) {
                for (const auto &c : emptyList->children) {
                    bool has = false;
                    for (const UiProperty &p : c.second->props) {
                        if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
                            has = true;
                        }
                    }
                    if (!has) {
                        ++noEname;
                    }
                }
            }
            check(QStringLiteral("三条路建出来的行都带唯一 ID 号"), noEname == 0,
                  QStringLiteral("%1 个没有").arg(noEname));
        }
    }

    /* --- 18v. 全量自检：每种控件 x 每条建节点的路 ---
     * 之前是"用户报一个、修一个"，漏得没完。这一条把不变量一次性立起来，
     * 全部从既有的两份工程（797 个节点）和 control.json 统计出来：
     *
     *   【硬性】违反了就是坏数据，生成出来烧进去要出事
     *     · 认得出类型（control.json 有模板）
     *     · 有 element_css                —— 没有就是零尺寸，整屏不显示（§12）
     *     · 有 id 属性且 ename 非空       —— 空的写出 `#define  0X...`，编译炸
     *     · ename 全工程唯一              —— 撞了就是两个宏定义打架
     *     · rect 有效且宽高都 > 0
     *     · 挂的 json 键 ∈ {layer, layout, widget, listwidget}
     *     · 父子关系 ∈ 既有工程里出现过的组合
     *
     *   【软性】既有工程里自己就有 2% 这种，只报数不判失败
     *     · 属性名单和模板不完全一致（有 9 个 ImageList 只有 id+element_css）
     *     · css 状态数和模板不一致（有 8 个 Time 是用户自己加的状态）
     *
     * 新建出来的节点按**硬性 + 严格模板一致**要求；已有工程只查硬性。 */
    {
        const ControlLibrary *lib = m_mgr->library();

        /* 既有工程 797 个节点统计出来的父子组合（父class, 键, 子class） */
        auto pairAllowed = [](const QString &pc, const QString &key,
                              const QString &cc) {
            if (pc == QLatin1String("ScenesScreen")) {
                return key == QLatin1String("layer") && cc == QLatin1String("NewLayer");
            }
            if (pc == QLatin1String("NewLayer")) {
                return key == QLatin1String("layout") && cc == QLatin1String("NewLayout");
            }
            if (pc == QLatin1String("NewLayout")) {
                return key == QLatin1String("layout")
                       && (cc == QLatin1String("NewFrame")
                           || cc == QLatin1String("NewLayout")
                           || cc == QLatin1String("NewList")
                           || cc == QLatin1String("NewGrid"));
            }
            if (pc == QLatin1String("NewList") || pc == QLatin1String("NewGrid")) {
                return key == QLatin1String("listwidget")
                       && cc == QLatin1String("NewLayout");
            }
            return false;       // NewFrame 是叶子，不该有孩子
        };

        auto enameOf = [](UiNode *n) {
            for (const UiProperty &p : n->props) {
                if (p.name == QLatin1String("id")) {
                    return p.ename;
                }
            }
            return QString();
        };
        auto hasIdProp = [](UiNode *n) {
            for (const UiProperty &p : n->props) {
                if (p.name == QLatin1String("id")) {
                    return true;
                }
            }
            return false;
        };
        auto propNames = [](UiNode *n) {
            QStringList v;
            for (const UiProperty &p : n->props) {
                v << p.name;
            }
            return v;
        };
        auto cssShape = [](UiNode *n, int *states) {
            QStringList v;
            *states = 0;
            for (const UiProperty &p : n->props) {
                if (p.name != QLatin1String("element_css")) {
                    continue;
                }
                const QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
                *states = st.size();
                if (!st.isEmpty()) {
                    for (const QJsonValue &f : st.at(0).toArray()) {
                        v << f.toObject().value(QStringLiteral("-name")).toString();
                    }
                }
                break;
            }
            return v;
        };

        /* ---- 硬性检查：一个节点一串问题 ---- */
        auto hardCheck = [&](UiNode *n, const QString &key, UiNode *par) {
            QStringList bad;
            if (!lib->byType(n->type)) {
                bad << QStringLiteral("认不出类型");
            }
            if (!n->findProp(QStringLiteral("element_css"))) {
                bad << QStringLiteral("没有 element_css");
            }
            if (!hasIdProp(n)) {
                bad << QStringLiteral("没有 id 属性");
            } else if (enameOf(n).isEmpty()) {
                bad << QStringLiteral("ID号是空的");
            }
            if (!n->rect.isValid() || n->rect.width() <= 0 || n->rect.height() <= 0) {
                bad << QStringLiteral("矩形无效 %1x%2")
                       .arg(n->rect.width()).arg(n->rect.height());
            }
            static const QStringList kKeys = { QStringLiteral("layer"),
                                               QStringLiteral("layout"),
                                               QStringLiteral("widget"),
                                               QStringLiteral("listwidget") };
            if (!kKeys.contains(key)) {
                bad << QStringLiteral("子键 '%1' 不在允许的那四个里").arg(key);
            } else if (par && !pairAllowed(par->cls, key, n->cls)) {
                bad << QStringLiteral("父子组合 %1 --%2--> %3 不是允许的组合")
                       .arg(par->cls, key, n->cls);
            }
            return bad;
        };

        /* ---- 扫全工程 ---- */
        auto auditAll = [&](const QString &tag) {
            QStringList firstBad;
            int nodes = 0, hard = 0, softProp = 0, softState = 0;
            QHash<QString, int> enames;
            for (UiNode *pg : m_mgr->model()->pages()) {
                QVector<QPair<QString, UiNode *>> stack;
                for (const auto &c : pg->children) {
                    stack.append(qMakePair(c.first, c.second));
                }
                while (!stack.isEmpty()) {
                    const auto cur = stack.takeLast();
                    UiNode *n = cur.second;
                    ++nodes;
                    const QStringList bad = hardCheck(n, cur.first, n->parent);
                    if (!bad.isEmpty()) {
                        ++hard;
                        if (firstBad.size() < 3) {
                            firstBad << QStringLiteral("%1: %2")
                                        .arg(n->name, bad.join(QStringLiteral("；")));
                        }
                    }
                    const QString en = enameOf(n).toUpper();
                    if (!en.isEmpty() && ++enames[en] == 2) {
                        ++hard;
                        if (firstBad.size() < 3) {
                            firstBad << QStringLiteral("%1: ID号 %2 撞车").arg(n->name, en);
                        }
                    }
                    if (const ControlTemplate *t = lib->byType(n->type)) {
                        UiNode *tmp = ProjectModel::fromJsonObject(t->raw, nullptr);
                        if (tmp) {
                            int ts = 0, ns = 0;
                            if (propNames(tmp) != propNames(n)) {
                                ++softProp;
                            }
                            if (cssShape(tmp, &ts) != cssShape(n, &ns) || ts != ns) {
                                ++softState;
                            }
                            delete tmp;
                        }
                    }
                    for (const auto &c : n->children) {
                        stack.append(qMakePair(c.first, c.second));
                    }
                }
            }
            check(QStringLiteral("%1：全工程 %2 个节点，硬性问题 0 个")
                  .arg(tag).arg(nodes),
                  hard == 0,
                  QStringLiteral("%1 个有问题：%2").arg(hard)
                  .arg(firstBad.join(QStringLiteral(" | "))));
            check(QStringLiteral("%1：与模板的软性偏差（既有工程里也有，只报数）").arg(tag),
                  true,
                  QStringLiteral("属性名单不同 %1 个，css 形状不同 %2 个")
                  .arg(softProp).arg(softState));
        };

        /* ---- 新建出来的节点：硬性 + 严格照模板 ---- */
        auto strictCheck = [&](UiNode *n, const QString &way) {
            const ControlTemplate *t = lib->byType(n->type);
            if (!t) {
                check(QStringLiteral("%1：认得出类型").arg(way), false, n->type);
                return;
            }
            UiNode *tmp = ProjectModel::fromJsonObject(t->raw, nullptr);
            int ts = 0, ns = 0;
            const QStringList wantP = propNames(tmp), gotP = propNames(n);
            const QStringList wantC = cssShape(tmp, &ts), gotC = cssShape(n, &ns);
            delete tmp;
            check(QStringLiteral("%1：属性名单照模板").arg(way), wantP == gotP,
                  QStringLiteral("模板 %1 / 实际 %2")
                  .arg(wantP.join(QLatin1Char(',')), gotP.join(QLatin1Char(','))));
            check(QStringLiteral("%1：css 字段和状态数照模板").arg(way),
                  wantC == gotC && ts == ns,
                  QStringLiteral("模板 %1(%2) / 实际 %3(%4)")
                  .arg(wantC.join(QLatin1Char(','))).arg(ts)
                  .arg(gotC.join(QLatin1Char(','))).arg(ns));
            const QStringList bad = hardCheck(n, EditorOps::childKeyFor(n->parent),
                                              n->parent);
            check(QStringLiteral("%1：硬性检查全过").arg(way), bad.isEmpty(),
                  bad.join(QStringLiteral("；")));
            check(QStringLiteral("%1：名字是中文名_序号").arg(way),
                  !n->name.isEmpty() && n->name.contains(QLatin1Char('_'))
                  && n->name.at(0).unicode() > 0x2E80,
                  n->name);
        };

        /* ===== 1) 打开进来的工程先扫一遍 ===== */
        auditAll(QStringLiteral("现有工程"));

        /* ===== 2) 每种控件，从控件栏建一个 ===== */
        onNodeSelected(layout);
        for (const ControlTemplate &t : lib->controls()) {
            if (t.type == QLatin1String("NewLayer")
                || t.type == QLatin1String("NewLayout")) {
                continue;                    // 这两种有自己的按钮，下面单独走
            }
            onNodeSelected(layout);
            const int was = layout->children.size();
            m_components->createControl(t.cls, t.type, t.caption);
            if (layout->children.size() != was + 1) {
                check(QStringLiteral("控件栏建【%1】").arg(t.caption), false);
                continue;
            }
            strictCheck(layout->children.last().second,
                        QStringLiteral("控件栏建【%1】").arg(t.caption));
        }

        /* ===== 3) 每种控件，拖一个进去 ===== */
        for (const ControlTemplate &t : lib->controls()) {
            if (t.type == QLatin1String("NewLayer")) {
                continue;                    // 图层落在页上，另算
            }
            onNodeSelected(layout);
            const int was = layout->children.size();
            m_components->createDropped(layout, t.cls, t.type, QPoint(1, 1));
            if (layout->children.size() != was + 1) {
                check(QStringLiteral("拖放建【%1】").arg(t.caption), false);
                continue;
            }
            strictCheck(layout->children.last().second,
                        QStringLiteral("拖放建【%1】").arg(t.caption));
        }

        /* ===== 4) 三种容器下各建一个布局 ===== */
        struct BoxCase { QString cls; QString type; QString label; };
        const QVector<BoxCase> boxes = {
            { QStringLiteral("NewList"), QStringLiteral("VerticalList"),
              QStringLiteral("垂直列表") },
            { QStringLiteral("NewList"), QStringLiteral("HorizontalList"),
              QStringLiteral("水平列表") },
            { QStringLiteral("NewGrid"), QStringLiteral("NewGrid"),
              QStringLiteral("表格") },
        };
        for (const BoxCase &b : boxes) {
            onNodeSelected(layout);
            m_components->createDropped(layout, b.cls, b.type, QPoint(0, 0));
            UiNode *box = layout->children.last().second;
            if (!box || box->cls != b.cls) {
                check(QStringLiteral("建得出【%1】").arg(b.label), false);
                continue;
            }
            sc->rebuild();

            /* 4a. 空容器右键「添加行」（模板那条路） */
            if (auto *lf = qobject_cast<NewList *>(sc->formFor(box))) {
                lf->onAddManyLine();
            } else {
                /* 表格没有"加项"的菜单，用「新建布局」把第一项建出来 */
                onNodeSelected(box);
                m_components->onCreateNewLayout();
            }
            if (!box->children.isEmpty()) {
                strictCheck(box->children.last().second,
                            QStringLiteral("【%1】第一项").arg(b.label));
            } else {
                check(QStringLiteral("【%1】建得出第一项").arg(b.label), false);
            }

            /* 4b. 再来一项（列表走克隆那条） */
            sc->rebuild();
            if (auto *lf = qobject_cast<NewList *>(sc->formFor(box))) {
                lf->onAddManyLine();
            } else {
                onNodeSelected(box);
                m_components->onCreateNewLayout();
            }
            if (box->children.size() >= 2) {
                strictCheck(box->children.last().second,
                            QStringLiteral("【%1】第二项").arg(b.label));
            }

            /* 4c. 选中容器点「新建布局」 */
            sc->rebuild();
            onNodeSelected(box);
            const int was = box->children.size();
            m_components->onCreateNewLayout();
            if (box->children.size() == was + 1) {
                strictCheck(box->children.last().second,
                            QStringLiteral("【%1】点新建布局").arg(b.label));
            } else {
                check(QStringLiteral("【%1】点新建布局建得出来").arg(b.label), false);
            }

            /* 4d. 项里再放一个图片 */
            if (!box->children.isEmpty()) {
                UiNode *item = box->children.last().second;
                sc->rebuild();
                onNodeSelected(item);
                const int inItem = item->children.size();
                m_components->createControl(QStringLiteral("NewFrame"),
                                            QStringLiteral("ImageList"),
                                            QStringLiteral("图片"));
                if (item->children.size() == inItem + 1) {
                    strictCheck(item->children.last().second,
                                QStringLiteral("【%1】项里的图片").arg(b.label));
                } else {
                    check(QStringLiteral("【%1】项里放得进图片").arg(b.label), false);
                }
            }
        }

        /* ===== 5) 图层下建布局 / 页上建图层 ===== */
        onNodeSelected(layer);
        {
            const int was = layer->children.size();
            m_components->onCreateNewLayout();
            if (layer->children.size() == was + 1) {
                strictCheck(layer->children.last().second,
                            QStringLiteral("图层下建布局"));
            }
        }

        /* ===== 6) 折腾完之后，全工程再扫一遍 ===== */
        auditAll(QStringLiteral("折腾完"));

        /* ===== 7) 存盘 -> 重开 -> 生成，三样产物都不能有毛病 ===== */
        {
            const QString dir = QDir::temp().filePath(QStringLiteral("uitools_audit"));
            QDir().mkpath(dir);
            const QString jf = QDir(dir).filePath(QStringLiteral("audit.json"));
            QString err;
            const bool saved = m_mgr->model()->save(jf, &err);
            check(QStringLiteral("自检工程存得下来"), saved, err);
            if (saved) {
                ProjectModel re;
                check(QStringLiteral("自检工程重新打开得了"), re.load(jf, &err), err);

                sty::Builder b;
                QString e2;
                const bool ok = b.loadProject(jf, &e2);
                check(QStringLiteral("自检工程生成得了资源"), ok, e2);
                if (ok) {
                    sty::Options opt;
                    const sty::Output o = b.build(opt);
                    check(QStringLiteral("生成没报错"), o.ok, o.error);

                    const QString h = QString::fromUtf8(o.enameH);
                    check(QStringLiteral("ename.h 里没有空宏名的 #define"),
                          !h.contains(QLatin1String("#define  ")) &&
                          !h.contains(QLatin1String("#define\t")),
                          QStringLiteral("头文件 %1 字节").arg(h.size()));
                    /* 每一行 #define 后面都得跟一个像样的宏名 */
                    int badLine = 0;
                    QString badSample;
                    for (const QString &ln : h.split(QLatin1Char('\n'))) {
                        if (!ln.startsWith(QLatin1String("#define "))) {
                            continue;
                        }
                        const QString rest = ln.mid(8).trimmed();
                        if (rest.isEmpty() || rest.startsWith(QLatin1String("0X"))
                            || rest.startsWith(QLatin1String("0x"))) {
                            ++badLine;
                            if (badSample.isEmpty()) {
                                badSample = ln;
                            }
                        }
                    }
                    check(QStringLiteral("ename.h 每条 #define 都有宏名"),
                          badLine == 0,
                          QStringLiteral("%1 条坏的，例如 [%2]").arg(badLine).arg(badSample));

                    for (const char *w : { "取不到父级尺寸", "没有唯一ID号" }) {
                        const QString key = QString::fromUtf8(w);
                        check(QStringLiteral("生成过程没有'%1'的警告").arg(key),
                              o.warnings.filter(key).isEmpty(),
                              o.warnings.filter(key).mid(0, 3)
                              .join(QStringLiteral(" / ")));
                    }
                }
            }
        }
    }

    /* --- 18w. 诊断：属性面板对"列表行"和"图层下的布局"各铺了哪些行 ---
     * 用户实测："列表控件下面的布局不该有位置坐标相关参数，你的有；
     * 而且你还把事件属性去掉了。"
     * 数据层面两者是同一套（id/element_css/action，css 字段也一样，
     * 见 §14.1），所以差异只可能在面板怎么铺。先把两边铺出来的行打出来。 */
    if (list && !list->children.isEmpty()) {
        UiNode *row = list->children.first().second;
        UiNode *plain = nullptr;
        for (const auto &c : layer->children) {
            if (c.second->cls == QLatin1String("NewLayout")) {
                plain = c.second;
                break;
            }
        }
        auto panelOf = [&](UiNode *n) {
            onNodeSelected(n);
            QStringList v;
            v << QStringLiteral("CSS[") + m_prop->rowsForTest().join(QLatin1Char('/'))
                 + QStringLiteral("]");
            v << QStringLiteral("专有[") + m_com->dynRowsForTest().join(QLatin1Char('/'))
                 + QStringLiteral("]");
            return v.join(QLatin1Char(' '));
        };
        check(QStringLiteral("18w 诊断：列表行的面板"), true, panelOf(row));
        if (plain) {
            check(QStringLiteral("18w 诊断：图层下布局的面板"), true, panelOf(plain));
        }
        /* 用户说的两条，先当断言挂上（现在可能是红的，正好定位） */
        onNodeSelected(row);
        const QStringList css = m_prop->rowsForTest();
        const QStringList dyn = m_com->dynRowsForTest();
        check(QStringLiteral("列表行：面板上有「事件属性」"),
              dyn.filter(QStringLiteral("事件")).size() > 0,
              dyn.join(QLatin1Char('/')));
        /* 【断言要找真实的组标题】这一组的标题来自 json 的 caption，
         * 是「坐标」两个字，不是「位置坐标」（那是 Position 的默认标题）。
         * 原来写成找"位置坐标"，永远是空的，等于白过一条。 */
        check(QStringLiteral("列表行：面板上没有「坐标」组（几何由列表决定）"),
              css.filter(QStringLiteral("坐标")).isEmpty(),
              css.join(QLatin1Char('/')));
        onNodeSelected(row);
        check(QStringLiteral("列表行：其余 CSS 组一个都不少"),
              css.contains(QStringLiteral("对齐方式"))
              && css.contains(QStringLiteral("背景颜色"))
              && css.contains(QStringLiteral("背景图片")),
              css.join(QLatin1Char('/')));
        if (plain) {
            onNodeSelected(plain);
            const QStringList pcss = m_prop->rowsForTest();
            const QStringList pdyn = m_com->dynRowsForTest();
            check(QStringLiteral("图层下的布局：坐标和事件属性都在"),
                  !pcss.filter(QStringLiteral("坐标")).isEmpty()
                  && !pdyn.filter(QStringLiteral("事件")).isEmpty(),
                  QStringLiteral("CSS[%1] 专有[%2]")
                  .arg(pcss.join(QLatin1Char('/')), pdyn.join(QLatin1Char('/'))));
        }
    }

    /* --- 18x. 属性面板全量自检 ---
     * 上一轮的"全量自检"(18v) 只比到**数据结构**：属性名单、css 字段、状态数。
     * 用户一比就找出两条我没查到的：列表行多显示了「坐标」、
     * 自愈补出来的节点少了「事件属性」。教训是**面板铺出来的行也得比**。
     *
     * 面板本质上就是 json 的一个渲染：
     *   CSS 属性页  = element_css.struct[state] 里每个字段一组，标题取 caption
     *   专有属性区  = 除 id / element_css 之外的每条 property 一行，标签取 caption
     * 所以"应该有哪些行"是**可以从 json 算出来**的。凡是算出来有、面板上没有
     * （漏行），或者面板上有、算出来没有（多行），都是一处需要单独说明的偏离。
     *
     * 这一条把每种控件在每种父级下都铺一遍，把两张表逐条比：
     *   · 漏行 —— 一律判失败（用户就是这么丢掉事件属性的）
     *   · 多行 —— 列进白名单才算过，白名单里每条都得写清出处
     * 同时把完整的面板表打印出来，方便一次性核对。 */
    {
        /* 【额外多铺的行，白名单】每条都要写清理由 */
        auto extraAllowed = [](UiNode *n, const QString &row) {
            /* 本版给文字控件加的"预览文字"：只存在工具配置里，不进工程数据。
             * 加这一行是为了画布上能看出排版效果。 */
            if (row == QStringLiteral("预览文字")) {
                return true;
            }
            Q_UNUSED(n)
            return false;
        };
        /* 【有意不铺的行，白名单】 */
        auto missAllowed = [](UiNode *n, const QString &row) {
            /* 列表/表格里的那一项：几何由容器的 sizehw/space 决定，
             * 所以不铺这一组（见 §14.13）。 */
            if (row == QStringLiteral("坐标") && n->parent
                && (n->parent->cls == QLatin1String("NewList")
                    || n->parent->cls == QLatin1String("NewGrid"))) {
                return true;
            }
            return false;
        };

        /* 从 json 算出"应该有哪些行" */
        auto wantCss = [](UiNode *n) {
            QStringList v;
            for (const QJsonValue &pv : n->cssState(0)) {
                const QJsonObject po = pv.toObject();
                const QString cap = po.value(QStringLiteral("caption")).toString();
                v << (cap.isEmpty() ? po.value(QStringLiteral("-name")).toString() : cap);
            }
            return v;
        };
        auto wantDyn = [](UiNode *n) {
            QStringList v;
            for (const UiProperty &p : n->props) {
                if (p.name == QLatin1String("id")
                    || p.name == QLatin1String("element_css")
                    || p.name == QLatin1String("rect") || p.name.isEmpty()) {
                    continue;
                }
                v << (p.caption.isEmpty() ? p.name : p.caption);
            }
            return v;
        };

        int miss = 0, extra = 0, orderBad = 0;
        QStringList detail;

        auto auditPanel = [&](UiNode *n, const QString &where) {
            onNodeSelected(n);
            const QStringList gotCss = m_prop->rowsForTest();
            const QStringList gotDyn = m_com->dynRowsForTest();
            const QStringList wCss = wantCss(n);
            const QStringList wDyn = wantDyn(n);

            QStringList missing, extras;
            for (const QString &r : wCss) {
                if (!gotCss.contains(r) && !missAllowed(n, r)) {
                    missing << QStringLiteral("CSS:") + r;
                }
            }
            for (const QString &r : wDyn) {
                if (!gotDyn.contains(r) && !missAllowed(n, r)) {
                    missing << QStringLiteral("专有:") + r;
                }
            }
            for (const QString &r : gotCss) {
                if (!wCss.contains(r) && !extraAllowed(n, r)) {
                    extras << QStringLiteral("CSS:") + r;
                }
            }
            for (const QString &r : gotDyn) {
                if (!wDyn.contains(r) && !extraAllowed(n, r)) {
                    extras << QStringLiteral("专有:") + r;
                }
            }
            /* 次序也要对：面板是按 json 的次序铺的，错位了排版就乱 */
            QStringList wSeq, gSeq;
            for (const QString &r : wCss) {
                if (gotCss.contains(r)) {
                    wSeq << r;
                }
            }
            for (const QString &r : gotCss) {
                if (wCss.contains(r)) {
                    gSeq << r;
                }
            }
            const bool seqOk = (wSeq == gSeq);

            miss += missing.size();
            extra += extras.size();
            if (!seqOk) {
                ++orderBad;
            }
            detail << QStringLiteral("%1  CSS[%2] 专有[%3]%4%5%6")
                      .arg(where, gotCss.join(QLatin1Char('/')),
                           gotDyn.join(QLatin1Char('/')),
                           missing.isEmpty() ? QString()
                               : QStringLiteral("  ← 漏 ") + missing.join(QLatin1Char(',')),
                           extras.isEmpty() ? QString()
                               : QStringLiteral("  ← 多 ") + extras.join(QLatin1Char(',')),
                           seqOk ? QString() : QStringLiteral("  ← 次序不对"));
        };

        /* ---- 1) 每种控件放在普通布局里 ---- */
        for (const ControlTemplate &t : m_mgr->library()->controls()) {
            if (t.type == QLatin1String("NewLayer")) {
                continue;
            }
            onNodeSelected(layout);
            const int was = layout->children.size();
            m_components->createControl(t.cls, t.type, t.caption);
            if (layout->children.size() != was + 1) {
                continue;
            }
            sc->rebuild();
            auditPanel(layout->children.last().second,
                       QStringLiteral("布局里的【%1】").arg(t.caption));
        }

        /* ---- 2) 图层 / 布局 / 页本身 ---- */
        auditPanel(layer, QStringLiteral("图层"));
        auditPanel(layout, QStringLiteral("布局"));

        /* ---- 3) 三种容器里的项，以及项里的控件 ---- */
        struct BoxCase { QString cls; QString type; QString label; };
        const QVector<BoxCase> boxes = {
            { QStringLiteral("NewList"), QStringLiteral("VerticalList"),
              QStringLiteral("垂直列表") },
            { QStringLiteral("NewList"), QStringLiteral("HorizontalList"),
              QStringLiteral("水平列表") },
            { QStringLiteral("NewGrid"), QStringLiteral("NewGrid"),
              QStringLiteral("表格") },
        };
        for (const BoxCase &b : boxes) {
            onNodeSelected(layout);
            m_components->createDropped(layout, b.cls, b.type, QPoint(0, 0));
            UiNode *box = layout->children.last().second;
            if (!box || box->cls != b.cls) {
                continue;
            }
            sc->rebuild();
            auditPanel(box, QStringLiteral("【%1】本体").arg(b.label));

            onNodeSelected(box);
            m_components->onCreateNewLayout();
            if (box->children.isEmpty()) {
                continue;
            }
            UiNode *item = box->children.last().second;
            sc->rebuild();
            auditPanel(item, QStringLiteral("【%1】里的项").arg(b.label));

            onNodeSelected(item);
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("ImageList"),
                                        QStringLiteral("图片"));
            if (!item->children.isEmpty()) {
                sc->rebuild();
                auditPanel(item->children.last().second,
                           QStringLiteral("【%1】项里的图片").arg(b.label));
            }
        }

        /* ---- 4) 结论 ---- */
        for (const QString &d : detail) {
            check(QStringLiteral("18x 面板表：%1").arg(d.section(QLatin1Char(' '), 0, 0)),
                  true, d.section(QLatin1Char(' '), 1));
        }
        check(QStringLiteral("面板没有漏行（json 里有的属性，面板上必须铺出来）"),
              miss == 0, QStringLiteral("漏 %1 行").arg(miss));
        check(QStringLiteral("面板没有白名单外的多余行"),
              extra == 0, QStringLiteral("多 %1 行").arg(extra));
        check(QStringLiteral("CSS 组的次序和 json 一致"),
              orderBad == 0, QStringLiteral("%1 个控件次序不对").arg(orderBad));

        /* ---- 5) 分页归属：每一行到底落在哪一页 ----
         * 上面比的是"两页合起来有没有漏/多"，这里再钉死**分在哪一页**，
         * 免得哪天改动把背景图片挪回基础页了都没人发现。 */
        {
            onNodeSelected(layout);
            m_components->createControl(QStringLiteral("NewFrame"),
                                        QStringLiteral("Text"),
                                        QStringLiteral("文字"));
            UiNode *tx = layout->children.last().second;
            sc->rebuild();
            onNodeSelected(tx);
            const QStringList bCss = m_prop->rowsForTest(SecBasic);
            const QStringList rCss = m_prop->rowsForTest(SecResource);
            const QStringList bDyn = m_com->dynRowsForTest(SecBasic);
            const QStringList rDyn = m_com->dynRowsForTest(SecResource);
            check(QStringLiteral("18x 分页：文字控件的「基础设置」页"), true,
                  QStringLiteral("CSS[%1] 专有[%2]  共 %3 行")
                  .arg(bCss.join(QLatin1Char('/')), bDyn.join(QLatin1Char('/')))
                  .arg(bCss.size() + bDyn.size()));
            check(QStringLiteral("18x 分页：文字控件的「资源」页"), true,
                  QStringLiteral("CSS[%1] 专有[%2]  共 %3 行")
                  .arg(rCss.join(QLatin1Char('/')), rDyn.join(QLatin1Char('/')))
                  .arg(rCss.size() + rDyn.size()));

            check(QStringLiteral("坐标在基础页、背景和边框在资源页"),
                  bCss.contains(QStringLiteral("坐标"))
                  && rCss.contains(QStringLiteral("背景颜色"))
                  && rCss.contains(QStringLiteral("背景图片"))
                  && rCss.contains(QStringLiteral("边框"))
                  && !bCss.contains(QStringLiteral("边框")),
                  QStringLiteral("基础[%1] 资源[%2]")
                  .arg(bCss.join(QLatin1Char('/')), rCss.join(QLatin1Char('/'))));
            check(QStringLiteral("文字列表在资源页，编码格式和事件属性在基础页"),
                  rDyn.contains(QStringLiteral("文字列表"))
                  && bDyn.contains(QStringLiteral("编码格式"))
                  && bDyn.contains(QStringLiteral("事件属性")),
                  QStringLiteral("基础[%1] 资源[%2]")
                  .arg(bDyn.join(QLatin1Char('/')), rDyn.join(QLatin1Char('/'))));

            /* 换一个控件必须回到第一页（用户明确要的） */
            m_prop->setSectionIndex(SecResource);
            onNodeSelected(layer);
            check(QStringLiteral("换控件回到第一页「基础设置」"),
                  m_prop->sectionIndex() == SecBasic,
                  QStringLiteral("现在停在第 %1 页").arg(m_prop->sectionIndex()));
        }
    }

    /* --- 18y. 「新建 CSS 属性」那套菜单必须够得着 ---
     * 拆页签时把 CSS 状态从页签换成了下拉框，还顺手写了
     * `setEnabled(状态数 > 1)` —— 只有一个状态时下拉框是灰的，
     * 而**禁用的控件收不到右键事件**，于是"复制添加/复制插入/清除/删除活动项"
     * 整套点不出来。最常见的场景恰恰就是它：新控件只有一个状态，想加第二个。
     *
     * 原来的测试只调 m_prop->onCopyAppendState()（直接调槽），绕过了界面，
     * 所以界面坏了它照样绿。这一条盯的是"够不够得着"。 */
    {
        onNodeSelected(frame);
        const int st0 = frame->cssStateCount();
        check(QStringLiteral("18y 前提：这个控件只有一个 CSS 状态"),
              st0 == 1, QStringLiteral("%1 个").arg(st0));
        check(QStringLiteral("只有一个状态时，状态那一行也能右键（菜单够得着）"),
              m_prop->stateMenuReachableForTest());
        check(QStringLiteral("下拉框里条目数 == CSS 状态数"),
              m_prop->stateCountForTest() == qMax(1, st0),
              QStringLiteral("下拉 %1 条 / 状态 %2 个")
              .arg(m_prop->stateCountForTest()).arg(st0));

        /* 走一遍"复制添加"，状态数和下拉条目都要跟着涨 */
        m_prop->onCopyAppendState();
        check(QStringLiteral("复制添加之后多了一个 CSS 状态"),
              frame->cssStateCount() == st0 + 1,
              QStringLiteral("%1 -> %2").arg(st0).arg(frame->cssStateCount()));
        check(QStringLiteral("下拉框跟着多一条"),
              m_prop->stateCountForTest() == st0 + 1,
              QStringLiteral("%1 条").arg(m_prop->stateCountForTest()));
        check(QStringLiteral("多状态时菜单照样够得着"),
              m_prop->stateMenuReachableForTest());

        /* 删回去，别把工程改脏了留给后面的用例 */
        m_prop->onRemoveState();
        check(QStringLiteral("删除活动项能删回去"),
              frame->cssStateCount() == st0,
              QStringLiteral("%1 个").arg(frame->cssStateCount()));
    }

    /* --- 18z. 配置跟着工程走：复制一份工程，预览文字还在 ---
     * ui-config 里存的全是**按工程**的东西（页面尺寸、图片目录、多国语言表、
     * 点阵屏预览配色、每个文字控件的「预览文字」）。它原来锚的是**进程当前
     * 目录** —— 靠启动脚本 `cd project` 再起 exe 才碰巧落对地方，
     * 从别处起 exe 就会在那儿凭空拉一个和工程无关的空配置。
     * 现在锚到工程目录，好处就是这一条：**把 UI 工程整个复制走，
     * 这些设置一起过去**。
     *
     * 【为什么不在这儿 openProject 验】那会把模型和画布整个拆了重建，
     * 而本测试全程攥着 page/layer/layout/frame 这些裸指针 —— 立刻全变野的，
     * 实测直接踩坏堆（0xC0000374）。改成只切配置目录，不动模型：
     * 结论一样硬，因为"复制工程能带走"靠的就是**这份文件在工程目录里**。 */
    {
        /* 【断言用启动时记下的那个路径】测试全程跑在沙箱里（见本函数开头），
         * 这里要验的是"工具真跑起来时配置落在工程目录下"。 */
        check(QStringLiteral("配置文件落在工程目录下，不是进程当前目录"),
              kRealCfgPath.startsWith(kProjCfgDir), kRealCfgPath);
        const QString projDir = kCfgSandbox;   // 下面拿沙箱当"这个工程的目录"

        /* 找一个带 ID 号的文字控件 */
        UiNode *txt = nullptr;
        for (UiNode *pg : m_mgr->model()->pages()) {
            pg->forEach([&](UiNode *x) {
                if (!txt && x->type == QLatin1String("Text")) {
                    for (const UiProperty &p : x->props) {
                        if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
                            txt = x;
                        }
                    }
                }
                return txt == nullptr;
            });
            if (txt) {
                break;
            }
        }
        if (!txt) {
            check(QStringLiteral("18z 前提：工程里有带 ID 号的文字控件"), false);
        } else {
            const QString probe = QStringLiteral("预览文字跟着工程走");
            const QString had = Preview::presetText(txt);
            Preview::setPresetText(txt, probe);
            check(QStringLiteral("预览文字写得进去"),
                  Preview::presetText(txt) == probe, Preview::presetText(txt));

            /* 切走再切回来：既强制把上一份落盘，又证明是**从文件里**读回来的 */
            const QString tmpDir = QDir::temp().filePath(QStringLiteral("uitools_cfgA"));
            QDir(tmpDir).removeRecursively();
            QDir().mkpath(tmpDir);
            GlobalSettings::setProjectDir(tmpDir);
            check(QStringLiteral("换工程目录之后，配置文件跟着换"),
                  GlobalSettings::filePath().startsWith(QDir(tmpDir).absolutePath()),
                  GlobalSettings::filePath());
            check(QStringLiteral("换到空目录，读不到上一个工程的预览文字（互不干扰）"),
                  Preview::presetText(txt).isEmpty(), Preview::presetText(txt));

            GlobalSettings::setProjectDir(projDir);
            check(QStringLiteral("切回来，预览文字从工程目录的文件里读得回来"),
                  Preview::presetText(txt) == probe, Preview::presetText(txt));

            /* ★ 复制工程 = 复制这个目录。把 ui-config 拷过去，指过去，还得在 */
            const QString cp = QDir::temp().filePath(QStringLiteral("uitools_cfgB"));
            QDir(cp).removeRecursively();
            QDir().mkpath(cp + QStringLiteral("/Application Data"));
            const bool copied =
                QFile::copy(GlobalSettings::filePath(),
                            QDir(cp).filePath(QStringLiteral("Application Data/ui-config")));
            check(QStringLiteral("18z 前提：配置文件复制得过去"), copied,
                  GlobalSettings::filePath());
            if (copied) {
                GlobalSettings::setProjectDir(cp);
                check(QStringLiteral("★ 复制一份工程，预览文字跟着过来了"),
                      Preview::presetText(txt) == probe,
                      Preview::presetText(txt));
            }

            /* 收拾干净：切回原工程，预览文字恢复原样 */
            GlobalSettings::setProjectDir(projDir);
            Preview::setPresetText(txt, had);
            check(QStringLiteral("测试完预览文字恢复原样"),
                  Preview::presetText(txt) == had, Preview::presetText(txt));
            QDir(tmpDir).removeRecursively();
            QDir(cp).removeRecursively();
        }
    }

    /* --- 19. 这一通改完，工程还得能存能读 --- */
    const QString tmp = QDir::temp().filePath(QStringLiteral("uitools_opstest.json"));
    QString err;
    const bool saved = m_mgr->model()->save(tmp, &err);
    check(QStringLiteral("改完还能保存"), saved, err);
    if (saved) {
        ProjectModel re;
        const bool reread = re.load(tmp, &err);
        check(QStringLiteral("存出来的还能读回去"),
              reread && !re.pages().isEmpty(), err);
        QFile::remove(tmp);
    }

    /* 配置指回工程目录，沙箱删掉 —— 别给下一次运行留脏东西 */
    GlobalSettings::setProjectDir(kProjCfgDir);
    QDir(kCfgSandbox).removeRecursively();

    EditorOps::setSilent(false);
    log << QStringLiteral("--- 通过 %1 项，失败 %2 项 ---").arg(log.size() - fail).arg(fail);
    *report = log.join(QLatin1Char('\n'));
    return fail;
}

/** 工具栏上那个「画面 2/5 布局_11」。 */
void MainWindow::refreshScreenLabel()
{
    if (!m_screenBox || !m_screenCap) {
        return;
    }
    /* 重填下拉的过程中 activated 不会发（那是用户操作才发的），但 clear()
     * 会带出 currentIndexChanged；这里统一屏蔽，免得刷新界面反过来改选中。 */
    QSignalBlocker block(m_screenBox);
    m_screenBox->clear();

    ScenesScreen *s = m_mgr->currentScreen();
    const QVector<UiNode *> all = s ? s->screens() : QVector<UiNode *>();
    const int i = s ? s->currentScreenIndex() : -1;
    if (all.isEmpty()) {
        m_screenCap->setText(tr("当前画面"));
        m_screenBox->setEnabled(false);
        return;
    }
    m_screenBox->setEnabled(true);
    for (UiNode *n : all) {
        m_screenBox->addItem(m_mgr->model()->displayName(n));
    }
    m_screenBox->setCurrentIndex(qMax(0, i));
    /* 「当前画面 2/5」—— 原来那行文字里的序号信息不能丢，挪到标题上 */
    m_screenCap->setText(tr("当前画面 %1/%2").arg(qMax(0, i) + 1).arg(all.size()));

    /* 宽度：标题和下拉对齐成一小块。取两者所需的较大值，布局名长了也看得全。 */
    const QFontMetrics fm = m_screenBox->fontMetrics();
    int w = m_screenCap->fontMetrics()
                .size(Qt::TextSingleLine, m_screenCap->text()).width();
    for (int k = 0; k < m_screenBox->count(); ++k) {
        w = qMax(w, fm.size(Qt::TextSingleLine, m_screenBox->itemText(k)).width()
                    + kComboArrowWidth);
    }
    m_screenBox->setFixedWidth(qBound(60, w, 140));
}

void MainWindow::onNodeSelected(UiNode *node)
{
    /* 控件列表要知道当前选中的是图层还是布局，否则"新建控件/新建布局"
     * 那两条限制无从判起。 */
    m_components->setCurrentNode(node);
    m_com->showNode(node);
    m_prop->showNode(node);
    m_tree->selectNode(node);
    refreshScreenLabel();
    /* 右列的「当前」标记跟着走。集合没变时 reloadLayouts() 只挪标记，
     * 不重建控件，所以每次选中都调也不肉。 */
    if (m_pages) {
        m_pages->reloadLayouts();
    }
    if (ScenesScreen *s = m_mgr->currentScreen()) {
        s->selectNode(node);
    }
}

void MainWindow::onStatusMessage(const QString &msg)
{
    if (!m_status) {
        return;
    }
    /* 标签限了宽（见 buildToolBar()），长句子自己加省略号，完整内容挂 tooltip。
     * QLabel 不会自动省略，不处理的话就是硬生生切掉半个字。 */
    const int budget = kStatusMaxWidth - m_status->contentsMargins().left()
                       - m_status->contentsMargins().right();
    m_status->setText(m_status->fontMetrics().elidedText(msg, Qt::ElideRight, budget));
    m_status->setToolTip(msg);
}

/**
 * "修改背景" —— 从 <UITools>/backgrounds/ 里挑一张铺到画布底下。
 *
 * 对话框里给一段说明（HTML）：
 *   背景图片目录名是 'backgrounds'，把背景图片放在该目录下就可以显示了，
 *   只支持 JPG 格式。
 * 双击列表里的一项就应用 —— 那正是 onDobuleClickedImage() 这个槽的用途。
 */
void MainWindow::onChangeBackgroud()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("修改背景"));
    dlg.resize(420, 460);

    auto *tip = new QLabel(QStringLiteral(
        "<b><p>背景图片目录名是 'backgrounds'　</p>"
        "<p>把背景图片放在该目录下就可以显示了,只支持JPG格式</p></b>"), &dlg);
    tip->setWordWrap(true);

    auto *list = new QListWidget(&dlg);
    list->setIconSize(QSize(64, 48));
    const QDir bg(QDir(m_mgr->toolsRoot()).filePath(QStringLiteral("backgrounds")));
    for (const QString &fn : bg.entryList(QStringList{ QStringLiteral("*.jpg"),
                                                       QStringLiteral("*.JPG") },
                                          QDir::Files, QDir::Name)) {
        auto *it = new QListWidgetItem(QIcon(bg.filePath(fn)), fn, list);
        it->setData(Qt::UserRole, bg.filePath(fn));
    }

    auto *color = new QPushButton(QStringLiteral("修改背景色"), &dlg);
    auto *close = new QPushButton(QStringLiteral("关闭"), &dlg);
    auto *row = new QHBoxLayout;
    row->addWidget(color);
    row->addStretch();
    row->addWidget(close);

    auto *root = new QVBoxLayout(&dlg);
    root->addWidget(tip);
    root->addWidget(list, 1);
    root->addLayout(row);

    connect(list, &QListWidget::itemDoubleClicked, this, &MainWindow::onDobuleClickedImage);
    connect(color, &QPushButton::clicked, this, [this]() {
        if (ScenesScreen *s = m_mgr->currentScreen()) {
            s->onChangedBackgroundColor();
        }
    });
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void MainWindow::onDobuleClickedImage(QListWidgetItem *a0)
{
    if (!a0) {
        return;
    }
    const QString path = a0->data(Qt::UserRole).toString();
    if (ScenesScreen *s = m_mgr->currentScreen()) {
        s->setBackgroundImage(path);
    }
    onStatusMessage(a0->text());
}

/**
 * 导出前的公共部分：拿到工程 json 的路径，必要时先存盘。
 *
 * 【为什么一定要先存】下游读的是**磁盘上的 json**，不是编辑器内存里的模型。
 * 不先存，导出的就是上一次保存时的样子。
 *
 * @return 工程 json 的绝对路径；空表示用户取消或工程还没保存过。
 */
QString MainWindow::prepareExport()
{
    /* 改完参数直接点"资源导出"也要算数 —— 见 EditorOps::commitPendingEdit() */
    EditorOps::commitPendingEdit();
    const QString json = m_mgr->model()->filePath();
    if (json.isEmpty()) {
        QMessageBox::warning(this, tr("资源导出"),
                             tr("工程还没保存过，先「保存工程」再导出."));
        return QString();
    }
    if (!m_mgr->model()->dirty()) {
        return json;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("资源导出"));
    box.setText(tr("工程有改动还没保存，导出读的是磁盘上的文件.\n先保存吗?"));
    QAbstractButton *save = box.addButton(tr("保存并导出"), QMessageBox::AcceptRole);
    QAbstractButton *skip = box.addButton(tr("直接导出"), QMessageBox::DestructiveRole);
    box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) {
        m_mgr->onSaveProject();
        return m_mgr->model()->dirty() ? QString() : json;   // 存盘被取消就别导
    }
    return (box.clickedButton() == skip) ? json : QString();
}

/**
 * 工具栏「资源导出」—— 点了**直接跑**，不弹界面。
 *
 * 跑的是 QtToolBin.exe 里的同一个 ToolBinWindow（CMake 里两个目标共用这几个
 * 源文件），只是这里把它造出来不显示、直接调 runHeadless()：读
 * config\ini\project.ini、写 project.bin / ename.h / Resbuilder.xml /
 * debug.txt、调 ResBuilder.exe、再跑收尾脚本 —— 和 step2 那条路一字不差。
 *
 * 结果写在工具栏末尾那句状态文字上（成功就报「编译成功」）；
 * 只有失败才弹框，并把完整输出附在详情里。
 */
void MainWindow::onExportResource()
{
    const QString json = prepareExport();
    if (json.isEmpty()) {
        return;
    }

    onStatusMessage(tr("正在导出资源…"));
    /* 这一趟是同步的（要等 ResBuilder 和收尾脚本），先把状态文字刷出去，
     * 不然用户点完看到的还是上一句。 */
    QApplication::processEvents();

    /* 有父窗口、never show() —— 界面不会出现，析构时自己从父窗口摘掉 */
    ToolBinWindow page(QFileInfo(json).absolutePath(), this);
    QString log;
    const bool ok = page.runHeadless(&log);

    if (ok) {
        onStatusMessage(tr("资源导出完成"));
        /* 完整输出挂 tooltip 上，想看细节鼠标停一下就行，不打断操作 */
        if (m_status) {
            m_status->setToolTip(log);
        }
        return;
    }

    onStatusMessage(tr("资源导出失败"));
    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(tr("资源导出"));
    box.setText(tr("资源导出失败，点「显示详细信息」看完整输出."));
    box.setDetailedText(log);
    box.exec();
}

bool MainWindow::exportResourceForTest(QString *report)
{
    const QString json = m_mgr->model()->filePath();
    if (json.isEmpty()) {
        if (report) {
            *report = QStringLiteral("工程没有文件路径，先 openProject()");
        }
        return false;
    }
    ToolBinWindow page(QFileInfo(json).absolutePath(), this);
    page.setRunScriptEnabled(false);      // 别往固件工程里拷东西
    return page.runHeadless(report);
}

/**
 * 右键菜单「资源导出设置…」—— 弹出 UIToolBin 那一页。
 *
 * 工程ID、不重新生成资源、调用脚本、旋转、版本配置、功能设置这些只有这一页
 * 能改；改完在这一页上点「生成资源文件」，和工具栏那个按钮跑的是同一条链。
 */
void MainWindow::onExportResourceDialog()
{
    const QString json = prepareExport();
    if (json.isEmpty()) {
        return;
    }
    QDialog dlg(this);
    auto *page = new ToolBinWindow(QFileInfo(json).absolutePath(), &dlg);
    dlg.setWindowTitle(page->windowTitle());
    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(page);
    dlg.resize(page->size());
    dlg.exec();
}

void MainWindow::onDumpSty()
{
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择 JL.sty / project.bin"), QString(),
        tr("布局文件 (*.sty *.bin);;所有文件 (*)"));
    if (f.isEmpty()) {
        return;
    }
    StyFile sty;
    QString err;
    if (!sty.load(f, &err)) {
        QMessageBox::warning(this, tr("解析失败"), err);
        return;
    }
    QString rt;
    StyFile::verifyRoundTrip(f, &rt);

    auto *dlg = new QPlainTextEdit;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("%1 —— 结构").arg(f));
    dlg->setReadOnly(true);
    dlg->setLineWrapMode(QPlainTextEdit::NoWrap);
    dlg->setPlainText(rt + QLatin1String("\n\n") + sty.describe());
    dlg->resize(900, 700);
    dlg->show();
}
