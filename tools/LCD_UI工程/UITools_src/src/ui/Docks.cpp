#include "Docks.h"
#include "Canvas.h"
#include "Forms.h"
#include "Property.h"
#include "ProjectModel.h"
#include "ControlLibrary.h"
#include "EditorOps.h"
#include "Preview.h"

#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabBar>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QCursor>
#include <QInputDialog>
#include <QLineEdit>
#include <QJsonObject>

/* UiNode* 存在 item 的 UserRole 里。QVariant 不认识裸指针类型名，
 * 用 quintptr 中转最省事，也免得注册 metatype。 */
static QVariant packNode(UiNode *n)
{
    return QVariant::fromValue<quintptr>(quintptr(n));
}
static UiNode *unpackNode(const QVariant &v)
{
    return reinterpret_cast<UiNode *>(v.value<quintptr>());
}

/** 取节点的 ename（就是 ename.h 里的宏名），树的第三列显示它。 */
static QString enameOf(UiNode *n)
{
    for (const UiProperty &p : n->props) {
        if (p.name == QLatin1String("id")) {
            return p.ename;
        }
    }
    return QString();
}

/* ===================== TreeDock ===================== */

TreeDock::TreeDock(QWidget *parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("TreeDock"));
    /* 这几个 dock 都不要标题文字，只留一个浮动按钮的细条 */
    setTitleBarWidget(nullptr);
    setWindowTitle(QString());
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    auto *host = new QWidget(this);
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_tree = new QTreeWidget(host);
    m_tree->setColumnCount(3);
    /* 三列名写成一条串 "结点,属性,ID号" 再拆，
     * 免得三处各写各的以后对不上。 */
    m_tree->setHeaderLabels(QStringLiteral("结点,属性,ID号").split(QLatin1Char(',')));
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setFixedHeight(20);          // 实测表头底线在 y=103，顶 84
    m_tree->setColumnWidth(0, 151);                // 实测 结点/属性/ID号 = 151/70/剩余
    m_tree->setColumnWidth(1, 70);
    m_tree->setIndentation(16);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    lay->addWidget(m_tree, 1);

    m_count = new QLabel(tr("控件数量: 0"), host);
    m_count->setContentsMargins(4, 2, 4, 2);
    lay->addWidget(m_count, 0);

    setWidget(host);

    connect(m_tree, &QTreeWidget::itemPressed, this, &TreeDock::onItemPressed);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &TreeDock::onCustomContextMenu);
}

TreeDock::~TreeDock() = default;

UiNode *TreeDock::nodeOf(QTreeWidgetItem *it) const
{
    return it ? unpackNode(it->data(0, Qt::UserRole)) : nullptr;
}

void TreeDock::addNode(UiNode *n, QTreeWidgetItem *parentItem)
{
    auto *it = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_tree);
    it->setText(0, m_mgr ? m_mgr->model()->displayName(n)
                         : (n->name.isEmpty() ? n->cls : n->name));
    it->setText(1, n->cls);
    it->setText(2, enameOf(n));
    it->setData(0, Qt::UserRole, packNode(n));
    /* 只有容器（有子节点的）才有那只眼睛 */
    if (!n->children.isEmpty()) {
        it->setIcon(0, QIcon(QStringLiteral(":/icon/icons/eye_open@2x.png")));
    }
    it->setExpanded(true);
    for (const auto &c : n->children) {
        addNode(c.second, it);
    }
}

int TreeDock::countAll() const
{
    if (!m_mgr) {
        return 0;
    }
    int n = 0;
    for (UiNode *p : m_mgr->model()->pages()) {
        for (const auto &c : p->children) {
            c.second->forEach([&n](UiNode *) { ++n; return true; });
        }
    }
    return n;
}

void TreeDock::reload()
{
    /* 【高亮要活过 clear()】reload() 的触发点大多没动结构 —— 改一个属性就会
     * 走到这儿。clear() 之后当前项没了，树上的高亮跟着没，用户看到的是
     * "改个参数选中就自己跑了"。先记住节点，重建完按节点找回来。 */
    UiNode *const keep = nodeOf(m_tree->currentItem());
    m_tree->clear();
    if (!m_mgr) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (s && s->page()) {
        /* 树的根就是当前页的图层，页节点本身不显示 */
        for (const auto &c : s->page()->children) {
            addNode(c.second, nullptr);
        }
        m_tree->expandAll();
    }
    if (keep) {
        selectNode(keep);      // 节点已经不在树上就是空操作
    }
    m_count->setText(tr("控件数量: %1").arg(countAll()));
}

UiNode *TreeDock::currentNodeForTest() const
{
    return nodeOf(m_tree->currentItem());
}

void TreeDock::selectNode(UiNode *n)
{
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        if (nodeOf(*it) == n) {
            m_tree->setCurrentItem(*it);
            return;
        }
        ++it;
    }
}

void TreeDock::onItemPressed(QTreeWidgetItem *item, int col)
{
    UiNode *n = nodeOf(item);
    if (!n) {
        return;
    }
    /* 点第 0 列的眼睛图标 = 显示/隐藏 */
    if (col == 0 && !n->children.isEmpty()) {
        const QRect r = m_tree->visualItemRect(item);
        const int iconLeft = r.left() + 2;
        if (m_tree->mapFromGlobal(QCursor::pos()).x() <= iconLeft + 18) {
            onSwapShowHideObject();
        }
    }
    if (m_mgr) {
        if (ScenesScreen *s = m_mgr->currentScreen()) {
            s->selectNode(n);
        }
    }
    emit nodeActivated(n);
}

void TreeDock::onCustomContextMenu(QPoint point)
{
    QTreeWidgetItem *it = m_tree->itemAt(point);
    UiNode *n = nodeOf(it);
    if (!n) {
        return;
    }
    /* 【树和画布用同一个菜单】两边的动作是同一套（删除当前-xxx /
     * 保存成控件 / 显示·隐藏 / 复制 / 粘贴 / 移层 / 查找对像），所以这里
     * 不再自己搭一个只有三项的简版，直接把右键位置交给对应的画布控件。
     * 以前树上只能删，画布上能干的事树上干不了，用起来就是
     * "树上的右键少了一半"。 */
    if (!m_mgr) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (!s) {
        return;
    }
    m_tree->setCurrentItem(it);
    s->selectNode(n);
    emit nodeActivated(n);
    if (BaseForm *f = s->formFor(n)) {
        f->showContextMenu(m_tree->viewport()->mapToGlobal(point));
    }
}

void TreeDock::onSwapShowHideObject()
{
    UiNode *n = nodeOf(m_tree->currentItem());
    if (!n || !m_mgr) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (!s) {
        return;
    }
    /* 【走画布的记账，不要直接 setVisible】画布会因为"选中即隔离"随时重算
     * 可见性，直接翻控件的 visible 位的话，点一下别的节点就被覆盖回去了。 */
    s->toggleUserHidden(n);
    if (QTreeWidgetItem *it = m_tree->currentItem()) {
        it->setIcon(0, QIcon(s->isUserHidden(n)
                             ? QStringLiteral(":/icon/icons/eye-hidden.png")
                             : QStringLiteral(":/icon/icons/eye_open@2x.png")));
    }
}

void TreeDock::onSwapShowHideSubObject()
{
    UiNode *n = nodeOf(m_tree->currentItem());
    if (!n || !m_mgr) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (!s) {
        return;
    }
    for (const auto &c : n->children) {
        s->toggleUserHidden(c.second);
    }
}

