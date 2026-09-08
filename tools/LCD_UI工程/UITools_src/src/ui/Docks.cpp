#include "Docks.h"
#include "Canvas.h"
#include "Forms.h"
#include "Property.h"
#include "ProjectModel.h"
#include "ControlLibrary.h"
#include "EditorOps.h"

#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QHeaderView>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabBar>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QPainter>
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
    /* 原厂这几个 dock 都没有标题文字，只留一个浮动按钮的细条 */
    setTitleBarWidget(nullptr);
    setWindowTitle(QString());
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    auto *host = new QWidget(this);
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_tree = new QTreeWidget(host);
    m_tree->setColumnCount(3);
    /* 原厂二进制里这三列名是一条串 "结点,属性,ID号" 逗号分隔，
     * 这里照它拆，免得三处各写各的以后对不上。 */
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
    /* 只有容器（有子节点的）才有那只眼睛，和原厂一致 */
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
    m_tree->clear();
    if (!m_mgr) {
        return;
    }
    ScenesScreen *s = m_mgr->currentScreen();
    if (s && s->page()) {
        /* 原厂树的根就是当前页的图层，页节点本身不显示 */
        for (const auto &c : s->page()->children) {
            addNode(c.second, nullptr);
        }
        m_tree->expandAll();
    }
    m_count->setText(tr("控件数量: %1").arg(countAll()));
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
    /* 点第 0 列的眼睛图标 = 显示/隐藏，和原厂一样 */
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
    /* 【树和画布用同一个菜单】原厂两边的动作是同一套（删除当前-xxx /
     * 保存成控件 / 显示·隐藏 / 复制 / 粘贴 / 移层 / 查找对像），所以这里
     * 不再自己搭一个只有三项的简版，直接把右键位置交给对应的画布控件。
     * 以前树上只能删，画布上能干的事树上干不了，用户从原厂换过来就会觉得
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

/** 页面缩略渲染：把该页所有控件按 rect 画成方块，再画一圈选中角点。
 *  原厂右栏就是把每一页原尺寸画出来（128x64），不是缩略图。 */
class PagePreview : public QWidget
{
public:
    explicit PagePreview(UiNode *page, QWidget *parent = nullptr)
        : QWidget(parent), m_page(page)
    {
        const QSize s = page && page->rect.isValid() ? page->rect.size() : QSize(128, 64);
        setFixedSize(s.width() + 8, s.height() + 8);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QRect body = rect().adjusted(4, 4, -4, -4);
        p.fillRect(body, QColor(0xF5, 0xF5, 0xF5));
        if (m_page) {
            p.translate(body.topLeft());
            drawNode(p, m_page, 0);
            p.resetTransform();
        }
        /* 8 个角点，模仿原厂的选中手柄外观 */
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        const QPoint pts[8] = {
            body.topLeft(), QPoint(body.center().x(), body.top()), body.topRight(),
            QPoint(body.left(), body.center().y()), QPoint(body.right(), body.center().y()),
            body.bottomLeft(), QPoint(body.center().x(), body.bottom()), body.bottomRight()
        };
        for (const QPoint &pt : pts) {
            p.drawRect(QRect(pt.x() - 2, pt.y() - 2, 5, 5));
        }
    }

private:
    void drawNode(QPainter &p, UiNode *n, int depth)
    {
        for (const auto &c : n->children) {
            UiNode *k = c.second;
            if (k->rect.isValid()) {
                static const QColor pal[] = {
                    QColor(0xA8, 0xC8, 0xA8), QColor(0x5B, 0x9B, 0xD5),
                    QColor(0xC5, 0xE0, 0xB4), QColor(0xA0, 0x10, 0x80)
                };
                p.fillRect(k->rect, pal[depth % 4]);
            }
            drawNode(p, k, depth + 1);
        }
    }
    UiNode *m_page = nullptr;
};

