#include "Canvas.h"

#include <QSettings>

#include "ConfigProject.h"
#include "Property.h"
#include "GlobalSettings.h"
#include "ZoomProject.h"
#include "Forms.h"
#include "ProjectDialog.h"
#include "EditorOps.h"

#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QColorDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSet>
#include <QWheelEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QStackedLayout>
#include <QGridLayout>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QFileInfo>

/* ===================== ScenesScreen ===================== */

ScenesScreen::ScenesScreen(QWidget *parent)
    /* 单色屏灭的时候就是黑的，画布底色跟着来 —— 这样画布上看到的
     * 明暗关系就是屏上的明暗关系。 */
    : QFrame(parent), m_bg(QColor(0x10, 0x10, 0x10))
{
    setFrameShape(QFrame::Box);
    setFrameShadow(QFrame::Plain);
    setAutoFillBackground(true);
    setAcceptDrops(true);      // 控件列表里拖过来的东西落在这儿
}

ScenesScreen::~ScenesScreen() = default;

void ScenesScreen::setPage(UiNode *page)
{
    m_page = page;
    rebuild();
}

void ScenesScreen::setZoom(int percent)
{
    const int z = qBound(25, percent, 800);
    if (z == m_zoom) {
        return;
    }
    m_zoom = z;
    /* 【选中要跨过重建活下来】改倍率只是把 QWidget 重摆一遍，**模型一个字节
     * 都没动**。但 rebuild() 会把 m_selected 清空（结构变更时那些裸指针确实
     * 会变野），于是单独预览失去目标，applyVisibility 退回去显示默认那个
     * 布局 —— 表现就是"一缩放就跳回第一个布局"。
     * 这里先记下节点，重建完再选回来。安全性由 m_forms 保证：它是照活模型
     * 重新建的，节点还在里面就说明指针没失效；不在就老老实实不恢复。 */
    UiNode *keep = m_selected;
    rebuild();
    if (keep && m_forms.contains(keep)) {
        selectNode(keep);
    }
}

void ScenesScreen::rebuild()
{
    for (BaseForm *f : m_forms) {
        f->deleteLater();
    }
    m_forms.clear();
    /* 眼睛藏的那些也一起清掉：这些是裸指针，重建之后全是野指针。
     * 反正重建意味着结构变了，重新来过是合理的。 */
    m_userHidden.clear();
    /* m_selected 是裸指针，画布一重建它就可能指向已经释放的节点；
     * 而且不清掉的话，重建后再选中同一个节点会被 selectNode() 当成"没变化"
     * 而吞掉。 */
    m_selected = nullptr;
    if (!m_page) {
        setFixedSize(1, 1);
        return;
    }

    const QSize s = m_page->rect.isValid() ? m_page->rect.size() : QSize(128, 64);
    setFixedSize(s.width() * m_zoom / 100, s.height() * m_zoom / 100);

    for (const auto &c : m_page->children) {
        buildRecursive(c.second, this);
    }
    applyVisibility();
    update();
}

void ScenesScreen::buildRecursive(UiNode *n, QWidget *parentWidget)
{
    BaseForm *f = createFormForClass(n->cls, parentWidget);
    /* 倍率要在 bind() 之前给：bind 里会调 syncRectFromNode() 摆位置 */
    f->setDisplayZoom(m_zoom);
    f->bind(n);
    f->show();
    m_forms.insert(n, f);
    /* 在画布上直接点控件，也要让树/属性面板跟着走（原厂就是这个行为）。
     * BaseForm::mousePressEvent 只调 setSelected(true)，既不通知外面、
     * 也不取消别的控件的选中框，所以这里拦一道press。事件过滤器跑在
     * 控件自己的处理之前，不影响它的拖动逻辑。 */
    f->installEventFilter(this);

    /* 结构类操作（右键删除/粘贴/挪层/列表加行）由控件自己发起，画布只负责
     * 往上转发一次 —— 树、页面栏、属性面板都挂在主窗口那一层。 */
    connect(f, &BaseForm::structureChanged, this, [this]() {
        m_selected = nullptr;          // 被删掉的那个可能就是它，别留悬空指针
        emit structureChanged();
    });
    connect(f, &BaseForm::findRequested, this, &ScenesScreen::findRequested);
    connect(f, &BaseForm::userHideRequested, this, &ScenesScreen::toggleUserHidden);

    connect(f, &FormResizer::formWindowSizeChanged, this,
            [this, n, f](QRect, QRect) {
                /* 换算统一在 BaseForm::syncRectToNode() 里做，这里不再自己算
                 * 一遍 —— 以前两处各算各的，靠"后写的盖前写的"才对，很脆。 */
                f->syncRectToNode();
                emit geometryEdited(n);
            });

    for (const auto &c : n->children) {
        buildRecursive(c.second, f);
    }
}