/* ===================== PageView ===================== */

/**
 * 页面渲染：把该页**运行时的样子**原尺寸画出来（128x64），不是缩略图。
 *
 * 【以前这里是另一套画法】按层级深浅填绿/蓝/紫的色块，既没有内容，也不是
 * 单色屏该有的样子 —— 画布那边早就改成"黑底白点 + 真内容"了，右栏还停在
 * 老版本，两边对不上。现在两边走同一条规则（Preview 那套单色语义 +
 * Preview::contentOf），右栏看到的就是画布看到的，也是屏上看到的。
 *
 * 【为什么天然只画一个布局】一页里 1~9 个整屏布局是互斥的，**恰好只有一个**
 * invisible=false。这里按"默认隐藏就不画"来走，剩下的自然就是运行时那一个，
 * 不用另外挑。
 */
class PagePreview : public QWidget
{
public:
    /**
     * @param page      取画幅用的节点（页），决定这块画多大
     * @param root      真正要画的子树；nullptr = 画整页
     * @param forceRoot root 自己"默认隐藏"也照画。右列的布局预览要的就是这个 ——
     *                  一页里那 1~9 个互斥布局只有一个不隐藏，不强画就全是空白
     */
    explicit PagePreview(UiNode *page, UiNode *root = nullptr,
                         bool forceRoot = false, QWidget *parent = nullptr)
        : QWidget(parent), m_page(page), m_root(root ? root : page),
          m_forceRoot(forceRoot && root && root != page)
    {
        const QSize s = page && page->rect.isValid() ? page->rect.size() : QSize(128, 64);
        setFixedSize(s.width() + 8, s.height() + 8);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const QRect body = rect().adjusted(4, 4, -4, -4);
        /* 灭的时候是什么颜色由[全局设置]配 —— 和画布同一个底色 */
        p.fillRect(body, Preview::monoDark());
        if (m_root) {
            p.save();
            p.translate(body.topLeft());
            p.setClipRect(QRect(QPoint(0, 0), body.size()));
            if (m_forceRoot) {
                /* 画某一个布局：先把它自己那一层画出来（背景/边框），
                 * 再画子树。位置要从页往下累加，不能直接用它自己的 rect ——
                 * 中间还隔着图层（本工程的图层恒等于整页，但别假设）。 */
                const QPoint at = originOf(m_root);
                const QRect box(at, m_root->rect.isValid() ? m_root->rect.size()
                                                           : body.size());
                drawOne(p, m_root, box);
                drawNode(p, m_root, box.topLeft());
            } else {
                drawNode(p, m_root);
            }
            p.restore();
        }
        /* 8 个角点，作为选中手柄。
         * 【不能再用黑色】页底已经是黑的了，黑手柄等于没画。
         * 【也不能压在页面上】以前手柄是骑在边界上的（各盖进去 2px），现在
         * 页面里画的是真内容，盖掉的就是真像素了 —— 挪到 4px 留白里去，
         * 页面区域保持"只有黑和白"。 */
        p.setBrush(QColor(0x2b, 0x7d, 0xd1));
        p.setPen(Qt::NoPen);
        const int hs = 4;                       // 手柄边长 = 留白宽度
        const int cx = body.center().x() - hs / 2;
        const int cy = body.center().y() - hs / 2;
        const int l = body.left() - hs, r = body.right() + 1;
        const int t = body.top() - hs, b = body.bottom() + 1;
        const QRect handles[8] = {
            QRect(l, t, hs, hs), QRect(cx, t, hs, hs), QRect(r, t, hs, hs),
            QRect(l, cy, hs, hs),                      QRect(r, cy, hs, hs),
            QRect(l, b, hs, hs), QRect(cx, b, hs, hs), QRect(r, b, hs, hs)
        };
        for (const QRect &h : handles) {
            p.drawRect(h);
        }
    }

private:
    /** n 相对页面左上角的位置（一路把祖先的 rect 左上角累加上去，页本身不算）。 */
    QPoint originOf(UiNode *n) const
    {
        QPoint at(0, 0);
        for (UiNode *a = n; a && a != m_page; a = a->parent) {
            if (a->rect.isValid()) {
                at += a->rect.topLeft();
            }
        }
        return at;
    }

    /** 控件的水平对齐（element_css 的 align），和 BaseForm::contentAlign 同源。 */
    static Qt::Alignment alignOf(UiNode *n)
    {
        const QString a = n->cssField(0, QStringLiteral("align"),
                                      QStringLiteral("default")).toString();
        if (a == QLatin1String("ALIGN_RIGHT")) {
            return Qt::AlignRight;
        }
        if (a == QLatin1String("ALIGN_CENTER")) {
            return Qt::AlignHCenter;
        }
        return Qt::AlignLeft;
    }

    /** 画一个控件自己那一层（背景/内容/边框），规则抄 BaseForm::paintEvent。 */
    static void drawOne(QPainter &p, UiNode *n, const QRect &box)
    {
        const QString bgCss = n->cssField(0, QStringLiteral("background_color"),
                                          QStringLiteral("background-color")).toString();
        QString txtCss;
        for (const UiProperty &pr : n->props) {
            if (pr.caption == QStringLiteral("文字颜色")) {
                txtCss = pr.raw.value(QStringLiteral("color")).toString();
                if (txtCss.isEmpty()) {
                    txtCss = pr.raw.value(QStringLiteral("background-color")).toString();
                }
                break;
            }
        }
        const Preview::MonoText tm = Preview::textModeOf(txtCss);

        /* 底：只有魔数 0x555AAA 才填充；反显也要先把整块点亮 */
        if (Preview::fillOf(bgCss) == Preview::MonoFill::Set
            || tm == Preview::MonoText::Invert) {
            p.fillRect(box, Preview::monoLit());
        }

        /* 背景图片：和画布同一套判定（见 Forms.cpp） */
        {
            const QString bgi = n->cssField(0, QStringLiteral("background_image"),
                                            QStringLiteral("background-image")).toString();
            const QPixmap bg = Preview::pictureOf(bgi, Preview::monoLit());
            if (!bg.isNull()) {
                p.drawPixmap(box.topLeft(), bg);
            }
        }

        if (tm != Preview::MonoText::Hidden) {
            const QPixmap content = Preview::contentOf(
                n, tm == Preview::MonoText::Invert ? Preview::monoDark()
                                                   : Preview::monoLit());
            if (!content.isNull()) {
                /* 原尺寸摆放（右栏就是 1:1），超出部分裁掉 —— 和固件一致 */
                int x = box.left();
                switch (alignOf(n)) {
                case Qt::AlignRight:   x = box.right() - content.width() + 1;      break;
                case Qt::AlignHCenter: x = box.left()
                                           + (box.width() - content.width()) / 2; break;
                default:               break;
                }
                const int y = box.top() + (box.height() - content.height()) / 2;
                p.save();
                p.setClipRect(box);
                p.drawPixmap(x, y, content);
                p.restore();
            }
        }

        /* 内边框线：颜色只决定画不画（判断方向和背景相反） */
        const QJsonObject b = n->cssField(0, QStringLiteral("border"),
                                          QStringLiteral("border")).toObject();
        if (b.isEmpty()
            || !Preview::borderVisible(n->cssField(0, QStringLiteral("border"),
                                                   QStringLiteral("color")).toString())) {
            return;
        }
        const int l = b.value(QStringLiteral("left")).toInt();
        const int t = b.value(QStringLiteral("top")).toInt();
        const int r = b.value(QStringLiteral("right")).toInt();
        const int bo = b.value(QStringLiteral("bottom")).toInt();
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        if (l > 0) {
            p.drawRect(QRect(box.left(), box.top(), l, box.height()));
        }
        if (r > 0) {
            p.drawRect(QRect(box.right() - r + 1, box.top(), r, box.height()));
        }
        if (t > 0) {
            p.drawRect(QRect(box.left(), box.top(), box.width(), t));
        }
        if (bo > 0) {
            p.drawRect(QRect(box.left(), box.bottom() - bo + 1, box.width(), bo));
        }
        p.restore();
    }

