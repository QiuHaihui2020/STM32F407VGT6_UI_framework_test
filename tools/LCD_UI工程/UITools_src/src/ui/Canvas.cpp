#include "Canvas.h"

#include <QJsonObject>
#include "Preview.h"

#include <QSettings>

#include "ConfigProject.h"
#include "Property.h"
#include "GlobalSettings.h"
#include "ZoomProject.h"
#include "Forms.h"
#include "ProjectDialog.h"
#include "EditorOps.h"

#include <QPainter>
#include <QResizeEvent>
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


/**
 * 像素网格的覆盖层。
 *
 * 【为什么要单独一层】网格原来画在 ScenesScreen::paintEvent 里，那是父控件的
 * 背景，Qt 之后才画子控件。点亮的像素不透明，把网格盖掉了 —— 于是只有熄灭
 * 的地方（子控件那里透明，露出父控件背景）看得见网格，点亮的地方看不见。
 * 覆盖层是子控件，rebuild 之后 raise() 到最上面，就压在所有内容之上了。
 *
 * 【线的颜色】用**实色**，亮区和熄灭区画出来一模一样。
 * 一度用过 XOR（拿深灰异或底色），那样不管底色配成什么都不会消失，但两边
 * 出来是两种颜色，连对比度都不对称（默认配色下暗区 +28、亮区 −60）。
 * 现在改成单色，颜色放进[全局设置]让人自己配 —— 代价是配得和亮/灭色太接近
 * 就看不见，所以那一项挂了提示。
 */
class GridOverlay : public QWidget
{
public:
    explicit GridOverlay(ScenesScreen *owner)
        : QWidget(owner), m_owner(owner)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_owner || !m_owner->showChrome() || !m_owner->showGrid()) {
            return;
        }
        const int zoom = m_owner->zoom();
        if (zoom < 300) {            // 太小了画出来是一团糊
            return;
        }
        QPainter p(this);
        p.setPen(Preview::monoGrid());
        const int step = zoom / 100;
        for (int x = 0; x < width(); x += step) {
            p.drawLine(x, 0, x, height());
        }
        for (int y = 0; y < height(); y += step) {
            p.drawLine(0, y, width(), y);
        }
    }

private:
    ScenesScreen *m_owner = nullptr;
};

ScenesScreen::ScenesScreen(QWidget *parent)
    /* 页底色 = 像素熄灭的颜色（[全局设置]里配），这样画布上看到的
     * 明暗关系就是屏上的明暗关系。 */
    : QFrame(parent), m_bg(Preview::monoDark())
{
    setFrameShape(QFrame::Box);
    setFrameShadow(QFrame::Plain);
    setAutoFillBackground(true);
    setAcceptDrops(true);      // 控件列表里拖过来的东西落在这儿
    m_gridOverlay = new GridOverlay(this);
}

void ScenesScreen::resizeEvent(QResizeEvent *e)
{
    QFrame::resizeEvent(e);
    if (m_gridOverlay) {
        m_gridOverlay->setGeometry(rect());
        m_gridOverlay->raise();
    }
}

ScenesScreen::~ScenesScreen()
{
    /* 【必须在这里把子控件的 destroyed 连接掐掉】
     *
     * 子控件（BaseForm）是在 QWidget::~QWidget() 里被删的，而那一步发生在
     * **本对象的成员已经析构完之后**（析构顺序：本类析构体 -> 本类成员 ->
     * 基类 QWidget::~QWidget 删子控件 -> QObject::~QObject 断连）。
     * buildRecursive() 给每个控件挂了 destroyed 回调去清 m_forms/m_userHidden，
     * 到那一步回调再进来，摸的就是已经析构的 QHash —— ASan 报
     * heap-use-after-free in QHash<UiNode*,BaseForm*>::value，进程退出时崩。
     *
     * 析构体是最后一个"成员还活着"的时机，在这儿断连正好。 */
    for (BaseForm *f : m_forms) {
        disconnect(f, nullptr, this, nullptr);
    }
    m_forms.clear();
}

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
    /* 改倍率只是把 QWidget 重摆一遍，模型一个字节都没动；
     * 选中由 rebuild() 自己保住（见那边的注释）。 */
    rebuild();
}