PageView::PageView(QWidget *parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("PageView"));
    setWindowTitle(QString());
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    m_list = new QListWidget(this);
    m_list->setSpacing(6);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, &PageView::onClickedItem);
    connect(m_list, &QListWidget::itemChanged, this, &PageView::onItemChanged);
}

PageView::~PageView() = default;

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
}

void PageView::onClickedItem(QListWidgetItem *a0)
{
    if (!a0 || !m_mgr) {
        return;
    }
    const int idx = a0->data(Qt::UserRole).toInt();
    m_mgr->setCurrentPage(idx);
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
        m_mgr->model()->setDirty(true);
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

    /* 原厂：图层、布局两个通栏大按钮在最上面 */
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

    /* 原厂把 control/ex/ 里的扩展控件单独放进"自定义控件"子组 */
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
 * 【是跨页全局的，不是每页各数各的】原厂 SmallColorTFT.json 里页0 有 49 个
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
     * 对照过原厂工程文件：节点的键集合 = control.json 模板的键集合
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
    if (!n->rect.isValid()) {
        n->rect = QRect(0, 0, 32, 16);
    }
    /* 拖进来的落在鼠标松手的地方；点击建的用模板的默认位置。
     * 负坐标夹回 0：控件的 rect 是相对父级的，拖到容器左上角外面会算出负值，
     * 存进 json 后固件按无符号读会得到一个巨大的坐标。 */
    if (pos.x() >= 0 && pos.y() >= 0) {
        n->rect.moveTo(qMax(0, pos.x()), qMax(0, pos.y()));
    }
    n->setRectOf(0, n->rect);

    parent->children.append(qMakePair(EditorOps::childKeyFor(parent), n));
    parent->markDirty();
    m_mgr->model()->setDirty(true);
    if (ScenesScreen *s = m_mgr->currentScreen()) {
        s->rebuild();
    }
    emit nodeCreated(n);
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
     *   点 —— parent 为空，用当前选中的节点，按原厂限制判一道。
     * ★ 原厂限制：点击建控件时必须先选中一个**布局**。之前这里是"自己往下
     * 找第一个布局"，看着方便，但和原厂不是一回事：用户按原厂习惯先点布局
     * 再点控件，在我这儿会莫名其妙建到别的布局里去。 */
    UiNode *host = parent ? parent : m_current;
    if (!parent && !EditorOps::acceptsChild(host)) {
        EditorOps::tip(this, QStringLiteral("请选择一个布局或者新建一个并选中它."));
        return;
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
    Q_UNUSED(this)   // 入口保留（原厂 moc 里有这个槽），实际走 createControl()
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
    /* ★ 原厂限制：布局只能挂在图层下，而且要先选中那个图层 */
    if (!EditorOps::isLayer(m_current)) {
        EditorOps::tip(this, QStringLiteral("请选择一个图层或者新建一个图层,并选中它."));
        return;
    }
    ScenesScreen *sc = m_mgr->currentScreen();
    appendChild(m_current, QStringLiteral("NewLayout"), QStringLiteral("NewLayout"),
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
    /* 图层直接挂在页上，原厂没有额外限制 */
    appendChild(sc->page(), QStringLiteral("NewLayer"), QStringLiteral("NewLayer"),
                QStringLiteral("图层"),
                QStringLiteral("图层_%1").arg(m_mgr->model()->nextNodeSeq()));
}

/* ===================== PropertyTab ===================== */

PropertyTab::PropertyTab(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName(QStringLiteral("PropertyTab"));

    /* CSS 状态的增删走**页签的右键菜单** —— 手册 2.10 的原话是
     * "右键点击菜单项的 CSS 属性_0，选择复制添加"。原厂界面上没有按钮行。 */
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QWidget::customContextMenuRequested,
            this, &PropertyTab::onTabContextMenu);

    connect(this, &QTabWidget::currentChanged, this, &PropertyTab::onTabChanged);
    /* 没选中任何东西时也要有一页空的 CSS属性_0，否则页签区整个塌掉 */
    showNode(nullptr);
}

PropertyTab::~PropertyTab() = default;

/* 页签数 = element_css.struct 的长度：一个 CSS 状态一页，
 * 名字就是原厂那个 "CSS属性_N"。 */
void PropertyTab::showNode(UiNode *n)
{
    m_node = n;
    const int want = n ? qMax(1, n->cssStateCount()) : 1;

    while (count() > want) {
        QWidget *w = widget(count() - 1);
        removeTab(count() - 1);
        m_pages.removeAll(qobject_cast<CssProperty *>(w));
        w->deleteLater();
    }
    while (count() < want) {
        auto *p = new CssProperty(this);
        p->setState(count());
        connect(p, &BaseProperty::nodeEdited, this, &PropertyTab::nodeEdited);
        m_pages.append(p);
        addTab(p, tr("CSS属性_%1").arg(count()));
    }
    for (int i = 0; i < m_pages.size(); ++i) {
        m_pages.at(i)->setState(i);
        m_pages.at(i)->showNode(n);
    }
}

void PropertyTab::onTabChanged(int idex)
{
    if (idex >= 0 && idex < m_pages.size()) {
        m_pages.at(idex)->showNode(m_node);
    }
}

/* ---- element_css.struct 的增删改 --------------------------------------
 * 原厂属性区页签上方有一排小按钮：清除 / 复制添加 / 复制插入 / 删除活动项。
 * 一个页签 = 一个 CSS 状态 = struct 数组里的一项，这四个按钮就是对这个数组
 * 做操作。之前重建版只把页签画出来了，数组是只读的 —— 想加一个状态只能去
 * 手改 json。 */
void PropertyTab::onTabContextMenu(QPoint pos)
{
    const int idx = tabBar()->tabAt(pos);
    if (idx < 0) {
        return;
    }
    setCurrentIndex(idx);

    QMenu menu(this);
    QAction *aClear  = menu.addAction(QStringLiteral("清除"));
    QAction *aAppend = menu.addAction(QStringLiteral("复制添加"));
    QAction *aInsert = menu.addAction(QStringLiteral("复制插入"));
    menu.addSeparator();
    QAction *aRemove = menu.addAction(QStringLiteral("删除活动项"));
    /* 只剩一个状态时"删除活动项"直接置灰，比点了再弹一句"不让删"体面 */
    aRemove->setEnabled(count() > 1);

    QAction *c = menu.exec(tabBar()->mapToGlobal(pos));
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
    if (!m_node || currentIndex() < 0) {
        return;
    }
    m_node->cssClearState(currentIndex());
    showNode(m_node);
    emit nodeEdited(m_node);
}

void PropertyTab::onCopyAppendState()
{
    if (!m_node || currentIndex() < 0) {
        return;
    }
    m_node->cssInsertState(m_node->cssStateCount(), m_node->cssState(currentIndex()));
    showNode(m_node);
    setCurrentIndex(count() - 1);
    emit nodeEdited(m_node);
}

void PropertyTab::onCopyInsertState()
{
    if (!m_node || currentIndex() < 0) {
        return;
    }
    const int at = currentIndex() + 1;
    m_node->cssInsertState(at, m_node->cssState(currentIndex()));
    showNode(m_node);
    setCurrentIndex(at);
    emit nodeEdited(m_node);
}

void PropertyTab::onRemoveState()
{
    if (!m_node || currentIndex() < 0) {
        return;
    }
    if (!m_node->cssRemoveState(currentIndex())) {
        /* 只剩一个状态时不让删 —— 删光了控件就没有几何也没有样式了 */
        EditorOps::tip(this, QStringLiteral("至少要保留一个 CSS 状态。"));
        return;
    }
    showNode(m_node);
    emit nodeEdited(m_node);
}