/**
 * 谁显示、谁淡出。
 *
 * 【这是"全部布局叠在一起"的解药】一个图层下经常挂着好几个**全屏尺寸**的
 * 布局（原厂 SmallColorTFT 页0 就有 5 个 (0,0,128,64) 的），它们是互斥的
 * 界面状态（主界面/音量/EQ/菜单…），运行时只显示一个。全画出来的话最上面
 * 那个把下面全盖死，什么都编不了。
 *
 * 两条规则：
 *   1. 标了"默认隐藏"(element_css.invisible == "true") 的默认不画。
 *      但**选中它或它的子孙时要画出来**，否则树上点得到、画布上摸不着。
 *   2. 选中即隔离：找到选中项所属的那个"顶层布局"（父节点是图层的那一层），
 *      同级的其它顶层布局整棵淡出。淡出不是隐藏，照样能点，点一下就换它清晰。
 */
void ScenesScreen::applyVisibility()
{
    if (m_forms.isEmpty()) {
        return;
    }

    /* 选中项到根的这条链上的节点，一律要可见 */
    QSet<UiNode *> onPath;
    for (UiNode *n = m_selected; n; n = n->parent) {
        onPath.insert(n);
    }

    /* ---- 规则 1：默认隐藏 ---- */
    for (auto it = m_forms.constBegin(); it != m_forms.constEnd(); ++it) {
        UiNode *n = it.key();
        bool visible = true;
        for (UiNode *a = n; a && a->parent; a = a->parent) {
            if (a->isDefaultHidden() && !onPath.contains(a)) {
                visible = false;
                break;
            }
        }
        if (m_showHidden) {
            visible = true;
        }
        /* 眼睛手动藏的优先级最高：它是用户刚刚点的，重算可见性时不能把它
         * 又放出来（否则点一下别的节点，刚藏好的东西就自己冒回来了）。
         * 祖先被藏了，子孙自然也看不见。 */
        for (UiNode *a = n; a && a->parent; a = a->parent) {
            if (m_userHidden.contains(a)) {
                visible = false;
                break;
            }
        }
        it.value()->setVisible(visible);
    }

    /* ---- 规则 2：单独预览 ---- */
    if (!m_solo) {
        return;
    }
    /* 没选中任何东西时，预览那个 invisible=false 的（= 运行时的默认画面）。
     * 规则 1 已经把它挑出来了，这里不用再动。 */
    UiNode *keep = topScreenOf(m_selected);
    if (!keep) {
        return;
    }
    for (const auto &sib : keep->parent->children) {
        if (sib.second == keep) {
            continue;
        }
        /* 整棵子树都不画。注意不能只藏顶层那一个 —— 子控件是独立的 QWidget，
         * 父控件 hide() 之后它们确实跟着不显示，但 applyVisibility 下一轮
         * 会按各自的规则把它们又 setVisible(true) 回去。 */
        QVector<UiNode *> stack{ sib.second };
        while (!stack.isEmpty()) {
            UiNode *x = stack.takeLast();
            if (BaseForm *f = m_forms.value(x, nullptr)) {
                f->setVisible(false);
            }
            for (const auto &c : x->children) {
                stack.append(c.second);
            }
        }
    }
}

UiNode *ScenesScreen::topScreenOf(UiNode *n) const
{
    UiNode *top = n;
    while (top && top->parent && !EditorOps::isLayer(top->parent)) {
        top = top->parent;
    }
    return (top && top->parent) ? top : nullptr;
}

QVector<UiNode *> ScenesScreen::screens() const
{
    QVector<UiNode *> out;
    if (!m_page) {
        return out;
    }
    for (const auto &layer : m_page->children) {
        for (const auto &lo : layer.second->children) {
            out.append(lo.second);
        }
    }
    return out;
}

int ScenesScreen::currentScreenIndex() const
{
    UiNode *cur = topScreenOf(m_selected);
    if (!cur) {
        /* 没选中就算"默认画面"是当前的 */
        const QVector<UiNode *> all = screens();
        for (int i = 0; i < all.size(); ++i) {
            if (!all.at(i)->isDefaultHidden()) {
                return i;
            }
        }
        return all.isEmpty() ? -1 : 0;
    }
    return screens().indexOf(cur);
}

void ScenesScreen::setSolo(bool on)
{
    if (m_solo == on) {
        return;
    }
    m_solo = on;
    applyVisibility();
}

void ScenesScreen::toggleUserHidden(UiNode *n)
{
    if (!n) {
        return;
    }
    if (m_userHidden.contains(n)) {
        m_userHidden.remove(n);
    } else {
        m_userHidden.insert(n);
    }
    applyVisibility();
}

void ScenesScreen::setShowHidden(bool on)
{
    if (m_showHidden == on) {
        return;
    }
    m_showHidden = on;
    applyVisibility();
}