void ScenesScreen::rebuild()
{
    for (BaseForm *f : m_forms) {
        /* 【必须当场脱离画布，不能只 deleteLater】deleteLater 要等事件循环
         * 转一圈才真销毁，在那之前旧控件还是画布的**可见子控件**，还会被
         * 重绘。而重建的常见诱因就是删了个节点—— 旧控件的 m_node 这时
         * 已经是野指针，一画就踩已释放内存（--ops-test 全程不回事件循环，
         * 旧控件成批堆着，grab/截屏 时稳定堆损坏 0xC0000374）。
         * 不能直接 delete：本函数常常是被控件自己的信号叫起来的，
         * 当场删发信号的对象会栈上崩。所以先摘干净再排队销毁。 */
        f->setSelected(false);         // 顺带收起挂在画布上的 8 个缩放手柄
        f->hide();
        f->setParent(nullptr);
        f->deleteLater();
    }
    m_forms.clear();
    /* 眼睛藏的那些也一起清掉：这些是裸指针，重建之后全是野指针。
     * 反正重建意味着结构变了，重新来过是合理的。 */
    m_userHidden.clear();
    /* m_selected 是裸指针，画布一重建它就可能指向已经释放的节点；
     * 而且不清掉的话，重建后再选中同一个节点会被 selectNode() 当成"没变化"
     * 而吞掉。先记一份，等新控件都建好了再按"还在不在新表里"决定要不要选回来。 */
    UiNode *const keep = m_selected;
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

    /* 【选中必须跨过重建活下来】applyVisibility() 的"选中即隔离"是靠
     * m_selected 找那个顶层布局的；选中一没，它就退回去把这一页所有布局
     * 一起画出来 —— 屏幕上就是好几个全屏布局叠成一团。而 rebuild() 的
     * 触发点大多**根本没动结构**：改一个属性、拖进一个新控件、换个倍率、
     * 改预览配色，都会走到这儿。每来一次就散一次架，没法用。
     *
     * 安全性由 m_forms 兜着：这张表刚刚照活模型重建过，keep 还在里面
     * 就说明这个节点还挂在树上；不在就老实不恢复（删节点那条路进来
     * 就是这种情况，调用方已经先把 m_selected 清了）。keep 只做键比较，
     * 不解引用，所以哪怕它已经是野指针也不会踩内存。
     *
     * 【不发 nodeSelected】这只是把重建前的状态摆回去，不是用户又选了
     * 一次。发出去会让属性面板整个重建 —— 而"改属性"正是最常见的触发点，
     * 输入框会在打字的当口被换掉。 */
    if (keep && m_forms.contains(keep)) {
        m_selected = keep;
        for (auto it = m_forms.constBegin(); it != m_forms.constEnd(); ++it) {
            it.value()->setSelected(it.key() == keep);
        }
    }
    applyVisibility();
    /* 刚建出来的控件会压在覆盖层上面，重新提一次 */
    if (m_gridOverlay) {
        m_gridOverlay->setGeometry(rect());
        m_gridOverlay->raise();
    }
    update();
}