    /**
     * 递归画整棵子树。
     * @param origin 父控件左上角在本页里的绝对坐标 —— 工程里的 rect 是相对
     *               父级的（画布上靠 QWidget 嵌套自动完成，这里得自己累加）。
     */
    /** 列表控件？行的位置不由自己的 rect 决定，得单独摊开。
     *  【认 -class 不认 -type】json 里 -type 是 VerticalList / HorizontalList，
     *  但画布是按 -class 建控件的（createFormForClass），两边要一致。 */
    static bool isList(UiNode *n)
    {
        return n->cls == QLatin1String("NewList");
    }

    void drawNode(QPainter &p, UiNode *n, const QPoint &origin = QPoint())
    {
        /* 【列表的行要自己摊开】工程里列表各行的 rect **全都一样**（都在
         * (0,0)），真正决定行距的是列表节点上那个独立的 sizehw / space 字段
         * —— 画布靠 NewList::relayoutRows() 摊开，右栏以前没做这一步，
         * 于是"系统/语言/关机"几行字全叠在同一个位置上。 */
        if (isList(n)) {
            const bool vert = n->extraValue(QStringLiteral("orientation")).toString()
                              != QLatin1String("Horizontal");
            const int size = qMax(1, n->extraValue(QStringLiteral("sizehw")).toInt(16));
            const int step = size + n->extraValue(QStringLiteral("space")).toInt(0);
            int i = 0;
            for (const auto &c : n->children) {
                UiNode *k = c.second;
                if (k->isDefaultHidden()) {
                    ++i;                       // 隐藏的行照样占位
                    continue;
                }
                const int off = i * step;
                const QRect box = vert
                    ? QRect(origin.x() + k->rect.x(), origin.y() + off,
                            k->rect.isValid() ? k->rect.width() : 1, size)
                    : QRect(origin.x() + off, origin.y() + k->rect.y(),
                            size, k->rect.isValid() ? k->rect.height() : 1);
                drawOne(p, k, box);
                drawNode(p, k, box.topLeft());
                ++i;
            }
            return;
        }

        for (const auto &c : n->children) {
            UiNode *k = c.second;
            /* 默认隐藏的不画 —— 一页里那 1~9 个互斥布局就是靠这条筛掉的 */
            if (k->isDefaultHidden()) {
                continue;
            }
            if (!k->rect.isValid()) {
                drawNode(p, k, origin);
                continue;
            }
            const QRect box(origin + k->rect.topLeft(), k->rect.size());
            drawOne(p, k, box);
            if (isList(k)) {
                /* 【行要被列表框裁掉】画布上行是列表 QWidget 的子控件，超出
                 * 列表矩形的部分 Qt 自动裁掉（这套工程的列表就是"5 行塞进
                 * 48px、只露 3 行"）。右栏是自己画的，得手动裁，否则多出来的
                 * 行会糊到列表下面去。 */
                p.save();
                p.setClipRect(box, Qt::IntersectClip);
                drawNode(p, k, box.topLeft());
                p.restore();
            } else {
                drawNode(p, k, box.topLeft());
            }
        }
    }
    UiNode *m_page = nullptr;
    UiNode *m_root = nullptr;
    bool    m_forceRoot = false;
};


/* ===================== LayoutBrace ===================== */

/**
 * 夹在"页面"和"当前页布局"两列中间的大括号。
 *
 * 右列选中哪一页，左列列的就是那一页的布局 —— 两列之间这层从属关系光靠挨着
 * 摆是看不出来的（尤其是右列滚动之后，当前页可能已经滚到别处）。这里画一个
 * `}`：两条臂包住整列布局，尖端拉一条线指到右列当前那一页。
 *
 * 几何全是**现算**的，不缓存：
 *   臂的上下沿 = 布局列第一项和最后一项的可视矩形（滚出去的部分按视口裁）
 *   尖端的 y   = 页面列当前项的中心（滚出视口就贴着边，并把箭头去掉）
 * 所以两列任意一个滚动、换页、换选中，只要 update() 一下就自洽。
 */
class LayoutBrace : public QWidget
{
public:
    LayoutBrace(QListWidget *pages, QListWidget *layouts, QWidget *parent = nullptr)
        : QWidget(parent), m_pages(pages), m_layouts(layouts)
    {
        setFixedWidth(kWidth);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    static const int kWidth = 30;

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_pages || !m_layouts || m_layouts->count() == 0) {
            return;
        }
        /* ---- 右列：臂要包住的上下沿 ---- */
        const QRect first = m_layouts->visualItemRect(m_layouts->item(0));
        const QRect last = m_layouts->visualItemRect(m_layouts->item(m_layouts->count() - 1));
        const QRect vp = m_layouts->viewport()->rect();
        int top = qMax(first.top(), vp.top());
        int bottom = qMin(last.bottom(), vp.bottom());
        if (bottom - top < 8) {
            return;                              // 一项都没露出来，不画
        }
        top = mapFromList(m_layouts, top);
        bottom = mapFromList(m_layouts, bottom);