void ScenesScreen::selectNode(UiNode *node)
{
    /* 【必须判重】选中是个环：
     *     selectNode -> nodeSelected -> MainWindow::onNodeSelected
     *         -> 回头又调 selectNode（它要让画布跟随树/属性面板的选中）
     * 不判重的话第二次进来又发一遍信号，无限递归，几百层就把栈撑爆
     * （实测 0xC00000FD STATUS_STACK_OVERFLOW，表现是点一下就卡死然后闪退）。
     * 判重之后第二次进来发现没变化，直接返回，环就断了 —— 这也是任何编辑器
     * 里"选中"该有的语义：重复选同一个东西是空操作。 */
    if (m_selected == node) {
        return;
    }
    m_selected = node;
    for (auto it = m_forms.constBegin(); it != m_forms.constEnd(); ++it) {
        it.value()->setSelected(it.key() == node);
    }
    applyVisibility();
    if (node) {
        emit nodeSelected(node);
    }
}

/* ---- 拖拽建控件 ---------------------------------------------------------
 * 手册里这是**主要手势**（点击那条路是第二条）。DragButton 早就在发起拖拽了，
 * 但之前全工程没有任何一处 setAcceptDrops，所以拖过去什么都不会发生。
 *
 * 落点判定：拖到哪个容器上就挂到哪个容器下 —— 这比"看你选中了什么"更符合
 * 直觉，也是手册的说法（"点击布局并拖动到图层上，即可新建一个布局"）。
 * 容器不合适时 ignore()，鼠标指针会变成禁止符号，用户当场就知道贴不进去。 */

/** 画布坐标 p 落在哪个节点上。取最深的那个（子控件盖在父容器上面）。 */
UiNode *ScenesScreen::nodeAt(const QPoint &p) const
{
    QWidget *w = const_cast<ScenesScreen *>(this)->childAt(p);
    while (w && w != this) {
        for (auto it = m_forms.constBegin(); it != m_forms.constEnd(); ++it) {
            if (it.value() == w) {
                return it.key();
            }
        }
        w = w->parentWidget();
    }
    return m_page;
}

/** 拖过来的东西能不能落在 p 处。能落的话 target 是接收它的父节点。 */
static bool dropTargetFor(UiNode *hit, const QString &cls, UiNode **target)
{
    /* 图层挂在页上，其余挂在布局下 —— 和 EditorOps 的层级规则同一套 */
    if (cls == QLatin1String("NewLayer")) {
        while (hit && hit->parent) {
            hit = hit->parent;          // 一路上溯到页节点
        }
        *target = hit;
        return hit != nullptr;
    }
    if (cls == QLatin1String("NewLayout")) {
        /* 布局要么挂图层下（手册："点击布局并拖动到图层上"），
         * 要么挂布局下（原厂工程里 NewLayout 套 NewLayout 有 1 例）。 */
        for (UiNode *n = hit; n; n = n->parent) {
            if (EditorOps::isLayer(n) || EditorOps::isLayout(n)) {
                *target = n;
                return true;
            }
        }
        return false;
    }
    /* 普通控件只认布局 */
    for (UiNode *n = hit; n; n = n->parent) {
        if (EditorOps::acceptsChild(n)) {
            *target = n;
            return true;
        }
    }
    return false;
}

/** 从 MIME 里取 "<-class>|<-type>"。 */
static bool parsePayload(const QMimeData *md, QString *cls, QString *type)
{
    if (!md || !md->hasFormat(QLatin1String(EditorOps::kControlMime))) {
        return false;
    }
    const QString s = QString::fromUtf8(
        md->data(QLatin1String(EditorOps::kControlMime)));
    const int bar = s.indexOf(QLatin1Char('|'));
    if (bar <= 0) {
        return false;
    }
    *cls = s.left(bar);
    *type = s.mid(bar + 1);
    return !type->isEmpty();
}

void ScenesScreen::wheelEvent(QWheelEvent *e)
{
    if (!(e->modifiers() & Qt::ControlModifier)) {
        /* 普通滚轮留给外层的滚动条，别抢 */
        e->ignore();
        return;
    }
    const int d = e->angleDelta().y();
    if (d != 0) {
        emit zoomStepRequested(d > 0 ? 1 : -1);
    }
    e->accept();
}