void ScenesScreen::buildRecursive(UiNode *n, QWidget *parentWidget)
{
    BaseForm *f = createFormForClass(n->cls, parentWidget);
    /* 倍率要在 bind() 之前给：bind 里会调 syncRectFromNode() 摆位置 */
    f->setDisplayZoom(m_zoom);
    f->setShowChrome(m_showChrome);
    f->bind(n);
    f->show();
    m_forms.insert(n, f);
    /* 【控件没了就得从索引里摘掉】BaseForm::onDeleteMe() 只 deleteLater()
     * 自己，不通知这边；等事件循环真销毁它，m_forms 里就是个野指针，
     * 之后任何遍历（重绘、setShowChrome、grab/截屏）都在踩已释放内存。
     * 挂在 destroyed 上而不是让删除路径自己收尾 —— 删除的入口不止一个。
     * lambda 里的 n 只当键用，不解引用，所以节点已经释放也安全。 */
    connect(f, &QObject::destroyed, this, [this, n, f]() {
        /* 【必须先确认表里存的还是它】rebuild() 的做法是"旧的 deleteLater +
         * 立刻建新的"，同一个节点键上很快就换成了新控件；等事件循环真去销毁
         * 旧控件时才轮到这个 lambda —— 那时无条件按键删，删掉的是**新**控件
         * 那一项，后面 formFor() 就返回空指针。
         * f 只做地址比较，不解引用，指向已释放对象也安全。 */
        if (m_forms.value(n) != f) {
            return;
        }
        m_forms.remove(n);
        m_userHidden.remove(n);
        if (m_selected == n) {
            m_selected = nullptr;
        }
    });
    /* 在画布上直接点控件，也要让树/属性面板跟着走。
     * BaseForm::mousePressEvent 只调 setSelected(true)，既不通知外面、
     * 也不取消别的控件的选中框，所以这里拦一道press。事件过滤器跑在
     * 控件自己的处理之前，不影响它的拖动逻辑。 */
    f->installEventFilter(this);

    /* 结构类操作（右键删除/粘贴/挪层/列表加行）由控件自己发起，画布只负责
     * 往上转发一次 —— 树、页面栏、属性面板都挂在主窗口那一层。 */
    connect(f, &BaseForm::structureChanged, this, [this]() {
        m_selected = nullptr;          // 被删掉的那个可能就是它，别留悬空指针
        /* 【必须重建画布】结构类操作动的是节点树本身。删除尤其致命：
         * UiNode 析构会递归 delete children，整棵子树一起没；而画布上那些
         * **子控件**的 BaseForm 还活着，m_node 全是野指针。以前这里只往上
         * 转发一次信号，主窗口那边也只 reload 树和页面栏，谁都没重建画布 ——
         * 于是删完一个带子节点的控件，下一次重绘/截图（onSshoot 就是
         * ScenesScreen::grab()）就在踩已释放内存，实测稳定堆损坏 0xC0000374。 */
        rebuild();
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
    /* 孩子全建完了才轮到它 —— 列表要在这里把各行摆到格子上。 */
    f->onSubtreeBuilt();
}

/**
 * 谁显示、谁淡出。
 *
 * 【这是"全部布局叠在一起"的解药】一个图层下经常挂着好几个**全屏尺寸**的
 * 布局（SmallColorTFT 页0 就有 5 个 (0,0,128,64) 的），它们是互斥的
 * 界面状态（主界面/音量/EQ/菜单…），运行时只显示一个。全画出来的话最上面
 * 那个把下面全盖死，什么都编不了。
 *
 * 两条规则：
 *   1. **顶层布局**里标了"默认隐藏"(element_css.invisible == "true") 的默认
 *      不画 —— 剩下的那个就是运行时的默认画面。
 *      但**选中它或它的子孙时要画出来**，否则树上点得到、画布上摸不着。
 *   2. 选中即隔离：找到选中项所属的那个"顶层布局"（父节点是图层的那一层），
 *      同级的其它顶层布局整棵淡出。淡出不是隐藏，照样能点，点一下就换它清晰。
 *
 * 【为什么规则 1 只看顶层这一层】叶子控件上的"默认隐藏"是**写进资源给固件
 * 运行时用的**位（比如文件列表里的图标，程序滚到哪一项才把它显出来），不是
 * 编辑器的可见性开关。拿它去藏画布上的控件，等于让人没法摆放这些控件 ——
 * 画布上看不看得见，只由对象树那只眼睛（和右键的显示/隐藏）管。
 */
void ScenesScreen::applyVisibility()
{
    /* 【网格覆盖层要重新提到最上面】FormResizer::setSelected(true) 里有
     * 一句 raise()，选中一个控件就把它顶到覆盖层之上，那块区域的网格就没了。
     * applyVisibility() 是所有"选中/可见性变了"的必经之路，在这儿补一次
     * 最省事，也不会漏。 */
    if (m_gridOverlay) {
        m_gridOverlay->raise();
    }
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
            if (EditorOps::isTopScreen(a) && a->isDefaultHidden()
                && !onPath.contains(a)) {
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
        /* 布局能落在：图层（手册："点击布局并拖动到图层上"）、布局（套娃，
         * 既有工程里 3 例）、**列表和表格**。
         * 列表那一条最要紧：列表的行/项就是一个个布局，既有的两个工程里
         * 一共 165 个布局挂在列表的 listwidget 键下。以前这里不认列表，
         * 于是垂直列表永远建不出行来，看上去就是"列表下面放不了东西"。 */
        for (UiNode *n = hit; n; n = n->parent) {
            if (EditorOps::acceptsLayout(n)) {
                *target = n;
                return true;
            }
        }
        return false;
    }
    /* 普通控件只落在布局里。落在列表上时会一路上溯到列表的父布局 ——
     * 列表的孩子必须是"行"（NewLayout），塞个裸控件进去，生成出来是
     * 一个从没出现过的形状，固件按行遍历也拿不到它。 */
    for (UiNode *n = hit; n; n = n->parent) {
        if (EditorOps::acceptsWidget(n)) {
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

bool ScenesScreen::resolveDropTargetForTest(UiNode *hit, const QString &cls,
                                            UiNode **target) const
{
    return dropTargetFor(hit, cls, target);
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

void ScenesScreen::setShowChrome(bool on)
{
    if (m_showChrome == on) {
        return;
    }
    m_showChrome = on;
    for (BaseForm *f : m_forms) {
        f->setShowChrome(on);
    }
    update();                      // 网格是本页自己画的
}

void ScenesScreen::setShowGrid(bool on)
{
    if (m_gridOverlay) {
        m_gridOverlay->update();
    }
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

    /* 像素网格不在这儿画 —— 这里是父控件的背景，子控件会盖在上面，
     * 点亮的像素就把网格糊掉了。挪去 GridOverlay（铺满画布、raise 到最上面）。
     * 【历史】以前这里还只判 m_zoom，工具栏那个"网格开关"翻的是
     * CanvasManager::m_showGrid，画布压根没看，点了只有状态栏文字会变。 */
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
    /* 粘贴/列表加行要给克隆出来的节点重分配 ID 号，去重范围得是整个工程 */
    EditorOps::setModel(&m_model);
    /* 列表右键「添加行」在 BaseForm 里，够不着管理器，得从这儿把模板库递过去 */
    EditorOps::setLibrary(&m_lib);
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
        /* QStackedLayout 不支持对齐，会把页面拉到中间；画布要贴左上角，
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

void CanvasManager::clearScreens()
{
    auto *l = qobject_cast<QStackedLayout *>(m_stackHost ? m_stackHost->layout()
                                                         : nullptr);
    if (l) {
        while (l->count() > 0) {
            QWidget *w = l->widget(0);
            l->removeWidget(w);
            /* 【立刻删，不能 deleteLater】调用方紧接着就要把整棵节点树换掉。
             * 推迟到事件循环再删的话，那会儿控件手里的 UiNode 已经是野指针。 */
            delete w;
        }
    }
    m_screens.clear();
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
    clearScreens();

    for (UiNode *page : m_model.pages()) {
        auto *s = new ScenesScreen(m_stackHost);
        s->setShowGrid(m_showGrid);
        s->setShowChrome(m_showChrome);
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

void CanvasManager::setShowChrome(bool on)
{
    m_showChrome = on;
    for (ScenesScreen *s : m_screens) {
        s->setShowChrome(on);
    }
    emit statusMessage(on ? tr("辅助线：显示") : tr("辅助线：隐藏"));
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
    const int cur = qMax(0, s->currentScreenIndex());
    gotoScreen(cur + delta);
}

void CanvasManager::gotoScreen(int index)
{
    ScenesScreen *s = currentScreen();
    if (!s) {
        return;
    }
    const QVector<UiNode *> all = s->screens();
    if (all.isEmpty()) {
        return;
    }
    const int to = qBound(0, index, all.size() - 1);
    if (to == s->currentScreenIndex()) {
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

void CanvasManager::newProjectForTest(const QString &name, const QSize &pageSize)
{
    m_pageSize = pageSize;
    const ControlTemplate *lt = m_lib.byType(QStringLiteral("NewLayer"));
    const ControlTemplate *ot = m_lib.byType(QStringLiteral("NewLayout"));
    /* 先拆画布再换模型，顺序反了会摸野指针 —— 见 clearScreens 的说明 */
    clearScreens();
    m_model.createDefault(name, pageSize,
                          lt ? lt->raw : QJsonObject(),
                          ot ? ot->raw : QJsonObject());
    m_current = 0;
    rebuildScreens();
    setDirty(true);
    emit projectChanged();
}

void CanvasManager::addPageForTest()
{
    onCreateNewScenesScreen();
}

int CanvasManager::healBrokenNodes()
{
    int healed = 0;
    for (UiNode *pg : m_model.pages()) {
        pg->forEach([&](UiNode *n) {
            if (n == pg) {
                return true;
            }
            const ControlTemplate *t = m_lib.byType(n->type);
            if (!t || t->raw.isEmpty()) {
                return true;                 // 认不出的类型，不瞎补
            }
            /* 照模板建一个临时节点当参照 —— 那一整套 struct/enum/min/max
             * 是属性面板和下游 QtToolBin 共同依赖的，自己拼一个迟早对不上。 */
            UiNode *tmp = ProjectModel::fromJsonObject(t->raw, nullptr);
            if (!tmp) {
                return true;
            }

            /* 【模板里有而节点没有的，全补，按模板的顺序】
             * 以前这里只补 element_css 和 id —— 于是坏节点修完是
             * "有坐标、没有事件属性"，属性面板上一眼就看出来缺东西
             * （用户一比就看出来了）。缺哪条补哪条，别挑食。
             *
             * 顺序照模板：属性面板是按 props 的次序铺的，补到末尾的话
             * 事件属性会跑到滚动方式前面去，排版就乱了。 */
            const bool needCss = (n->findProp(QStringLiteral("element_css")) == nullptr);
            QVector<UiProperty> merged;
            bool added = false;
            for (const UiProperty &tp : tmp->props) {
                UiProperty *own = tp.name.isEmpty() ? nullptr : n->findProp(tp.name);
                if (own) {
                    merged.append(*own);
                } else {
                    merged.append(tp);       // 模板那条原样搬过来
                    added = true;
                }
            }
            /* 模板里没有、节点自己有的（老工程的遗留键）一条都不能丢 */
            for (const UiProperty &op : n->props) {
                bool inTpl = false;
                for (const UiProperty &tp : tmp->props) {
                    if (!tp.name.isEmpty() && tp.name == op.name) {
                        inTpl = true;
                        break;
                    }
                }
                if (!inTpl) {
                    merged.append(op);
                }
            }
            delete tmp;

            UiProperty *idp = n->findProp(QStringLiteral("id"));
            const bool needId = (idp == nullptr || idp->ename.isEmpty());
            if (!added && !needId) {
                return true;                 // 什么都不缺
            }
            n->props = merged;
            n->markDirty();
            ++healed;

            /* 缺唯一 ID 号：空的宏名会让 ename.h 写出 `#define  0XC30002` */
            idp = n->findProp(QStringLiteral("id"));
            if (idp && idp->ename.isEmpty()) {
                idp->ename = m_model.uniqueEname();
                idp->dirty = true;
            }
            if (!needCss || !n->findProp(QStringLiteral("element_css"))) {
                return true;                 // 几何本来就有（或模板也没有）
            }

            /* 给个说得过去的矩形：在列表/表格里就是它那一格，
             * 否则退到父容器的尺寸（再不行 32x16）。 */
            int idx = 0;
            if (n->parent) {
                for (int i = 0; i < n->parent->children.size(); ++i) {
                    if (n->parent->children.at(i).second == n) {
                        idx = i;
                        break;
                    }
                }
            }
            QRect r = EditorOps::cellRectFor(n->parent, idx);
            if (!r.isValid()) {
                r = (n->parent && n->parent->rect.isValid())
                    ? QRect(0, 0, n->parent->rect.width(), n->parent->rect.height())
                    : QRect(0, 0, 32, 16);
            }
            n->rect = r;
            n->setRectOf(0, r);
            n->markDirty();
            return true;
        });
    }
    return healed;
}

/* 工程一换，配置文件跟着换到那个工程目录下，并把跟配置走的东西重读一遍。
 * 预览配色和「预览文字」都存在 ui-config 里，不重读的话画布上还是上一个
 * 工程的样子。 */
void CanvasManager::bindSettingsToProject(const QString &jsonPath)
{
    const QString dir = jsonPath.isEmpty()
                        ? QString()
                        : QFileInfo(jsonPath).absolutePath();
    GlobalSettings::setProjectDir(dir);
    Preview::reloadMonoColors();
    emit previewStyleChanged();
}

bool CanvasManager::openProject(const QString &path, QString *err)
{
    /* 同理：load() 一成功旧节点树就没了，先把画布拆掉 */
    clearScreens();
    if (!m_model.load(path, err)) {
        return false;
    }
    bindSettingsToProject(path);
    /* 【打开就地修】旧版建出来的空壳节点（没有 element_css）在这儿补齐，
     * 否则生成资源时它和它整棵子树全是零尺寸，烧进去不显示。
     * 补完是**脏**的，用户下次保存就落到文件里。 */
    const int healed = healBrokenNodes();
    m_current = m_model.activePage();
    rebuildScreens();
    emit projectChanged();
    if (healed > 0) {
        setDirty(true);
        emit statusMessage(tr("已打开 %1（%2 页）；修复了 %3 个缺样式/缺ID号的控件，"
                              "保存后生效")
                           .arg(QFileInfo(path).fileName())
                           .arg(m_model.pages().size()).arg(healed));
    } else {
        emit statusMessage(tr("已打开 %1（%2 页）").arg(QFileInfo(path).fileName())
                           .arg(m_model.pages().size()));
    }
    return true;
}

bool CanvasManager::saveProjectAs(const QString &path, QString *err)
{
    if (!m_model.save(path, err)) {
        return false;
    }
    /* 另存到别处 = 换了工程目录，配置也跟过去（下次打开那份才配得上） */
    bindSettingsToProject(path);
    setDirty(false);
    writeProjectIni(path);
    /* 工程目录里同时留一份 autosave.json，工具崩了还能捞回来。 */
    m_model.saveAutosave(QFileInfo(path).absolutePath());
    emit statusMessage(QStringLiteral(" 工程已保存(%1)").arg(path));
    return true;
}

/* 把工程文件名写回 config/ini/project.ini。
 * 这是"双击 step2 就出资源"那条路能成立的前提 —— QtToolBin 不带参数时
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
 * ★ 之前完全没有这道拦截：改了一半点"打开"，改动直接没了。
 * 两条串："关闭工程提示" + "当前编辑的工程有新的修改没有保存,选请择
 * <保存>进行保存."（"选请择"这个写法沿用用户熟悉的说法，不改）。
 * 按钮是 保存 / 取消 —— 正文点名了 <保存>。
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
        QStringLiteral("UI 工程 (*.json)"));
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
    /* 【先把输入框里的改动逼出来】工具栏按钮是 NoFocus，点"保存"不会让
     * 属性面板上那个输入框失焦，editingFinished 不发，刚敲的值还没进模型。 */
    EditorOps::commitPendingEdit();
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
    /* 【先把输入框里的改动逼出来】工具栏按钮是 NoFocus，点"保存"不会让
     * 属性面板上那个输入框失焦，editingFinished 不发，刚敲的值还没进模型。 */
    EditorOps::commitPendingEdit();
    const QString f = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("保存工程文件"), m_model.name() + QStringLiteral(".json"),
        QStringLiteral("UI 工程 (*.json)"));
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
    /* 先问"是否关闭当前工程,新建工程?"，再问没保存的改动 */
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
    /* 这里弹 ProjectDialog（自动连接槽是 on_pushButton_clicked）。 */
    ProjectDialog dlg;
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const QString name = dlg.projectName();
    m_pageSize = dlg.pageSize();
    /* 把 control.json 里图层/布局的模板传进去，别让它手搭 —— 手搭的没有
     * element_css，生成出来固件不显示（见 createDefault 的注释）。 */
    const ControlTemplate *lt = m_lib.byType(QStringLiteral("NewLayer"));
    const ControlTemplate *ot = m_lib.byType(QStringLiteral("NewLayout"));
    /* 先拆画布再换模型 —— 见 clearScreens 的说明。
     * 【这是个真崩】开着一个工程再点「新建工程」就会撞上。 */
    clearScreens();
    m_model.createDefault(name, m_pageSize,
                          lt ? lt->raw : QJsonObject(),
                          ot ? ot->raw : QJsonObject());
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
    applyGlobalSettings();
}

void CanvasManager::applyGlobalSettings()
{
    /* 【这里只碰[全局设置]里真有的项】
     *
     * 以前这儿还有两行：
     *     m_showGrid = GlobalSettings::value("canvas/grid", 1).toInt() != 0;
     *     setZoom(GlobalSettings::value("canvas/defaultZoom", 100).toInt());
     * 这两个键是早先那版全局设置留下的残骸。对话框重做之后
     * （五条路径 + 界面尺寸），里头根本没有网格和缩放这两项，也就没人写
     * 这两个键 —— 每次读到的都是默认值。结果是：放大到 400% 编到一半，进一次
     * [全局设置]再出来，缩放被拉回 100%、网格开关也被复位。用户什么都没改，
     * 状态却被冲掉了。删掉。
     *
     * 剩下的点阵屏预览配色是对话框里确实有的项，立刻生效 ——
     * 它只影响画面怎么画，和"更新设置要重启软件才能生效"管的
     * 那几条路径不是一回事，配颜色本来就得一边改一边看。 */
    Preview::reloadMonoColors();
    for (ScenesScreen *sc : m_screens) {
        sc->setBackgroundColor(Preview::monoDark());
        sc->rebuild();
    }
    emit previewStyleChanged();
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
    /* 这个"工程缩放"是**换屏幕尺寸**（128x64 -> 240x240 之类），
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
    /* 三行：图标 + 名称 / 开发者 / 维护者。 */
    QMessageBox::about(nullptr, QStringLiteral("关于"),
                       QStringLiteral("<b><img src=':/icons/logo.png'></b>"
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
    /* 【页的 rect 必须落成一条 property】 UiNode::rect 只是内存里的字段，
     * 存盘写出去的是 props。以前这儿只设了 rect 不建 property，于是新建的页
     * 存进 json 时连 "property" 这个键都没有 —— 重新打开后页面没有尺寸，
     * StyBuilder 拿不到父矩形，该页所有控件的 css 左/上/宽/高全留 0，
     * 烧进设备就是**整屏不显示**（实测 ui_128_64_JL02 的页 3）。
     *
     * 形状：页节点的这一条是个**裸对象**，只有 "rect" 键，
     * 连 -name 都没有（见 ProjectModel 读取那一侧的注释）。 */
    {
        QJsonObject r;
        r.insert(QStringLiteral("x"), page->rect.x());
        r.insert(QStringLiteral("y"), page->rect.y());
        r.insert(QStringLiteral("width"), page->rect.width());
        r.insert(QStringLiteral("height"), page->rect.height());
        QJsonObject po;
        po.insert(QStringLiteral("rect"), r);
        UiProperty rp;
        rp.raw = po;                      // -name 留空，就是那种裸 rect
        page->props.append(rp);
    }
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
    /* 用同一个"删除提示"框（按钮叫 <删除>），文案里的 %1 换成"页面" */
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