        /* ---- 右列：当前页在哪一行 ---- */
        bool pageVisible = false;
        int pageY = (top + bottom) / 2;
        if (QListWidgetItem *cur = m_pages->currentItem()) {
            const QRect r = m_pages->visualItemRect(cur);
            const QRect pvp = m_pages->viewport()->rect();
            /* 当前页滚出视口了：还画，但整个括号转灰 —— 这时指不到真东西上 */
            pageVisible = r.intersects(pvp);
            pageY = mapFromList(m_pages, qBound(pvp.top(), r.center().y(), pvp.bottom()));
        }
        pageY = qBound(0, pageY, height() - 1);
        /* 【跨度 = 布局范围 ∪ 当前页那一行】
         * 尖端必须落在跨度里面，否则括号形状就歪了。布局少、当前页又靠下时
         * （比如只有 1 个布局、停在第 4 页），页面那一行远在布局范围之外 ——
         * 这时**让括号自己伸长**去括住中间那段空位，比"括号只抱布局、再引一条
         * 折线过去"好看得多（折线那版试过，很碎）。
         * kMinHalf 是尖端两侧各自的最短半长。取 48 而不是刚够画勾的十几像素：
         * 伸长那一侧如果只留一点点，括号看着一头长一头秃，很跛。不追求和长的
         * 那一半对称（那样会在空白里拉出很大一片），够看就行。 */
        const int kMinHalf = 48;
        top = qMin(top, pageY - kMinHalf);
        bottom = qMax(bottom, pageY + kMinHalf);
        top = qMax(0, top);
        bottom = qMin(height() - 1, bottom);
        if (bottom - top < 8) {
            return;
        }
        const int tip = qBound(top, pageY, bottom);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        /* 当前页滚出视口时改成灰的 —— 尖端这时指不到真东西上，
         * 别拿高亮色误导人。 */
        QPen pen(pageVisible ? QColor(0x2b, 0x7d, 0xd1) : QColor(0xaa, 0xaa, 0xaa));
        pen.setWidthF(1.6);
        /* 尖端是两段曲线以锐角交汇，圆角接头会把尖磨平 */
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        /* 大括号：臂在左（贴着布局那一列），尖端在右（指着页面那一列）。
         *
         *   xa ──┐                     臂：贴住布局列，勾住上下两端
         *        │
         *   xw   │──┐                  腰：竖直的主干
         *           └─ xp  ►           尖：从腰上拐出去指向页面列
         *
         * 四个拐角都用二次贝塞尔，控制点放在"直角"上，出来就是标准的大括号。
         * 勾的长度取上下半段的较小值，列表很短时才不会两个勾叠在一起。 */
        const qreal xa = 2.0;                    // 臂
        const qreal xw = 11.0;                   // 腰
        const qreal xp = width() - 2.0;          // 尖，一直顶到页面那一列
        const qreal hook = qBound(2.0,
                                  qMin(qreal(tip - top), qreal(bottom - tip)) * 0.5,
                                  10.0);
        /* 【尖端为什么这么画】控制点放在 (xw, tip ∓ hook*0.15) 而不是 (xw, tip)：
         * 放在 tip 上时曲线是水平切入尖端的，上下两段切线共线，出来是个钝头；
         * 往回收一点，切线变成斜的（上段从左上来、下段往左下去），
         * 两条斜切线在尖端交成锐角 —— 这才是数学大括号那个尖。 */
        const qreal ctl = hook * 0.15;
        QPainterPath path;
        path.moveTo(xa, top);
        path.quadTo(xw, top, xw, top + hook);            // 上勾
        path.lineTo(xw, tip - hook);                     // 上主干
        path.quadTo(xw, tip - ctl, xp, tip);             // 收成尖
        path.quadTo(xw, tip + ctl, xw, tip + hook);      // 从尖出来
        path.lineTo(xw, bottom - hook);                  // 下主干
        path.quadTo(xw, bottom, xa, bottom);             // 下勾
        p.drawPath(path);

    }

private:
    /**
     * 把某一列视口里的 y 换算到本控件的坐标系。
     *
     * 【必须绕全局坐标】QWidget::mapTo(target, ...) 只认**祖先**；
     * 这两列和大括号是兄弟，直接 mapTo 会踩空指针崩掉（实测 0xC0000005）。
     */
    int mapFromList(QListWidget *w, int y) const
    {
        const QPoint g = w->viewport()->mapToGlobal(QPoint(0, y));
        return const_cast<LayoutBrace *>(this)->mapFromGlobal(g).y();
    }

    QListWidget *m_pages = nullptr;
    QListWidget *m_layouts = nullptr;
};

PageView::PageView(QWidget *parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("PageView"));
    setWindowTitle(QString());
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    auto mkList = [this]() {
        auto *w = new QListWidget(this);
        w->setSpacing(4);
        w->setSelectionMode(QAbstractItemView::SingleSelection);
        w->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        /* 去掉边框，两列并排时能省下十来个像素 */
        w->setFrameShape(QFrame::NoFrame);
        return w;
    };
    m_list = mkList();
    m_layouts = mkList();

    /* 左列页面、右列当前页的布局，并排。中间那条 QSplitter 让人能自己调
     * 两列的宽度 —— 页面名字长的时候左列要宽一点。 */
    auto mkCol = [](const QString &title, QListWidget *w) {
        auto *box = new QWidget;
        auto *v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto *cap = new QLabel(title, box);
        cap->setAlignment(Qt::AlignHCenter);
        cap->setStyleSheet(QStringLiteral("color:#555;"));
        v->addWidget(cap);
        v->addWidget(w, 1);
        return box;
    };
    /* 两列中间夹一个大括号，把整列布局括起来指向左列当前那一页。
     * 【为什么不用 QSplitter 了】两个预览都是定宽 128，本来也没什么可拖的；
     * 中间要塞画笔画的东西，固定布局反而好算坐标。 */
    auto *box = new QWidget(this);
    auto *h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(mkCol(tr("当前页布局"), m_layouts), 1);

    auto *braceCol = new QWidget(box);
    auto *bv = new QVBoxLayout(braceCol);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(2);
    /* 上面留一格和两列的标题对齐，大括号才不会顶到标题上去 */
    auto *spacer = new QLabel(braceCol);
    spacer->setAlignment(Qt::AlignHCenter);
    bv->addWidget(spacer);
    m_brace = new LayoutBrace(m_list, m_layouts, braceCol);
    bv->addWidget(m_brace, 1);
    braceCol->setFixedWidth(LayoutBrace::kWidth);
    h->addWidget(braceCol, 0);

    h->addWidget(mkCol(tr("页面"), m_list), 1);
    setWidget(box);

    connect(m_list, &QListWidget::itemClicked, this, &PageView::onClickedItem);
    connect(m_list, &QListWidget::itemChanged, this, &PageView::onItemChanged);
    connect(m_layouts, &QListWidget::itemClicked, this, &PageView::onLayoutClicked);
    /* 哪一列滚了大括号都要重画 —— 它的两端跟着两列的可视范围走 */
    for (QListWidget *w : { m_list, m_layouts }) {
        connect(w->verticalScrollBar(), &QScrollBar::valueChanged,
                m_brace, QOverload<>::of(&QWidget::update));
    }
}

PageView::~PageView() = default;

QImage PageView::grabPageForTest(int i) const
{
    if (!m_mgr || i < 0 || i >= m_mgr->model()->pages().size()) {
        return QImage();
    }
    UiNode *page = m_mgr->model()->pages().at(i);
    const QSize s = page->rect.isValid() ? page->rect.size() : QSize(128, 64);
    /* 只要页面内容那块，不要外面那圈边距和选中手柄 —— 拿去和画布比像素的。 */
    PagePreview pv(page);
    const QPixmap whole = pv.grab();
    return whole.copy(QRect(QPoint(4, 4), s)).toImage()
           .convertToFormat(QImage::Format_RGB32);
}

void PageView::reload()
{
    if (!m_mgr) {
        return;
    }
    m_loading = true;
    m_list->clear();
    const QVector<UiNode *> &pages = m_mgr->model()->pages();
    for (int i = 0; i < pages.size(); ++i) {
        /* 每一项 = 页面渲染 + 下方标题，标题可双击改名（onItemChanged） */
        auto *cell = new QWidget;
        auto *lay = new QVBoxLayout(cell);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(2);
        lay->addWidget(new PagePreview(pages.at(i)), 0, Qt::AlignHCenter);
        auto *cap = new QLabel(pages.at(i)->caption.isEmpty()
                               ? tr("页面_%1").arg(i) : pages.at(i)->caption);
        cap->setAlignment(Qt::AlignHCenter);
        lay->addWidget(cap);

        auto *it = new QListWidgetItem;
        it->setSizeHint(cell->sizeHint());
        it->setData(Qt::UserRole, i);
        m_list->addItem(it);
        m_list->setItemWidget(it, cell);
    }
    m_list->setCurrentRow(m_mgr->currentPage());
    m_loading = false;
    reloadLayouts();
}

/* ===================== 右列：当前页的顶层布局 ===================== */