void ScenesScreen::dragEnterEvent(QDragEnterEvent *e)
{
    QString cls, type;
    if (parsePayload(e->mimeData(), &cls, &type)) {
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void ScenesScreen::dragMoveEvent(QDragMoveEvent *e)
{
    QString cls, type;
    UiNode *target = nullptr;
    if (parsePayload(e->mimeData(), &cls, &type)
        && dropTargetFor(nodeAt(e->pos()), cls, &target)) {
        e->acceptProposedAction();
    } else {
        /* ignore 之后指针变成禁止符号 —— 松手前就知道这儿贴不进去 */
        e->ignore();
    }
}

void ScenesScreen::dropEvent(QDropEvent *e)
{
    QString cls, type;
    UiNode *target = nullptr;
    if (!parsePayload(e->mimeData(), &cls, &type)
        || !dropTargetFor(nodeAt(e->pos()), cls, &target)) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    /* 落点换算成相对目标容器的坐标：控件的 rect 存的就是相对父级的。
     * 缩放中画布是放大显示的，得先除回 1:1。 */
    QPoint local = e->pos();
    if (BaseForm *tf = formFor(target)) {
        local = tf->mapFrom(this, e->pos());
    }
    if (m_zoom != 100) {
        local = QPoint(local.x() * 100 / m_zoom, local.y() * 100 / m_zoom);
    }
    emit controlDropped(target, cls, type, local);
}

bool ScenesScreen::simulateDropForTest(const QPoint &pos, const QString &cls,
                                       const QString &type, UiNode **landedOn)
{
    if (landedOn) {
        UiNode *t = nullptr;
        *landedOn = dropTargetFor(nodeAt(pos), cls, &t) ? t : nullptr;
    }
    QMimeData md;
    md.setData(QLatin1String(EditorOps::kControlMime),
               (cls + QLatin1Char('|') + type).toUtf8());
    QDropEvent e(pos, Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
    dropEvent(&e);
    return e.isAccepted();
}

void ScenesScreen::setShowGrid(bool on)
{
    if (m_showGrid == on) {
        return;
    }
    m_showGrid = on;
    update();
}

void ScenesScreen::setBackgroundImage(const QString &path)
{
    m_bgImage = path.isEmpty() ? QPixmap() : QPixmap(path);
    if (m_page) {
        m_page->setExtra(QStringLiteral("background_image"), path);
    }
    update();
}

void ScenesScreen::contextMenuEvent(QContextMenuEvent *e)
{
    /* 点在空白处（没落在任何控件上）才是页面的菜单；落在控件上的右键由
     * BaseForm::contextMenuEvent 先接走，根本到不了这里。 */
    QMenu menu(this);
    QAction *aDel   = menu.addAction(QStringLiteral("删除当前页面"));
    menu.addSeparator();
    QAction *aColor = menu.addAction(QStringLiteral("修改背景色"));
    QAction *aImage = menu.addAction(QStringLiteral("修改背景图片"));

    QAction *c = menu.exec(e->globalPos());
    if (c == aDel) {
        /* 删页面是 CanvasManager 的事（它管着页数组），这里只发个请求 */
        emit deletePageRequested();
    } else if (c == aColor) {
        onChangedBackgroundColor();
    } else if (c == aImage) {
        const QString f = QFileDialog::getOpenFileName(
            this, QStringLiteral("修改背景图片"), QString(),
            QStringLiteral("图片 (*.jpg *.png *.bmp)"));
        if (!f.isEmpty()) {
            setBackgroundImage(f);
        }
    }
    e->accept();
}

void ScenesScreen::onChangedBackgroundColor()
{
    const QColor c = QColorDialog::getColor(m_bg, this, tr("画布背景色"));
    if (c.isValid()) {
        m_bg = c;
        update();
    }
}

void ScenesScreen::paintEvent(QPaintEvent *e)
{
    QPainter p(this);
    p.fillRect(rect(), m_bg);
    /* 底图铺在背景色之上、控件之下。拉伸到画布大小并保持比例 —— 它只是
     * 编辑时的参照（比如把产品效果图垫在下面对位），不进工程数据。 */
    if (!m_bgImage.isNull()) {
        p.drawPixmap(rect(), m_bgImage.scaled(size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    }

    /* 像素网格：开关开着、且放大到看得清的倍数才画。
     * 【以前这里只判 m_zoom】工具栏的"网格开关"翻的是 CanvasManager 的
     * m_showGrid，画布压根没看那个标志，所以点了只有状态栏文字会变，
     * 画面纹丝不动。 */
    if (m_showGrid && m_zoom >= 300) {
        p.setPen(QColor(0xff, 0xff, 0xff, 40));
        const int step = m_zoom / 100;
        for (int x = 0; x < width(); x += step) {
            p.drawLine(x, 0, x, height());
        }
        for (int y = 0; y < height(); y += step) {
            p.drawLine(0, y, width(), y);
        }
    }
    QFrame::paintEvent(e);
}

bool ScenesScreen::eventFilter(QObject *watched, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton) {
        for (auto it = m_forms.constBegin(); it != m_forms.constEnd(); ++it) {
            if (it.value() == watched) {
                selectNode(it.key());   // 幂等，重复点同一个不会再发信号
                break;
            }
        }
    }
    return QFrame::eventFilter(watched, e);
}

void ScenesScreen::mousePressEvent(QMouseEvent *e)
{
    /* 点空白处取消选中 */
    if (e->button() == Qt::LeftButton) {
        selectNode(nullptr);
    }
    QFrame::mousePressEvent(e);
}

/* ===================== CanvasManager ===================== */

CanvasManager::CanvasManager(QObject *parent)
    : QObject(parent)
{
}

CanvasManager::~CanvasManager() = default;

void CanvasManager::setMPageSize(const QSize &v)
{
    m_pageSize = v;
}

void CanvasManager::setToolsRoot(const QString &p)
{
    QString err;
    if (!m_lib.load(p, &err)) {
        emit statusMessage(tr("控件库加载失败: %1").arg(err));
    } else {
        emit statusMessage(tr("控件库: %1 个控件").arg(m_lib.controls().size()));
    }
}

void CanvasManager::attachHost(QWidget *host)
{
    m_host = host;
    if (m_host && !m_host->layout()) {
        /* QStackedLayout 不支持对齐，会把页面拉到中间；原厂画布是贴左上角的，
         * 所以套一层 QGridLayout 专门做左上对齐。 */
        auto *outer = new QGridLayout(m_host);
        outer->setContentsMargins(8, 8, 8, 8);
        m_stackHost = new QWidget(m_host);
        auto *l = new QStackedLayout(m_stackHost);
        l->setContentsMargins(0, 0, 0, 0);
        l->setStackingMode(QStackedLayout::StackOne);
        outer->addWidget(m_stackHost, 0, 0, Qt::AlignLeft | Qt::AlignTop);
        outer->setRowStretch(1, 1);
        outer->setColumnStretch(1, 1);
    }
    rebuildScreens();
}

void CanvasManager::rebuildScreens()
{
    if (!m_host) {
        return;
    }
    auto *l = qobject_cast<QStackedLayout *>(m_stackHost ? m_stackHost->layout() : nullptr);
    if (!l) {
        return;
    }
    while (l->count() > 0) {
        QWidget *w = l->widget(0);
        l->removeWidget(w);
        w->deleteLater();
    }
    m_screens.clear();

    for (UiNode *page : m_model.pages()) {
        auto *s = new ScenesScreen(m_stackHost);
        s->setShowGrid(m_showGrid);
        s->setShowHidden(m_showHidden);
        s->setSolo(m_solo);
        s->setZoom(m_zoom);
        s->setPage(page);
        connect(s, &ScenesScreen::nodeSelected, this, &CanvasManager::nodeSelected);
        connect(s, &ScenesScreen::geometryEdited, this,
                [this](UiNode *) { setDirty(true); });
        connect(s, &ScenesScreen::structureChanged, this, [this, s]() {
            setDirty(true);
            s->rebuild();                 // 画布先照新结构重画
            emit structureChanged();      // 再让树/页面栏跟上
        });
        connect(s, &ScenesScreen::findRequested, this, &CanvasManager::findRequested);
        connect(s, &ScenesScreen::controlDropped, this, &CanvasManager::controlDropped);
        connect(s, &ScenesScreen::zoomStepRequested, this, [this](int step) {
            /* 按档位走，不是线性加减 —— 线性的话从 100% 滚到 800% 要滚半天 */
            static const int kSteps[] = { 25, 50, 75, 100, 150, 200, 300, 400, 600, 800 };
            const int n = int(sizeof(kSteps) / sizeof(kSteps[0]));
            int i = 0;
            while (i < n - 1 && kSteps[i] < m_zoom) {
                ++i;
            }
            i = qBound(0, i + step, n - 1);
            setZoom(kSteps[i]);
        });
        connect(s, &ScenesScreen::deletePageRequested,
                this, &CanvasManager::onDelCurrentScenesScreen);
        l->addWidget(s);
        m_screens.append(s);
    }
    if (!m_model.pages().isEmpty()) {
        m_pageSize = m_model.pages().first()->rect.size();
    }
    m_current = qBound(0, m_current, qMax(0, m_screens.size() - 1));
    l->setCurrentIndex(m_current);
    emit pagesChanged();
}

void CanvasManager::setZoom(int percent)
{
    const int z = qBound(25, percent, 800);
    if (z == m_zoom) {
        return;
    }
    m_zoom = z;
    for (ScenesScreen *s : m_screens) {
        s->setZoom(z);
    }
    emit zoomChanged(m_zoom);
    emit statusMessage(tr("缩放 %1%").arg(m_zoom));
}

int CanvasManager::zoomToFit(const QSize &viewport)
{
    ScenesScreen *s = currentScreen();
    if (!s || !s->page() || !s->page()->rect.isValid() || !viewport.isValid()) {
        return m_zoom;
    }
    const QSize page = s->page()->rect.size();
    if (page.width() <= 0 || page.height() <= 0) {
        return m_zoom;
    }
    /* 留一点边，别顶着滚动条 */
    const int pad = 24;
    const int zw = (viewport.width() - pad) * 100 / page.width();
    const int zh = (viewport.height() - pad) * 100 / page.height();
    setZoom(qMin(zw, zh));
    return m_zoom;
}

void CanvasManager::setShowHidden(bool on)
{
    m_showHidden = on;
    for (ScenesScreen *s : m_screens) {
        s->setShowHidden(on);
    }
    emit statusMessage(on ? tr("显示默认隐藏项") : tr("隐藏默认隐藏项"));
}

void CanvasManager::setSolo(bool on)
{
    m_solo = on;
    for (ScenesScreen *s : m_screens) {
        s->setSolo(on);
    }
    emit statusMessage(on ? tr("单独预览：开") : tr("单独预览：关"));
    emit screenListChanged();
}

/** 翻到当前页的上一个/下一个画面。选中它就等于预览它。 */
void CanvasManager::stepScreen(int delta)
{
    ScenesScreen *s = currentScreen();
    if (!s) {
        return;
    }
    const QVector<UiNode *> all = s->screens();
    if (all.isEmpty()) {
        return;
    }
    const int cur = qMax(0, s->currentScreenIndex());
    const int to = qBound(0, cur + delta, all.size() - 1);
    if (to == cur && delta != 0) {
        return;
    }
    s->selectNode(all.at(to));
    emit nodeSelected(all.at(to));
}

ScenesScreen *CanvasManager::screen(int i) const
{
    return (i >= 0 && i < m_screens.size()) ? m_screens.at(i) : nullptr;
}

ScenesScreen *CanvasManager::currentScreen() const
{
    return screen(m_current);
}

void CanvasManager::setCurrentPage(int i)
{
    if (i < 0 || i >= m_screens.size() || i == m_current) {
        return;
    }
    m_current = i;
    if (auto *l = qobject_cast<QStackedLayout *>(m_stackHost ? m_stackHost->layout() : nullptr)) {
        l->setCurrentIndex(i);
    }
    m_model.setActivePage(i);
    emit currentPageChanged(i);
}

bool CanvasManager::openProject(const QString &path, QString *err)
{
    if (!m_model.load(path, err)) {
        return false;
    }
    m_current = m_model.activePage();
    rebuildScreens();
    emit projectChanged();
    emit statusMessage(tr("已打开 %1（%2 页）").arg(QFileInfo(path).fileName())
                       .arg(m_model.pages().size()));
    return true;
}

bool CanvasManager::saveProjectAs(const QString &path, QString *err)
{
    if (!m_model.save(path, err)) {
        return false;
    }
    setDirty(false);
    writeProjectIni(path);
    /* 原厂还会在工程目录里留一份 autosave.json（这个名字在 ui-tools.exe
     * 里能搜到）。保存时同步写一份，工具崩了还能捞回来。 */
    m_model.saveAutosave(QFileInfo(path).absolutePath());
    emit statusMessage(QStringLiteral(" 工程已保存(%1)").arg(path));
    return true;
}

/* 把工程文件名写回 config/ini/project.ini。
 * 这是原厂那套"双击 step2 就出资源"能成立的前提 —— QtToolBin 不带参数时
 * 就是从这里读 projectfilename 的。新建工程后不写，下游就断链。
 * 只动这一个键，别的（projectid / projectrotate / projectbatscript）原样留着。 */
void CanvasManager::writeProjectIni(const QString &jsonPath)
{
    const QDir dir(QFileInfo(jsonPath).absolutePath());
    const QString ini = dir.absoluteFilePath(QStringLiteral("config/ini/project.ini"));
    if (!QDir().mkpath(QFileInfo(ini).absolutePath())) {
        return;
    }
    QSettings st(ini, QSettings::IniFormat);
    const QString name = QFileInfo(jsonPath).fileName();
    if (st.value(QStringLiteral("Project/projectfilename")).toString() == name) {
        return;
    }
    st.setValue(QStringLiteral("Project/projectfilename"), name);
    if (!st.contains(QStringLiteral("Project/projectbatscript"))) {
        st.setValue(QStringLiteral("Project/projectbatscript"),
                    QStringLiteral("copy_file.bat"));
    }
    if (!st.contains(QStringLiteral("Project/projectrotate"))) {
        st.setValue(QStringLiteral("Project/projectrotate"), 0);
    }
    if (!st.contains(QStringLiteral("Project/projectid"))) {
        st.setValue(QStringLiteral("Project/projectid"), 0);
    }
    st.sync();
}

/* --- 菜单/工具栏直连的槽 --- */

/**
 * 有未保存改动时先拦一道。
 *
 * ★ 这是原厂的限制，之前重建版完全没有：改了一半点"打开"，改动直接没了。
 * 原厂两条串："关闭工程提示" + "当前编辑的工程有新的修改没有保存,选请择
 * <保存>进行保存."（"选请择"是原厂自己的笔误，这里照抄，免得用户觉得是
 * 两个不同的工具）。按钮是 保存 / 取消 —— 正文点名了 <保存>。
 * @return true 表示可以继续（已保存或用户放弃保存）。
 */
void CanvasManager::setDirty(bool d)
{
    if (m_model.dirty() == d) {
        return;
    }
    m_model.setDirty(d);
    emit dirtyChanged(d);
}

bool CanvasManager::confirmDiscardChanges()
{
    if (!m_model.dirty()) {
        return true;
    }
    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("关闭工程提示"));
    box.setText(QStringLiteral("当前编辑的工程有新的修改没有保存,选请择<保存>进行保存."));
    QAbstractButton *save = box.addButton(QStringLiteral("保存"), QMessageBox::AcceptRole);
    QAbstractButton *drop = box.addButton(QStringLiteral("不保存"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) {
        onSaveProject();
        return !m_model.dirty();        // 存失败/取消了另存，就别往下走
    }
    return box.clickedButton() == drop;
}

void CanvasManager::onOpenProject()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString f = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("打开工程文件"), QString(),
        QStringLiteral("ui-tools 工程 (*.json)"));
    if (f.isEmpty()) {
        return;
    }
    QString err;
    if (!openProject(f, &err)) {
        QMessageBox::warning(nullptr, tr("打开失败"), err);
    }
}

void CanvasManager::onSaveProject()
{
    if (m_model.filePath().isEmpty()) {
        onSaveAsProject();
        return;
    }
    QString err;
    if (!saveProjectAs(m_model.filePath(), &err)) {
        QMessageBox::warning(nullptr, tr("保存失败"), err);
    }
}

void CanvasManager::onSaveAsProject()
{
    const QString f = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("保存工程文件"), m_model.name() + QStringLiteral(".json"),
        QStringLiteral("ui-tools 工程 (*.json)"));
    if (f.isEmpty()) {
        return;
    }
    QString err;
    if (!saveProjectAs(f, &err)) {
        QMessageBox::warning(nullptr, tr("保存失败"), err);
    }
}

void CanvasManager::onCreateNewProject()
{
    /* 原厂先问"是否关闭当前工程,新建工程?"，再问没保存的改动 */
    if (!m_model.pages().isEmpty()) {
        QMessageBox box;
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("新建工程提示"));
        box.setText(QStringLiteral("是否关闭当前工程,新建工程?"));
        box.addButton(QMessageBox::Yes)->setText(QStringLiteral("确定"));
        box.addButton(QMessageBox::No)->setText(QStringLiteral("取消"));
        if (box.exec() != QMessageBox::Yes) {
            return;
        }
    }
    if (!confirmDiscardChanges()) {
        return;
    }
    /* 原程序在这里弹 ProjectDialog（类名与它的自动连接槽 on_pushButton_clicked
     * 一起从二进制元数据里逆向出来），照此调用。 */
    ProjectDialog dlg;
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dlg.projectName();
    m_pageSize = dlg.pageSize();
    m_model.createDefault(name, m_pageSize);
    m_model.setLangExcel(dlg.languageExcel());
    m_current = 0;
    rebuildScreens();
    emit projectChanged();
}

