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
#include "StyFile.h"
#include "EditorOps.h"
#include "BuildDate.h"
#include "Preview.h"
#include "findDlg.h"
#include "I18nLanguage.h"
#include "ImageFileDialog.h"
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

/* 配色直接从原厂截图采样：
 *   面板绿 #C0DCC0 / 属性区 #CEE2CE / 列表内白底 #F1F1F1 / 画布灰 #F0F0F0 */
static const char *const kFactoryQss = R"(
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
    /* 尺寸按原厂截图实测：客户区 1687x969（工具栏 31 + 内容 969），
     * 四列 263 / 232 / 927 / 255。 */
    resize(1694, 1032);

    m_mgr = new CanvasManager(this);

    /* 中央：可滚动的画布宿主。原厂画布贴左上角，不居中。 */
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
    applyFactoryStyle();

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

void MainWindow::applyFactoryStyle()
{
    setStyleSheet(QLatin1String(kFactoryQss));
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
     * 这个上下次序就是原厂的次序。 */
    auto *propArea = new BaseScrollArea(side);
    auto *propHost = new QWidget;
    auto *propLay = new QVBoxLayout(propHost);
    propLay->setContentsMargins(2, 2, 2, 2);
    propLay->setSpacing(3);

    m_com = new ComProperty(propHost);
    m_prop = new PropertyTab(propHost);
    propLay->addWidget(m_com, 0);
    propLay->addWidget(m_prop, 0);
    propLay->addWidget(m_com->dynamicSection(), 0);
    propLay->addStretch(1);

    propArea->setWidget(propHost);
    sideLay->addWidget(propArea, 1);

    m_sideDock->setWidget(side);
    addDockWidget(Qt::LeftDockWidgetArea, m_sideDock);
    /* 两列并排，而不是上下堆叠 */
    splitDockWidget(m_tree, m_sideDock, Qt::Horizontal);

    /* --- 右列：页面 --- */
    m_pages = new PageView(this);
    m_pages->setManager(m_mgr);
    m_pages->setMinimumWidth(140);
    addDockWidget(Qt::RightDockWidgetArea, m_pages);

    /* 实测宽度：树 263 / 第二列 232 / 页面栏 255（中间画布拿剩下的 927） */
    resizeDocks({ m_tree, m_sideDock }, { 263, 232 }, Qt::Horizontal);
    resizeDocks({ m_pages }, { 255 }, Qt::Horizontal);

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
    /* 原厂是"图标在上、文字在下"的大按钮，一排排到底（temp/Snipaste_2026-09-09_08-46-05.jpg）。
     * 之前做成了 16px 小图标 + 文字在右，一眼就不是那个东西。 */
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(28, 28));
    tb->setFloatable(false);
    tb->setMovable(true);          // 原厂左端有那个可拖的点阵手柄
    tb->setMinimumHeight(58);      // 28 图标 + 文字 + 上下留白

    /* 图标从 exe 的 qrc 里扔出来的 77 个中挑最接近原厂那一排的 */
    QAction *aNew    = tb->addAction(icon("category_vcs.png"),     tr("新建工程(P)"));
    QAction *aOpen   = tb->addAction(icon("document-open.png"),    tr("打开工程(O)"));
    QAction *aSave   = tb->addAction(icon("Save_Icon.png"),        tr("保存工程(S)"));
    QAction *aSaveAs = tb->addAction(icon("document-save-as.png"), tr("另存为(A)"));
    tb->addSeparator();
    QAction *aNewPage = tb->addAction(icon("canvas-diagram.png"),      tr("新建页面(N)"));
    QAction *aDelPage = tb->addAction(icon("removesubmitfield.png"),   tr("删除页面(D)"));
    tb->addSeparator();
    /* 【原厂没有这个按钮】原厂要退出编辑器、再双击 step2 的脚本弹 UIToolBin
     * 才能出资源。这里把那一页直接搬进来（同一个 ToolBinWindow 类，同一条
     * 生成链），改完布局当场就能导出，省掉来回切窗口。 */
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

    /* ---- 以下是原厂没有的，跟在原厂那一排后面，同一排 --------------------
     * 点阵屏工程 128x64 在 927px 宽的画布上就是左上角一个指甲盖，没有缩放
     * 基本没法编；一个图层下又常挂着好几个全屏尺寸的互斥布局，不做隔离就是
     * 一团糊。这几个开关是干这个用的。
     *
     * 【为什么装在一个子控件里，而不是直接 tb->addAction】第一排换成
     * "图标在上、文字在下"的大按钮之后，工具栏会把每个直属按钮都撑成那个
     * 高度和宽度，11 个原厂按钮 + 这几个就一千七百多像素，末尾那句状态文字
     * 直接被挤出窗口。装进一个自带 QHBoxLayout 的 QWidget 里，它们就不受
     * 工具栏的 ToolButtonStyle 管，按各自的文字宽度排，省下一半宽度，
     * 既保住了原厂那一排的形状，也不用把任何一项挪走或藏起来。 */
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
     * 别写死像素：字体换了（原厂是宋体 9pt，别的机器上未必）宽度要跟着走。 */
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

    /* 原厂状态文字就挂在工具栏末尾（截图右边那句「编译成功」），没有独立状态栏 */
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
    /* 这两个在原二进制里是 private slot，用字符串连接走 moc 元调用 */
    connect(aNewPage, SIGNAL(triggered()), m_mgr, SLOT(onCreateNewScenesScreen()));
    connect(aDelPage, SIGNAL(triggered()), m_mgr, SLOT(onDelCurrentScenesScreen()));

    /* 逆向出来但原厂工具栏上没有的入口，放右键菜单里，不破坏工具栏形状 */
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
    /* ★ 原厂退出要问两次：先"是否真的退出程序?"，再问没保存的改动。
     * 重建版之前是直接关，改了一下午的东西点个叉就没了。 */
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
                /* 原厂原话，前面那个空格也是原厂的 */
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

    /* 多国语言表：工程里记了就用工程的，否则按原厂目录结构去
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

    ScenesScreen *sc = m_mgr->currentScreen();
    if (!sc || !sc->page()) {
        *report = QStringLiteral("没有可测的页面");
        return 1;
    }
    UiNode *page = sc->page();

    UiNode *layer  = firstOfClass(page, "NewLayer");
    UiNode *layout = firstOfClass(page, "NewLayout");
    UiNode *frame  = firstOfClass(page, "NewFrame");
    UiNode *list   = firstOfClass(page, "NewList");
    if (!layer || !layout || !frame) {
        *report = QStringLiteral("工程里缺图层/布局/控件样本，测不了");
        return 1;
    }

    /* --- 1. 建控件必须先选中布局 --- */
    int before = countNodes(page);
    onNodeSelected(frame);                       // 选中的是控件，不是布局
    EditorOps::clearLastMessage();
    m_components->createControl(QStringLiteral("NewFrame"), QStringLiteral("Text"),
                                QStringLiteral("文字"));
    check(QStringLiteral("选中控件时建控件被拦下"),
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

    /* --- 3. 建布局必须先选中图层 --- */
    before = countNodes(page);
    onNodeSelected(layout);                      // 选中的是布局，不是图层
    EditorOps::clearLastMessage();
    m_components->onCreateNewLayout();
    check(QStringLiteral("选中布局时建布局被拦下"),
          countNodes(page) == before
          && EditorOps::lastMessage()
             == QStringLiteral("请选择一个图层或者新建一个图层,并选中它."),
          EditorOps::lastMessage());

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
        m_prop->setCurrentIndex(0);
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
                const QPoint want = lf->mapTo(sc, lf->rect().center())
                                    - sc->formFor(landed)->mapTo(sc, QPoint(0, 0));
                check(QStringLiteral("落点写进了新控件的 rect"),
                      drop->rectOf(0).topLeft() == want,
                      QStringLiteral("rect=(%1,%2) 期望=(%3,%4)")
                          .arg(drop->rectOf(0).x()).arg(drop->rectOf(0).y())
                          .arg(want.x()).arg(want.y()));
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
     * 固件的 struct element_css 里 left/top 是**有符号 int**，原厂
     * SmallColor_oled.json 里就有 (0,-11) 的控件 —— 让内容从父容器上边缘
     * 露出去是他们在用的手法。我一度在拖动里把坐标夹到 [0,父级尺寸]，
     * 那是凭空多出来的限制，会把用户已有的负坐标改掉。这条守着它别再回来。 */
    sc->rebuild();
    if (BaseForm *f = sc->formFor(frame)) {
        const QRect g0 = f->geometry();
        f->setGeometry(QRect(-7, -11, qMax(1, g0.width()), qMax(1, g0.height())));
        f->syncRectToNode();
        check(QStringLiteral("负坐标能写进节点（原厂允许，别钳）"),
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
     * 原厂界面上 VerticalList 那两个节点显示的是"垂直列表_16"/"垂直列表_25"，
     * 可它们的 -name 在 json 里是**裸的类型名** "VerticalList"。也就是说
     * -name 没被正经命名过时，原厂是拿 caption+序号 现算的。我一开始直接显示
     * -name，树上就冒出个英文名。 */
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
     * 一个图层下常挂着好几个**全屏尺寸**的互斥布局（原厂页0 有 5 个
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
        const QVector<QWidget *> roots{ m_com, m_com->dynamicSection(), m_prop };
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
            for (QComboBox *cb : m_com->dynamicSection()
                                 ->findChildren<QComboBox *>()) {
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
     * 原厂面板每个列表类属性下面都跟着一个条目下拉框（图片"缩略图+文件名"、
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
            for (QComboBox *cb : m_com->dynamicSection()->findChildren<QComboBox *>()) {
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
            for (QComboBox *cb : m_com->dynamicSection()->findChildren<QComboBox *>()) {
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
        for (QComboBox *cb : m_com->dynamicSection()->findChildren<QComboBox *>()) {
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
             * 配置里：写进工程就不是原厂那份 json 了，而且**不能**把工程标记
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
            for (QComboBox *cb : m_com->dynamicSection()->findChildren<QComboBox *>()) {
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
                    for (QLabel *l : m_com->dynamicSection()->findChildren<QLabel *>()) {
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
     * 原厂的「图片编辑」是把每张位图画出来的，「显示列表」是"内容#ResID"
     * （见 temp/图片列表.jpg、temp/文字列表.jpg）。以前这两个都退化成了
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
     * （见 docs/FACTORY_UI.md 8.4），配成绿底蓝字这条断言照样得成立。
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
        /* 标题格式：UI编辑工具(Build:YYYY-MM-DD) <工程名>，
         * 不带"重建版"这类字眼 */
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
            QSpinBox *sp = nullptr;
            for (QSpinBox *s : m_prop->findChildren<QSpinBox *>()) {
                if (s->isVisible()) {
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
     * 那两条原厂限制无从判起。 */
    m_components->setCurrentNode(node);
    m_com->showNode(node);
    m_prop->showNode(node);
    m_tree->selectNode(node);
    refreshScreenLabel();
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
 * 原厂在这儿有一段说明（HTML，逐字照抄）：
 *   背景图片目录名是 'backgrounds'，把背景图片放在该目录下就可以显示了，
 *   只支持 JPG 格式。
 * 双击列表里的一项就应用 —— 那正是 onDobuleClickedImage() 这个槽的用途
 * （Double 拼错也是原厂的，保留）。
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
 * 结果写在工具栏末尾那句状态文字上（原厂那一排也是在这儿报「编译成功」）；
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