void PageView::reloadLayouts()
{
    if (!m_mgr || !m_layouts) {
        return;
    }
    ScenesScreen *sc = m_mgr->currentScreen();
    const QVector<UiNode *> screens = sc ? sc->screens() : QVector<UiNode *>();

    /* 集合没变就只挪一下「当前」标记 —— 每选一次控件都重建 9 个预览太浪费 */
    if (screens == m_layoutNodes) {
        markCurrentLayout();
        return;
    }
    m_layoutNodes = screens;

    m_loading = true;
    m_layouts->clear();
    UiNode *page = m_mgr->model()->pages().value(m_mgr->currentPage(), nullptr);
    for (int i = 0; i < screens.size(); ++i) {
        UiNode *lo = screens.at(i);
        auto *cell = new QWidget;
        auto *lay = new QVBoxLayout(cell);
        lay->setContentsMargins(2, 2, 2, 2);
        lay->setSpacing(1);
        /* forceRoot=true：这些布局大多是"默认隐藏"的，不强画就是一片黑 */
        lay->addWidget(new PagePreview(page, lo, true), 0, Qt::AlignHCenter);

        auto *cap = new QLabel(lo->name);
        cap->setAlignment(Qt::AlignHCenter);
        lay->addWidget(cap);
        /* 第二行是状态：谁是运行时那一个、谁是默认隐藏的。
         * 【为什么一定要标出来】画面看着都一样，不标的话很容易把"设备上根本
         * 不会显示"的那几个当成会显示的。 */
        auto *st = new QLabel;
        st->setAlignment(Qt::AlignHCenter);
        st->setObjectName(QStringLiteral("layoutState"));
        lay->addWidget(st);

        auto *it = new QListWidgetItem;
        it->setSizeHint(cell->sizeHint());
        it->setData(Qt::UserRole, i);
        m_layouts->addItem(it);
        m_layouts->setItemWidget(it, cell);
    }
    m_loading = false;
    markCurrentLayout();
}

void PageView::markCurrentLayout()
{
    if (!m_mgr || !m_layouts) {
        return;
    }
    ScenesScreen *sc = m_mgr->currentScreen();
    const int cur = sc ? sc->currentScreenIndex() : -1;
    m_loading = true;
    for (int i = 0; i < m_layouts->count() && i < m_layoutNodes.size(); ++i) {
        QWidget *cell = m_layouts->itemWidget(m_layouts->item(i));
        auto *st = cell ? cell->findChild<QLabel *>(QStringLiteral("layoutState")) : nullptr;
        if (!st) {
            continue;
        }
        /* 【两件事，分开说】
         *   会不会显示：只看"默认隐藏"（Canvas::applyVisibility 规则 1 就这一条），
         *               没隐藏的**全都**会显示，会互相叠在一起；
         *   是不是当前：currentScreenIndex()，纯编辑器概念（工具栏下拉框指向谁），
         *               和设备上显不显示没关系。
         * 早先把这两件事挤成一条三选一，"不是当前、又没隐藏"的就落进空白，
         * 看的人只能猜。现在每一项都有话说。 */
        const bool hidden = m_layoutNodes.at(i)->isDefaultHidden();
        QStringList parts;
        if (i == cur) {
            parts << tr("当前");
        }
        parts << (hidden ? tr("默认隐藏") : tr("会显示"));
        st->setText(parts.join(QStringLiteral(" · ")));
        if (i == cur) {
            st->setStyleSheet(QStringLiteral("color:#2b7dd1;font-weight:bold;"));
        } else if (hidden) {
            st->setStyleSheet(QStringLiteral("color:#999;"));
        } else {
            st->setStyleSheet(QStringLiteral("color:#333;"));
        }
        st->setToolTip(hidden
            ? tr("这个布局设了「默认隐藏」，设备上不会显示。\n"
                 "在这一列点它可以看它长什么样，不会改工程。")
            : tr("这个布局没设「默认隐藏」，设备上会显示。\n"
                 "同一页里没隐藏的布局会**同时**显示、叠在一起。"));
    }
    if (cur >= 0 && cur < m_layouts->count()) {
        m_layouts->setCurrentRow(cur);
    }
    m_loading = false;
    if (m_brace) {
        m_brace->update();
    }
}

QImage PageView::grabLayoutForTest(int i) const
{
    if (!m_mgr || i < 0 || i >= m_layoutNodes.size()) {
        return QImage();
    }
    UiNode *page = m_mgr->model()->pages().value(m_mgr->currentPage(), nullptr);
    if (!page) {
        return QImage();
    }
    const QSize s = page->rect.isValid() ? page->rect.size() : QSize(128, 64);
    PagePreview pv(page, m_layoutNodes.at(i), true);
    return pv.grab().copy(QRect(QPoint(4, 4), s)).toImage()
           .convertToFormat(QImage::Format_RGB32);
}

void PageView::onLayoutClicked(QListWidgetItem *a0)
{
    if (!a0 || !m_mgr || m_loading) {
        return;
    }
    const int idx = a0->data(Qt::UserRole).toInt();
    /* 走和工具栏「当前画面」下拉框同一条路 —— 两边永远一致 */
    m_mgr->gotoScreen(idx);
    emit layoutActivated(idx);
}

void PageView::onClickedItem(QListWidgetItem *a0)
{
    if (!a0 || !m_mgr) {
        return;
    }
    const int idx = a0->data(Qt::UserRole).toInt();
    m_mgr->setCurrentPage(idx);
    reloadLayouts();                 // 换页了，右列整列都要换
    emit pageActivated(idx);
}

void PageView::onItemChanged(QListWidgetItem *a0)
{
    if (m_loading || !a0 || !m_mgr) {
        return;
    }
    const int idx = a0->data(Qt::UserRole).toInt();
    QVector<UiNode *> &pages = m_mgr->model()->pages();
    if (idx >= 0 && idx < pages.size() && !a0->text().isEmpty()) {
        pages[idx]->caption = a0->text();
        pages[idx]->markDirty();
        m_mgr->markDirty();
    }
}

/* ===================== CompoentControls ===================== */