void CanvasManager::onUpdateNewProjectSize()
{
    bool ok = false;
    const int w = QInputDialog::getInt(nullptr, tr("页面尺寸"), tr("宽（像素）"),
                                       m_pageSize.width(), 1, 2048, 1, &ok);
    if (!ok) {
        return;
    }
    const int h = QInputDialog::getInt(nullptr, tr("页面尺寸"), tr("高（像素）"),
                                       m_pageSize.height(), 1, 2048, 1, &ok);
    if (!ok) {
        return;
    }
    m_pageSize = QSize(w, h);
    for (UiNode *p : m_model.pages()) {
        p->rect = QRect(p->rect.topLeft(), m_pageSize);
        p->markDirty();
    }
    rebuildScreens();
    setDirty(true);
}

void CanvasManager::onGlobalBtn()
{
    GlobalSettings dlg;
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    // 设置立刻生效：网格开关和间距都在这里
    m_showGrid = GlobalSettings::value(QStringLiteral("canvas/grid"), 1).toInt() != 0;
    setZoom(GlobalSettings::value(QStringLiteral("canvas/defaultZoom"), 100).toInt());
    emit statusMessage(tr("全局设置已保存"));
}

void CanvasManager::onSshoot()
{
    ScenesScreen *s = currentScreen();
    if (!s) {
        return;
    }
    const QString f = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("保存PNG截图"), QStringLiteral("page%1.png").arg(m_current),
        QStringLiteral("PNG (*.png)"));
    if (f.isEmpty()) {
        return;
    }
    if (s->grab().save(f)) {
        emit statusMessage(tr("截图已存到 %1").arg(f));
    }
}

