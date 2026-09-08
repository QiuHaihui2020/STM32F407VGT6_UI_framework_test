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
#include "Preview.h"
#include "findDlg.h"

#include <QAction>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
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
QToolBar { background: #F6F6F6; border-bottom: 1px solid #D0D0D0; spacing: 2px; }
QToolButton { padding: 3px 6px; border: 1px solid transparent; }
QToolButton:hover { border: 1px solid #A0C0E0; background: #EAF2FB; }
)";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(tr("UI编辑工具（重建版）"));
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
    connect(m_prop, &PropertyTab::nodeEdited, this, [this](UiNode *) {
        if (ScenesScreen *s = m_mgr->currentScreen()) {
            s->rebuild();
        }
        m_tree->reload();
        m_pages->reload();
    });
    connect(m_com, &BaseProperty::nodeEdited, this, [this](UiNode *) { m_tree->reload(); });
}

void MainWindow::buildToolBar()
{
    auto icon = [](const char *n) {
        return QIcon(QStringLiteral(":/icon/icons/%1").arg(QLatin1String(n)));
    };

    QToolBar *tb = addToolBar(tr("主工具栏"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setIconSize(QSize(16, 16));
    tb->setFloatable(false);
    tb->setMovable(true);          // 原厂左端有那个可拖的点阵手柄
    tb->setFixedHeight(31);        // 实测 y=31..61

    /* 图标从 exe 的 qrc 里扔出来的 77 个中挑最接近原厂那一排的 */
    QAction *aNew    = tb->addAction(icon("category_vcs.png"),     tr("新建工程(&P)"));
    QAction *aOpen   = tb->addAction(icon("document-open.png"),    tr("打开工程(&O)"));
    QAction *aSave   = tb->addAction(icon("Save_Icon.png"),        tr("保存工程(&S)"));
    QAction *aSaveAs = tb->addAction(icon("document-save-as.png"), tr("另存为(&A)"));
    tb->addSeparator();
    QAction *aNewPage = tb->addAction(icon("canvas-diagram.png"),      tr("新建页面(&N)"));
    QAction *aDelPage = tb->addAction(icon("removesubmitfield.png"),   tr("删除当前页(&D)"));
    tb->addSeparator();
    QAction *aShot   = tb->addAction(icon("Screenshot.png"),          tr("截屏(&P)"));
    aShot->setToolTip(QStringLiteral("截取程序的界面,并保存成PNG图片"));
    tb->addSeparator();
    QAction *aGlobal = tb->addAction(icon("preferences-system.png"),  tr("全局设置"));
    aGlobal->setToolTip(QStringLiteral("软件的全局设置,需要重启软件后生效."));
    QAction *aZoom   = tb->addAction(icon("interface.png"),           tr("工程缩放"));
    aZoom->setToolTip(QStringLiteral(
        "对当前工程的页面尺寸进行缩放,宽高最好要按比例缩放,不然会出现截断与坐标清零."));
    tb->addSeparator();
    QAction *aAbout  = tb->addAction(icon("mode_help@2x.png"),        tr("关于(&I)"));

    /* ---- 以下是原厂没有的，全部追加在原厂那一排之后，不打乱原有形状 ----
     * 点阵屏工程 128x64 在 927px 宽的画布上就是左上角一个指甲盖，没有缩放
     * 基本没法编；一个图层下又常挂着好几个全屏尺寸的互斥布局，不做隔离就是
     * 一团糊。这两个开关是干这个用的。 */
    tb->addSeparator();
    tb->addWidget(new QLabel(tr(" 缩放 "), tb));
    m_zoomBox = new QComboBox(tb);
    m_zoomBox->setEditable(false);
    for (int z : { 25, 50, 75, 100, 150, 200, 300, 400, 600, 800 }) {
        m_zoomBox->addItem(QStringLiteral("%1%").arg(z), z);
    }
    m_zoomBox->addItem(tr("适应窗口"), 0);
    m_zoomBox->setCurrentIndex(m_zoomBox->findData(100));
    m_zoomBox->setToolTip(tr("画布显示倍率（Ctrl+滚轮也行）。只影响显示，坐标始终按 1:1 存盘"));
    tb->addWidget(m_zoomBox);
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

    auto *aHidden = tb->addAction(tr("显示隐藏项"));
    aHidden->setCheckable(true);
    aHidden->setToolTip(tr("连属性里标了「默认隐藏」的控件也画出来"));
    connect(aHidden, &QAction::toggled, m_mgr, &CanvasManager::setShowHidden);

    auto *aSolo = tb->addAction(tr("单独预览"));
    aSolo->setCheckable(true);
    aSolo->setChecked(true);
    aSolo->setToolTip(tr("只画选中项所属的那一个顶层布局。这套工程一页里有 1~9 个"
                         "整屏布局互相盖死，不这么干什么都看不清"));
    connect(aSolo, &QAction::toggled, m_mgr, &CanvasManager::setSolo);

    /* 画面翻页：单独预览一次只看一个，得能快速翻，不然找"音量界面在哪个布局"
     * 要去树里一个个猜。 */
    tb->addSeparator();
    auto *aPrev = tb->addAction(QStringLiteral("◀"));
    aPrev->setToolTip(tr("上一个画面"));
    m_screenLabel = new QLabel(tb);
    m_screenLabel->setMinimumWidth(150);
    m_screenLabel->setContentsMargins(6, 0, 6, 0);
    tb->addWidget(m_screenLabel);
    auto *aNext = tb->addAction(QStringLiteral("▶"));
    aNext->setToolTip(tr("下一个画面"));
    connect(aPrev, &QAction::triggered, this, [this]() { m_mgr->stepScreen(-1); });
    connect(aNext, &QAction::triggered, this, [this]() { m_mgr->stepScreen(+1); });
    connect(m_mgr, &CanvasManager::screenListChanged, this, &MainWindow::refreshScreenLabel);

    /* 原厂状态文字就挂在工具栏末尾，没有独立状态栏 */
    m_status = new QLabel(tr("初始化编辑环境完成"), tb);
    m_status->setContentsMargins(12, 0, 6, 0);
    tb->addWidget(m_status);

    aNew->setShortcut(QKeySequence::New);
    aOpen->setShortcut(QKeySequence::Open);
    aSave->setShortcut(QKeySequence::Save);

    connect(aNew,     &QAction::triggered, m_mgr, &CanvasManager::onCreateNewProject);
    connect(aOpen,    &QAction::triggered, m_mgr, &CanvasManager::onOpenProject);
    connect(aSave,    &QAction::triggered, m_mgr, &CanvasManager::onSaveProject);
    connect(aSaveAs,  &QAction::triggered, m_mgr, &CanvasManager::onSaveAsProject);
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
    /* 原厂标题：UI编辑工具(Build:...) <工程名> */
    setWindowTitle(tr("UI编辑工具（重建版） %1%2")
                   .arg(m_mgr->model()->name(),
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
        const auto spins = m_com->findChildren<QAbstractSpinBox *>()
                           + m_prop->findChildren<QAbstractSpinBox *>();
        int rolled = 0, total = 0;
        for (QAbstractSpinBox *sb : spins) {
            auto *sp = qobject_cast<QSpinBox *>(sb);
            if (!sp || !sp->isVisible()) {
                continue;
            }
            ++total;
            const int before = sp->value();
            QWheelEvent we(QPointF(5, 5), sp->mapToGlobal(QPoint(5, 5)),
                           QPoint(0, 120), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(sp, &we);
            if (sp->value() != before) {
                ++rolled;
                sp->setValue(before);
            }
        }
        check(QStringLiteral("属性栏里滚轮扫过不会改数值"),
              total > 0 && rolled == 0,
              QStringLiteral("%1 个数值框，被滚动改掉 %2 个").arg(total).arg(rolled));
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
    if (!m_screenLabel) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (!s) {
        m_screenLabel->clear();
        return;
    }
    const QVector<UiNode *> all = s->screens();
    const int i = s->currentScreenIndex();
    if (all.isEmpty() || i < 0) {
        m_screenLabel->setText(tr("画面 -"));
        return;
    }
    m_screenLabel->setText(tr("画面 %1/%2  %3")
                           .arg(i + 1).arg(all.size())
                           .arg(m_mgr->model()->displayName(all.at(i))));
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
    if (m_status) {
        m_status->setText(msg);
    }
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