CompoentControls::CompoentControls(QWidget *parent)
    : QGroupBox(tr("控件列表"), parent)
{
    setObjectName(QStringLiteral("CompoentControls"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    m_area = new BaseScrollArea(this);
    m_grid = new QWidget;
    m_area->setWidget(m_grid);
    outer->addWidget(m_area);
}

CompoentControls::~CompoentControls() = default;

void CompoentControls::addControlButton(const QString &cls, const QString &type,
                                        const QString &caption, int row, int col)
{
    auto *g = qobject_cast<QGridLayout *>(m_grid->layout());
    auto *b = new DragButton(m_grid);
    b->setText(caption.isEmpty() ? type : caption);
    b->setPayload(cls, type);
    b->setFixedHeight(23);            // 实测网格按钮行距 23
    const QString cap = caption;
    connect(b, &QPushButton::clicked, this, [this, cls, type, cap]() {
        createControl(cls, type, cap);
    });
    g->addWidget(b, row, col);
}

void CompoentControls::reload()
{
    /* 重建整块面板 */
    delete m_grid->layout();
    qDeleteAll(m_grid->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));

    auto *g = new QGridLayout(m_grid);
    g->setContentsMargins(5, 4, 5, 4);
    g->setHorizontalSpacing(2);
    g->setVerticalSpacing(1);

    /* 图层、布局两个通栏大按钮在最上面 */
    auto *layer = new DragButton(m_grid);
    layer->setText(tr("图层"));
    layer->setPayload(QStringLiteral("NewLayer"), QStringLiteral("NewLayer"));
    layer->setFixedHeight(51);        // 实测 y=106..157
    connect(layer, &QPushButton::clicked, this, &CompoentControls::onCreateNewLayer);
    g->addWidget(layer, 0, 0, 1, 2);

    auto *layout = new DragButton(m_grid);
    layout->setText(tr("布局"));
    layout->setPayload(QStringLiteral("NewLayout"), QStringLiteral("NewLayout"));
    layout->setFixedHeight(51);       // 实测 y=159..210
    connect(layout, &QPushButton::clicked, this, &CompoentControls::onCreateNewLayout);
    g->addWidget(layout, 1, 0, 1, 2);

    int row = 2, col = 0;
    QVector<const ControlTemplate *> custom;
    if (m_mgr) {
        for (const ControlTemplate &t : m_mgr->library()->controls()) {
            /* control/ex/ 里的扩展控件（slider / vslider）本身就是组合布局，
             * 它们的 -type 也是 "NewLayout" —— 所以"跳过图层/布局"这条过滤
             * 只能对内置控件生效，否则扩展控件会被一起误杀。 */
            if (t.isExtension) {
                custom.append(&t);
                continue;
            }
            if (t.type == QLatin1String("NewLayer") || t.type == QLatin1String("NewLayout")) {
                continue;
            }
            addControlButton(t.cls, t.type, t.caption, row, col);
            if (++col == 2) {
                col = 0;
                ++row;
            }
        }
    }
    if (col != 0) {
        ++row;
    }

    /* control/ex/ 里的扩展控件单独放进"自定义控件"子组 */
    m_custom = new QGroupBox(tr("自定义控件"), m_grid);
    auto *cg = new QGridLayout(m_custom);
    cg->setContentsMargins(6, 6, 6, 6);
    cg->setSpacing(4);
    int cr = 0, cc = 0;
    for (const ControlTemplate *t : custom) {
        auto *b = new DragButton(m_custom);
        b->setText(t->caption.isEmpty() ? t->type : t->caption);
        b->setPayload(t->cls, t->type);
        b->setFixedHeight(23);
        const QString cls = t->cls, type = t->type, cap = t->caption;
        connect(b, &QPushButton::clicked, this, [this, cls, type, cap]() {
            createControl(cls, type, cap);
        });
        cg->addWidget(b, cr, cc);
        if (++cc == 2) {
            cc = 0;
            ++cr;
        }
    }
    g->addWidget(m_custom, row, 0, 1, 2);
    g->setRowStretch(row + 1, 1);
}

/**
 * 新建时的默认名字序号。
 *
 * 【是跨页全局的，不是每页各数各的】SmallColorTFT.json 里页0 有 49 个
 * 非页节点（图层_0 … 文字_48），页1 的第一个节点就叫 图层_49 —— 计数器跨页
 * 连着走，而且不算页节点自己。我一开始按页数，第二页新建出来的东西会和第一页
 * 重名。真正的实现在 ProjectModel::nextNodeSeq()。
 */

UiNode *CompoentControls::appendChild(UiNode *parent, const QString &cls,
                                      const QString &type, const QString &caption,
                                      const QString &name, const QPoint &pos)
{
    if (!parent || !m_mgr) {
        return nullptr;
    }

    /* 【必须从模板整份克隆，不能手搭一个空壳】
     * 控件的几何、样式、事件全在 property[-name=="element_css"] 里，模板
     * 里那一整套 struct/enum/min/max 是属性面板和下游 QtToolBin 共同依赖的。
     * 之前这里是现搭一个只有 id + rect 两条属性的节点，结果新建出来的控件
     * 在属性面板上只有 ID 一项，生成 .sty 时也没有坐标。
     * 对照过既有工程文件：节点的键集合 = control.json 模板的键集合
     * （连那个空的 "widget": [] 都留着），只多出装孩子用的 layout/listwidget。
     * 所以直接把模板 json 塞进 fromJsonObject 就是最保真的做法。 */
    const ControlTemplate *t = m_mgr->library()->byType(type);
    UiNode *n = nullptr;
    if (t && !t->raw.isEmpty()) {
        n = ProjectModel::fromJsonObject(t->raw, parent);
    } else {
        QJsonObject o;
        o.insert(QStringLiteral("-class"), cls);
        o.insert(QStringLiteral("-type"),  type);
        o.insert(QStringLiteral("-name"),  name);
        n = ProjectModel::fromJsonObject(o, parent);
    }
    n->forEach([](UiNode *x) { x->markDirty(); return true; });
    n->cls  = cls;
    n->type = type;
    n->name = EditorOps::uniqueName(parent, name);
    if (!caption.isEmpty()) {
        n->caption = caption;
    }
    /* 【进列表/表格的，用格子的尺寸，不要通用默认值】列表的行就是一格：
     * 宽高由 sizehw / 列表自身尺寸决定（见 EditorOps::cellRectFor）。
     * 用通用默认值的话，行比格子大得多，容器一裁，里头放的图片只剩一条边 ——
     * 用户看到的就是"往列表里放了张图，预览里什么都没有"。 */
    const QRect cell = EditorOps::cellRectFor(parent, parent->children.size());
    if (cell.isValid()) {
        n->rect = cell;
    } else if (!n->rect.isValid()) {
        n->rect = QRect(0, 0, 32, 16);
    }
    /* 拖进来的落在鼠标松手的地方；点击建的用模板的默认位置。
     * 负坐标夹回 0：控件的 rect 是相对父级的，拖到容器左上角外面会算出负值，
     * 存进 json 后固件按无符号读会得到一个巨大的坐标。
     * 【格子里的那一项不跟鼠标走】它的位置由第几格决定，拖到哪儿都一样。 */
    if (!cell.isValid() && pos.x() >= 0 && pos.y() >= 0) {
        n->rect.moveTo(qMax(0, pos.x()), qMax(0, pos.y()));
    }

    /* 【默认尺寸不许超过父容器】control.json 的模板默认值是给大屏留的
     * （图片 75x75、布局 100x100、文字 75x25），128x64 的屏上本来就偏大；
     * 放进列表的行（比如水平列表一格才 30x25）更是整个溢出，容器一裁，
     * 里头的图连边都露不出来 —— 用户看到的就是"放了张图，预览里什么都没有"。
     *
     * 夹到父容器尺寸，和属性面板那条限制是同一条：宽/高的取值范围就是
     * 0..父容器宽/高（见 docs/UI_BEHAVIOR.md §10）。
     * 也就是说 75x75 这种值属性面板上根本填不进去。 */
    if (parent->rect.isValid() && parent->rect.width() > 0
        && parent->rect.height() > 0) {
        const int w = qMin(n->rect.width(), parent->rect.width());
        const int h = qMin(n->rect.height(), parent->rect.height());
        n->rect.setWidth(qMax(1, w));
        n->rect.setHeight(qMax(1, h));
        /* 【只夹尺寸，不动位置】位置是用户松手的地方，挪走等于不听话；
         * 拖放这条路不钳坐标（ops-test 第 14 条盯着这件事）。 */
    }
    n->setRectOf(0, n->rect);

    /* 【新建就得带一个唯一的 ID 号】建出来的控件"唯一ID号"这一栏不能是空的。
     * 空着的话属性面板会提示 Ename is empty，生成资源时这个控件也拿不到
     * ename.h 里的宏 —— 业务代码根本引用不到它。
     * 取名规则见 ProjectModel::uniqueEname()。模板自带 ename 的（自定义控件）
     * 不覆盖，只补空的。 */
    if (UiProperty *idp = n->findProp(QStringLiteral("id"))) {
        if (idp->ename.isEmpty()) {
            idp->ename = m_mgr->model()->uniqueEname();
            idp->dirty = true;
            n->markDirty();
        }
    }

    parent->children.append(qMakePair(EditorOps::childKeyFor(parent), n));
    parent->markDirty();
    m_mgr->markDirty();
    ScenesScreen *s = m_mgr->currentScreen();
    if (s) {
        s->rebuild();
    }
    emit nodeCreated(n);       // 先让树/页面栏认得这个新节点
    /* 【新建出来就选中它】这里还兼着一件正事：
     * 画布的"选中即隔离"要有个目标才成立。什么都没选的时候往一个布局里
     * 拖控件，这一页的布局会全部画出来叠成一团，刚拖进去的那个反而看不见。
     * 放在 nodeCreated 之后：那条信号会重载控件树，得等树上有了这一项，
     * selectNode 才点得亮它。 */
    if (s) {
        s->selectNode(n);
    }
    return n;
}

void CompoentControls::createControl(const QString &cls, const QString &type,
                                     const QString &caption, UiNode *parent,
                                     const QPoint &pos)
{
    if (!m_mgr) {
        return;
    }
    /* 两条路进到这里：
     *   拖 —— parent 由落点决定，画布那边已经判过能不能落，这里不再拦；
     *   点 —— parent 为空，用当前选中的节点，按限制判一道。
     * ★ 限制：点击建控件时必须先选中一个**布局**。之前这里是"自己往下
     * 找第一个布局"，看着方便，其实是错的：先点布局再点控件，
     * 会莫名其妙建到别的布局里去。 */
    UiNode *host = parent;
    if (!host) {
        host = EditorOps::hostForNewControl(m_current);
        if (!host) {
            EditorOps::tip(this, QStringLiteral("请选择一个布局或者新建一个并选中它."));
            return;
        }
    }
    const ControlTemplate *t = m_mgr->library()->byType(type);
    const QString cap = (t && !t->caption.isEmpty()) ? t->caption : caption;
    ScenesScreen *sc = m_mgr->currentScreen();
    const QString def = QStringLiteral("%1_%2").arg(cap.isEmpty() ? type : cap)
                        .arg(m_mgr->model()->nextNodeSeq());

    QString name = def;
    if (!EditorOps::silent()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("提示"),
                                     QStringLiteral("请输入控件名称"),
                                     QLineEdit::Normal, def, &ok);
        if (!ok || name.isEmpty()) {
            return;
        }
    }
    appendChild(host, t ? t->cls : cls, type, cap, name, pos);
}