void CanvasManager::onZoomProject()
{
    /* 原厂这个"工程缩放"是**换屏幕尺寸**（128x64 -> 240x240 之类），
     * 所有控件坐标按比例换算；画布那个百分比缩放是 ScenesScreen::setZoom()，
     * 两码事。对话框里的 lab_oldw / lab_oldh / spinBoxW / spinBoxH 就是干这个的。 */
    const QSize oldSize = m_pageSize;
    if (oldSize.isEmpty() || m_model.pages().isEmpty()) {
        return;
    }
    ZoomProject dlg;
    dlg.setOldSize(oldSize);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QSize ns = dlg.newSize();
    if (ns == oldSize || ns.isEmpty()) {
        return;
    }
    const double sx = double(ns.width()) / oldSize.width();
    const double sy = double(ns.height()) / oldSize.height();

    for (UiNode *page : m_model.pages()) {
        page->forEach([&](UiNode *n) {
            if (n == page) {
                return true;                 // 页面本身下面统一改
            }
            for (int st = 0; st < n->cssStateCount(); ++st) {
                const QRect r = n->rectOf(st);
                if (r.isNull()) {
                    continue;
                }
                n->setRectOf(st, QRect(qRound(r.x() * sx), qRound(r.y() * sy),
                                       qRound(r.width() * sx), qRound(r.height() * sy)));
            }
            return true;
        });
    }
    m_pageSize = ns;
    for (UiNode *p : m_model.pages()) {
        p->rect = QRect(p->rect.topLeft(), m_pageSize);
        p->markDirty();
    }
    rebuildScreens();
    setDirty(true);
    emit statusMessage(tr("工程已从 %1x%2 缩放到 %3x%4")
                       .arg(oldSize.width()).arg(oldSize.height())
                       .arg(ns.width()).arg(ns.height()));
}