void CompoentControls::onCreateCompoentToCanvas()
{
    Q_UNUSED(this)   // 入口保留，实际走 createControl()
}

void CompoentControls::onCreateCustomWidget()
{
    Q_UNUSED(this)
}

/**
 * 拖放落地的总入口。cls 决定走哪一条：图层 / 布局 / 普通控件。
 * 落点合法性画布那边已经判过（dropTargetFor），这里只管建。
 */
void CompoentControls::createDropped(UiNode *parent, const QString &cls,
                                     const QString &type, const QPoint &pos)
{
    if (!m_mgr || !parent) {
        return;
    }
    ScenesScreen *sc = m_mgr->currentScreen();
    const int seq = m_mgr->model()->nextNodeSeq();
    if (cls == QLatin1String("NewLayer")) {
        appendChild(parent, cls, QStringLiteral("NewLayer"), QStringLiteral("图层"),
                    QStringLiteral("图层_%1").arg(seq), pos);
        return;
    }
    if (cls == QLatin1String("NewLayout")
        && type == QLatin1String("NewLayout")) {
        /* 注意：control/ex 里的 slider / vslider 的 -class 也是 NewLayout，
         * 但 -type 不是，所以要连 type 一起判，否则拖 slider 会变成拖布局。 */
        appendChild(parent, cls, type, QStringLiteral("布局"),
                    QStringLiteral("布局_%1").arg(seq), pos);
        return;
    }
    const ControlTemplate *t = m_mgr->library()->byType(type);
    createControl(t ? t->cls : cls, type,
                  t ? t->caption : QString(), parent, pos);
}

void CompoentControls::onCreateNewLayout()
{
    if (!m_mgr) {
        return;
    }
    /* ★ 规则见 EditorOps::hostForNewLayout。
     * 以前这里写死"只能挂图层下"，太严：选中布局该**套一层布局**，
     * 选中控件/列表该加到它的父级。 */
    bool needTip = false;
    UiNode *host = EditorOps::hostForNewLayout(m_current, &needTip);
    if (!host) {
        if (needTip) {
            EditorOps::tip(this,
                           QStringLiteral("请选择一个图层或者新建一个图层,并选中它."));
        }
        return;
    }
    appendChild(host, QStringLiteral("NewLayout"), QStringLiteral("NewLayout"),
                QStringLiteral("布局"),
                QStringLiteral("布局_%1").arg(m_mgr->model()->nextNodeSeq()));
}

void CompoentControls::onCreateNewLayer()
{
    if (!m_mgr) {
        return;
    }
    ScenesScreen *sc = m_mgr->currentScreen();
    if (!sc || !sc->page()) {
        return;
    }
    /* 图层直接挂在页上，没有额外限制 */
    appendChild(sc->page(), QStringLiteral("NewLayer"), QStringLiteral("NewLayer"),
                QStringLiteral("图层"),
                QStringLiteral("图层_%1").arg(m_mgr->model()->nextNodeSeq()));
}

/* ===================== PropertyTab ===================== */

PropertyTab::PropertyTab(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PropertyTab"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(3);

    /* ---- 常驻的 CSS 状态选择器 ----
     * 背景/边框在资源页、坐标在基础页，两页都跟着状态变，
     * 所以它必须待在两页都看得见的地方。 */
    m_stateRow = new QWidget(this);
    auto *rowLay = new QHBoxLayout(m_stateRow);
    rowLay->setContentsMargins(6, 0, 6, 0);
    rowLay->setSpacing(4);
    auto *stateLb = new QLabel(QStringLiteral("CSS状态:"), m_stateRow);
    rowLay->addWidget(stateLb, 0);
    m_stateCb = new QComboBox(m_stateRow);
    m_stateCb->setObjectName(QStringLiteral("CssStateCombo"));
    rowLay->addWidget(m_stateCb, 1);
    lay->addWidget(m_stateRow, 0);

    /* 【增删 CSS 状态的四个动作】手册 2.10 说的是"右键点击菜单项的
     * CSS 属性_0，选择复制添加"。这里页签是另一个含义，所以这套菜单挪到
     * 状态那一行的右键上。
     *
     * 【整行都要能右键，而且下拉框不能置灰】第一版只挂了下拉框，还顺手写了
     * `setEnabled(want > 1)` —— 只有一个状态时下拉框是灰的，而**禁用的控件
     * 收不到右键事件**，于是"新建 CSS 属性"整个点不出来了（正是最常见的
     * 场景：新控件只有一个状态，想加第二个）。 */
    for (QWidget *w : { static_cast<QWidget *>(m_stateRow),
                        static_cast<QWidget *>(stateLb),
                        static_cast<QWidget *>(m_stateCb) }) {
        w->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(w, &QWidget::customContextMenuRequested,
                this, &PropertyTab::onStateContextMenu);
    }
    connect(m_stateCb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
                if (i >= 0 && i != m_state) {
                    setState(i);
                }
            });

    /* ---- 两个页签 ---- */
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("PropSectionTab"));
    static const char *const kTitle[SecCount] = { "基础设置", "资源" };
    for (int i = 0; i < SecCount; ++i) {
        auto *page = new QWidget(m_tabs);
        m_pageLay[i] = new QVBoxLayout(page);
        m_pageLay[i]->setContentsMargins(0, 0, 0, 0);
        m_pageLay[i]->setSpacing(3);
        m_css[i] = new CssProperty(page);
        m_css[i]->setSection(PropSection(i));
        connect(m_css[i], &BaseProperty::nodeEdited, this, &PropertyTab::nodeEdited);
        m_pageLay[i]->addWidget(m_css[i], 0);
        m_pageLay[i]->addStretch(1);
        m_tabs->addTab(page, QString::fromUtf8(kTitle[i]));
    }
    lay->addWidget(m_tabs, 1);

    showNode(nullptr);
}

PropertyTab::~PropertyTab() = default;

/* 页签数 = element_css.struct 的长度：一个 CSS 状态一页，
 * 名字就是 "CSS属性_N"。 */
bool PropertyTab::stateMenuReachableForTest() const
{
    if (!m_stateCb || !m_stateRow) {
        return false;
    }
    /* 禁用的控件收不到 contextMenuEvent，等于菜单不存在 */
    return m_stateCb->isEnabled()
           && m_stateCb->contextMenuPolicy() == Qt::CustomContextMenu
           && m_stateRow->contextMenuPolicy() == Qt::CustomContextMenu;
}

int PropertyTab::stateCountForTest() const
{
    return m_stateCb ? m_stateCb->count() : 0;
}

QStringList PropertyTab::rowsForTest(PropSection sec) const
{
    return m_css[sec] ? m_css[sec]->rowsForTest() : QStringList();
}

QStringList PropertyTab::rowsForTest() const
{
    /* 两页合起来。而且 基础 在前、资源 在后，正好还原 json 里
     * align / invisible / flags / rect / 背景两项 / border 的原始次序，
     * 18x 那条"次序要和 json 一致"照样成立。 */
    return rowsForTest(SecBasic) + rowsForTest(SecResource);
}

void PropertyTab::setDynamicSections(QWidget *basic, QWidget *resource)
{
    QWidget *const w[SecCount] = { basic, resource };
    for (int i = 0; i < SecCount; ++i) {
        if (!w[i] || !m_pageLay[i]) {
            continue;
        }
        /* 插在 CssProperty 之后、弹簧之前 */
        m_pageLay[i]->insertWidget(1, w[i], 0);
    }
}

int PropertyTab::stateCount() const
{
    return m_node ? qMax(1, m_node->cssStateCount()) : 1;
}

int PropertyTab::sectionIndex() const
{
    return m_tabs ? m_tabs->currentIndex() : 0;
}

void PropertyTab::setSectionIndex(int i)
{
    if (m_tabs && i >= 0 && i < m_tabs->count()) {
        m_tabs->setCurrentIndex(i);
    }
}

void PropertyTab::setState(int st)
{
    m_state = qBound(0, st, stateCount() - 1);
    if (m_stateCb && m_stateCb->currentIndex() != m_state) {
        QSignalBlocker b(m_stateCb);
        m_stateCb->setCurrentIndex(m_state);
    }
    for (int i = 0; i < SecCount; ++i) {
        if (m_css[i]) {
            m_css[i]->setState(m_state);
            m_css[i]->showNode(m_node);
        }
    }
}

void PropertyTab::showNode(UiNode *n)
{
    m_node = n;
    const int want = stateCount();

    /* 下拉框的条目名：CSS属性_0 / CSS属性_1 … */
    {
        QSignalBlocker b(m_stateCb);
        m_stateCb->clear();
        for (int i = 0; i < want; ++i) {
            m_stateCb->addItem(tr("CSS属性_%1").arg(i));
        }
        /* 只要选中了控件就可用 —— 灰掉的话右键菜单也没了，见构造里的说明。
         * 只有一个状态时下拉框里就一条，本来也不影响。 */
        m_stateCb->setEnabled(n != nullptr);
    }
    /* 【换控件回到第一页、第一个状态】不同控件的参数完全不一样，
     * 停在上一个控件的第二页上会让人以为"这个控件没有基础参数"。 */
    m_state = 0;
    if (m_stateCb) {
        QSignalBlocker b(m_stateCb);
        m_stateCb->setCurrentIndex(0);
    }
    setSectionIndex(SecBasic);
    for (int i = 0; i < SecCount; ++i) {
        if (m_css[i]) {
            m_css[i]->setState(0);
            m_css[i]->showNode(n);
        }
    }
}


/* ---- element_css.struct 的增删改 --------------------------------------
 * 属性区上方有一排小动作：清除 / 复制添加 / 复制插入 / 删除活动项。
 * 一个状态 = struct 数组里的一项，这四个动作就是对这个数组做操作。
 * 之前只把状态画出来了，数组是只读的 —— 想加一个状态只能去手改 json。 */
void PropertyTab::onStateContextMenu(QPoint pos)
{
    if (!m_node) {
        return;
    }
    QMenu menu(this);
    QAction *aClear  = menu.addAction(QStringLiteral("清除"));
    QAction *aAppend = menu.addAction(QStringLiteral("复制添加"));
    QAction *aInsert = menu.addAction(QStringLiteral("复制插入"));
    menu.addSeparator();
    QAction *aRemove = menu.addAction(QStringLiteral("删除活动项"));
    /* 只剩一个状态时"删除活动项"直接置灰，比点了再弹一句"不让删"体面 */
    aRemove->setEnabled(stateCount() > 1);

    /* pos 是发信号那个控件的局部坐标（整行 / 标签 / 下拉框都可能），
     * 统一按发送者换算，不然菜单会弹到别处去。 */
    QWidget *from = qobject_cast<QWidget *>(sender());
    QAction *c = menu.exec((from ? from : m_stateCb)->mapToGlobal(pos));
    if (c == aClear) {
        onClearState();
    } else if (c == aAppend) {
        onCopyAppendState();
    } else if (c == aInsert) {
        onCopyInsertState();
    } else if (c == aRemove) {
        onRemoveState();
    }
}

void PropertyTab::onClearState()
{
    if (!m_node) {
        return;
    }
    const int st = m_state;
    m_node->cssClearState(st);
    showNode(m_node);
    setState(st);
    emit nodeEdited(m_node);
}

void PropertyTab::onCopyAppendState()
{
    if (!m_node) {
        return;
    }
    m_node->cssInsertState(m_node->cssStateCount(), m_node->cssState(m_state));
    showNode(m_node);
    setState(stateCount() - 1);
    emit nodeEdited(m_node);
}

void PropertyTab::onCopyInsertState()
{
    if (!m_node) {
        return;
    }
    const int at = m_state + 1;
    m_node->cssInsertState(at, m_node->cssState(m_state));
    showNode(m_node);
    setState(at);
    emit nodeEdited(m_node);
}

void PropertyTab::onRemoveState()
{
    if (!m_node) {
        return;
    }
    if (!m_node->cssRemoveState(m_state)) {
        /* 只剩一个状态时不让删 —— 删光了控件就没有几何也没有样式了 */
        EditorOps::tip(this, QStringLiteral("至少要保留一个 CSS 状态。"));
        return;
    }
    showNode(m_node);
    emit nodeEdited(m_node);
}