void CanvasManager::onAboutBtn()
{
    /* 版式照抄原厂那三行（图标 + 名称 / 开发者 / 维护者），署名写自己。 */
    QMessageBox::about(nullptr, QStringLiteral("关于"),
                       QStringLiteral("<b><img src=':/icon/icons/smallpt.png'></b>"
                          "<p>名称: UITools </p>"
                          "<p>开发者: Claude Opus 5 (Anthropic)</p>"
                          "<p>维护者: Claude Opus 5 (Anthropic)</p>"));
}

void CanvasManager::onSelectGrid()
{
    m_showGrid = !m_showGrid;
    emit statusMessage(m_showGrid ? tr("网格：开") : tr("网格：关"));
    for (ScenesScreen *s : m_screens) {
        s->setShowGrid(m_showGrid);      // 以前只 update()，画布根本不知道开关变了
    }
}

void CanvasManager::onCreateNewScenesScreen()
{
    auto *page = new UiNode;
    page->cls  = page->name = QStringLiteral("ScenesScreen");
    page->type = QStringLiteral("page");
    page->caption = tr("页面_%1").arg(m_model.pages().size());
    page->version = QStringLiteral("1");
    page->rect = QRect(1, 1, m_pageSize.width(), m_pageSize.height());
    page->markDirty();
    m_model.pages().append(page);
    setDirty(true);
    rebuildScreens();
    setCurrentPage(m_model.pages().size() - 1);
    emit projectChanged();
}

void CanvasManager::onDelCurrentScenesScreen()
{
    if (m_model.pages().size() <= 1) {
        EditorOps::tip(nullptr, tr("至少要保留一页。"));
        return;
    }
    /* 原厂用的是同一个"删除提示"框（按钮叫 <删除>），文案里的 %1 换成"页面" */
    if (!EditorOps::confirmDelete(nullptr, QStringLiteral("页面"))) {
        return;
    }
    delete m_model.pages().takeAt(m_current);
    m_current = qMax(0, m_current - 1);
    setDirty(true);
    rebuildScreens();
    emit projectChanged();
}

void CanvasManager::onConfProject()
{
    ConfigProject dlg;
    dlg.setProjectName(m_model.name());
    dlg.setExcelPath(m_model.langExcel());
    dlg.setLanguageMask(m_model.languageMask());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    if (dlg.projectName() != m_model.name()) {
        m_model.setName(dlg.projectName());
    }
    m_model.setLangExcel(dlg.excelPath());
    m_model.setLanguageMask(dlg.languageMask());
    // 属性面板里"文字列表"要用这张表，换了就得让它知道
    PropertyContext::instance().excelPath = dlg.excelPath();
    emit projectChanged();
    emit statusMessage(tr("工程配置已更新"));
}
