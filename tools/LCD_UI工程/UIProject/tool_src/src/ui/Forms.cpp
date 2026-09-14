#include "Forms.h"
#include "ProjectModel.h"
#include "EditorOps.h"
#include "Preview.h"
#include <QJsonObject>

#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QFontMetrics>
#include <QJsonArray>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QFile>

/* ===================== ResizeHandle ===================== */

static const int kHandleSize = 6;

ResizeHandle::ResizeHandle(QWidget *parent, Direction d, QWidget *target)
    : QWidget(parent), m_dir(d), m_target(target)
{
    setFixedSize(kHandleSize, kHandleSize);
    setMouseTracking(true);
    static const Qt::CursorShape cursors[] = {
        Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
        Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor
    };
    setCursor(cursors[int(d)]);
}

ResizeHandle::~ResizeHandle() = default;

void ResizeHandle::updatePosition()
{
    if (!m_target || !parentWidget()) {
        return;
    }
    const QRect g = m_target->geometry();
    const int h = kHandleSize / 2;
    QPoint p;
    switch (m_dir) {
    case LeftTop:     p = g.topLeft();                                    break;
    case Top:         p = QPoint(g.center().x(), g.top());                break;
    case RightTop:    p = g.topRight();                                   break;
    case Right:       p = QPoint(g.right(), g.center().y());              break;
    case RightBottom: p = g.bottomRight();                                break;
    case Bottom:      p = QPoint(g.center().x(), g.bottom());             break;
    case LeftBottom:  p = g.bottomLeft();                                 break;
    case Left:        p = QPoint(g.left(), g.center().y());               break;
    }
    move(p.x() - h, p.y() - h);
    raise();
}

void ResizeHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x7d, 0xd1));
    p.setPen(Qt::white);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}

void ResizeHandle::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_target) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragging    = true;
    m_pressGlobal = e->globalPos();
    m_startGeo    = m_target->geometry();
}

void ResizeHandle::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging || !m_target) {
        return;
    }
    const QPoint d = e->globalPos() - m_pressGlobal;
    QRect g = m_startGeo;
    switch (m_dir) {
    case LeftTop:     g.setTopLeft(g.topLeft() + d);          break;
    case Top:         g.setTop(g.top() + d.y());              break;
    case RightTop:    g.setTopRight(g.topRight() + d);        break;
    case Right:       g.setRight(g.right() + d.x());          break;
    case RightBottom: g.setBottomRight(g.bottomRight() + d);  break;
    case Bottom:      g.setBottom(g.bottom() + d.y());        break;
    case LeftBottom:  g.setBottomLeft(g.bottomLeft() + d);    break;
    case Left:        g.setLeft(g.left() + d.x());            break;
    }
    /* 点阵屏最小就是 1x1，不做下限会拖出负宽高，写进 json 后固件解析直接崩 */
    if (g.width() < 1) {
        g.setWidth(1);
    }
    if (g.height() < 1) {
        g.setHeight(1);
    }
    /* 只保下限，不夹位置：坐标可以是负的（见 CanvasItem::mouseMoveEvent 的说明）。 */
    m_target->setGeometry(g);
}

void ResizeHandle::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    m_dragging = false;
    emit mouseButtonReleased(m_startGeo, m_target ? m_target->geometry() : m_startGeo);
}

/* ===================== ResizeFrame ===================== */

ResizeFrame::ResizeFrame(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    m_lastGeo = geometry();
}

ResizeFrame::~ResizeFrame()
{
    /* 手柄挂在父控件上，Qt 不会跟着本对象一起销毁 —— 不删的话它们会留在
     * 画布上，m_target 还指着刚释放的本对象。画布每缩放一次都会 rebuild，
     * 孤儿手柄越攒越多，之后任何一次重绘/截图都在踩已释放内存。 */
    for (const QPointer<ResizeHandle> &h : m_handles) {
        delete h.data();                 // QPointer 已置空的（父控件先走）跳过
    }
    m_handles.clear();
}

void ResizeFrame::createHandles()
{
    if (!m_handles.isEmpty() || !parentWidget()) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        auto *h = new ResizeHandle(parentWidget(), ResizeHandle::Direction(i), this);
        connect(h, &ResizeHandle::mouseButtonReleased,
                this, [this](QRect oldGeo, QRect) { notifyGeometryChanged(oldGeo); });
        m_handles.append(h);
    }
}

void ResizeFrame::layoutHandles()
{
    for (const QPointer<ResizeHandle> &h : m_handles) {
        if (!h) {
            continue;
        }
        h->updatePosition();
        h->setVisible(m_selected && m_showChrome);
    }
}

void ResizeFrame::setShowChrome(bool on)
{
    if (m_showChrome == on) {
        return;
    }
    m_showChrome = on;
    layoutHandles();               // 手柄是独立子窗口，得单独收起来
    update();
}

void ResizeFrame::setSelected(bool on)
{
    m_selected = on;
    if (on) {
        createHandles();
        raise();
    }
    layoutHandles();
    update();
}

void ResizeFrame::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutHandles();
}

void ResizeFrame::moveEvent(QMoveEvent *e)
{
    QWidget::moveEvent(e);
    layoutHandles();
}

void ResizeFrame::notifyGeometryChanged(const QRect &oldGeo)
{
    const QRect now = geometry();
    if (now == oldGeo) {
        return;
    }
    m_lastGeo = now;
    layoutHandles();
    emit formWindowSizeChanged(oldGeo, now);
}

/* ===================== CanvasItem ===================== */

CanvasItem::CanvasItem(QWidget *parent)
    : ResizeFrame(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
}

CanvasItem::~CanvasItem() = default;

/** 控件的"对齐方式"（element_css 里的 align 枚举）。 */
Qt::Alignment CanvasItem::contentAlign() const
{
    if (!m_node) {
        return Qt::AlignLeft;
    }
    const QString a = m_node->cssField(0, QStringLiteral("align"),
                                       QStringLiteral("default")).toString();
    if (a == QLatin1String("ALIGN_RIGHT")) {
        return Qt::AlignRight;
    }
    if (a == QLatin1String("ALIGN_CENTER")) {
        return Qt::AlignHCenter;
    }
    return Qt::AlignLeft;
}

QColor CanvasItem::frameColor() const
{
    return QColor(0x60, 0x60, 0x60);
}

void CanvasItem::bind(UiNode *node)
{
    m_node = node;
    if (m_node) {
        setToolTip(QStringLiteral("%1  [%2]").arg(m_node->name, m_node->type));
        syncRectFromNode();
    }
}

void CanvasItem::setDisplayZoom(int percent)
{
    m_zoom = qBound(25, percent, 800);
}

void CanvasItem::syncRectToNode()
{
    if (!m_node) {
        return;
    }
    /* 画布坐标 -> 1:1 工程坐标。倍率一定要在这里除掉，别指望调用方。 */
    const QRect g = geometry();
    const QRect r = (m_zoom == 100)
        ? g
        : QRect(g.x() * 100 / m_zoom, g.y() * 100 / m_zoom,
                qMax(1, g.width() * 100 / m_zoom),
                qMax(1, g.height() * 100 / m_zoom));
    /* 几何写回 element_css.struct[0] 的 rect —— 工程 json 里控件没有独立的
     * rect 属性，只有页节点才有。 */
    if (m_node->cssStateCount() > 0) {
        m_node->setRectOf(0, r);
    } else {
        m_node->rect = r;
    }
    m_node->markDirty();
}

void CanvasItem::syncRectFromNode()
{
    if (!m_node || !m_node->rect.isValid()) {
        return;
    }
    const QRect r = m_node->rect;
    setGeometry(m_zoom == 100
                ? r
                : QRect(r.x() * m_zoom / 100, r.y() * m_zoom / 100,
                        qMax(1, r.width() * m_zoom / 100),
                        qMax(1, r.height() * m_zoom / 100)));
}

/** 从 element_css.struct[0] 里取背景色。json 里存的是 "#RRGGBB" 之类的字符串，
 *  空串表示不填充（没设色的控件透出父级的底）。 */
static QColor cssBackground(UiNode *n)
{
    if (!n) {
        return QColor();
    }
    const QJsonArray st = n->cssState(0);
    for (const QJsonValue &v : st) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("-name")).toString() != QLatin1String("background_color")) {
            continue;
        }
        const QString s = o.value(QStringLiteral("background-color")).toString();
        if (s.isEmpty()) {
            return QColor();
        }
        QColor c(s);
        return c.isValid() ? c : QColor();
    }
    return QColor();
}

void CanvasItem::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    /* ================= 按单色点阵屏来画 =================
     * 这是 128x64 单色屏的工具，屏上只有"亮/灭"。工程 json 里那些
     * #D9EE94 / #368FEE 之类的背景色，在设备上**根本不会显示成绿色蓝色** ——
     * 固件在 MONO 下只认三个魔数（见 Preview.h 抬头，规则抄自
     * ui_synthesis_oled.c 的 jlui_fill_rect / draw_rect / draw_text）。
     * 以前这里照着 css 里的颜色填色块，画出来花花绿绿，全是虚构的。 */
    const QString bgCss = m_node
        ? m_node->cssField(0, QStringLiteral("background_color"),
                           QStringLiteral("background-color")).toString()
        : QString();
    const Preview::MonoFill bgFill = Preview::fillOf(bgCss);
    const bool fill = bgFill == Preview::MonoFill::Set;

    QString txtCss;
    if (m_node) {
        for (const UiProperty &pr : m_node->props) {
            if (pr.caption == QStringLiteral("文字颜色")) {
                txtCss = pr.raw.value(QStringLiteral("color")).toString();
                if (txtCss.isEmpty()) {
                    txtCss = pr.raw.value(QStringLiteral("background-color")).toString();
                }
                break;
            }
        }
    }
    const Preview::MonoText tm = Preview::textModeOf(txtCss);

    /* 底：填充 = 点亮成白；反显文字也要先把整块点亮（固件就是先 fill 再挖字） */
    if (fill || tm == Preview::MonoText::Invert) {
        p.fillRect(rect(), Preview::monoLit());
    } else if (bgFill == Preview::MonoFill::Clear) {
        /* 【设了背景色 = 先擦干净】固件 jlui_fill_rect 对非 BGC_MONO_SET 的颜色
         * 是逐点 0x55aa（&= ~BIT），把这块擦成"灭"。所以这个控件会**盖住**底下
         * 布局的背景图，而不是透明叠加。不画这一笔的话，画布上看到的是叠加，
         * 和屏上不一致 —— 这正是"图片叠在背景图上"那个现象的成因。 */
        p.fillRect(rect(), Preview::monoDark());
    }

    /* 内容：图片/文字/数字。反显时字要画成"灭"。
     * 亮/灭具体是什么颜色由[全局设置]的点阵屏预览配色决定 —— 不同的屏差很多。 */
    if (tm != Preview::MonoText::Hidden) {
        const QColor litColor = (tm == Preview::MonoText::Invert)
                                ? Preview::monoDark() : Preview::monoLit();
        const QPixmap content = Preview::contentOf(m_node, litColor);
        if (!content.isNull()) {
            /* 【按原尺寸摆，不要拉伸】固件是按对齐方式贴进控件区域再裁掉超出
             * 的部分，不缩放。画布倍率要乘上去 —— 那是显示放大，不是内容变形。 */
            const QSize natural(content.width() * m_zoom / 100,
                                content.height() * m_zoom / 100);
            int x = 0;
            switch (contentAlign()) {
            case Qt::AlignRight:   x = width() - natural.width();       break;
            case Qt::AlignHCenter: x = (width() - natural.width()) / 2; break;
            default:               x = 0;                               break;
            }
            const int y = (height() - natural.height()) / 2;   // 竖向一律居中
            p.save();
            p.setClipRect(rect());
            p.drawPixmap(QRect(QPoint(x, y), natural), content);
            p.restore();
        }
    }

    /* 背景图片：css 里的 background-image。
     * 【这是屏上真会画的东西】StyBuilder 把它编进 css 的 +24 字段（图片资源号），
     * 固件按那个号取图铺在控件底下。以前画布压根不画它 —— 用户在属性面板里
     * 选完图，画布上什么变化都没有，看着就像"设置不生效"。
     * 铺在填充之上、内容之下，和固件的层次一致。 */
    if (m_node) {
        const QString bgi = m_node->cssField(0, QStringLiteral("background_image"),
                                             QStringLiteral("background-image")).toString();
        const QPixmap bg = Preview::pictureOf(bgi, Preview::monoLit());
        if (!bg.isNull()) {
            const int z = qMax(1, m_zoom);
            p.drawPixmap(0, 0, bg.width() * z / 100, bg.height() * z / 100, bg);
        }
    }

    /* 内边框线：宽度和颜色都在 css 里，颜色只决定"画不画"（和背景相反） */
    paintBorder(p);

    /* ---- 以下是编辑器自己的辅助线，不是屏上的东西 ----
     * 用细虚线 + 半透明，别和内容抢眼。选中时才实线。
     * 工具栏的「隐藏辅助线」关掉的就是这一段 —— 放大之后这些描边比内容还显眼，
     * 想看真实效果就得能藏起来。 */
    if (!m_showChrome) {
        return;
    }
    QPen pen(isSelected() ? QColor(0x2b, 0x7d, 0xd1) : QColor(255, 255, 255, 60));
    pen.setStyle(isSelected() ? Qt::SolidLine : Qt::DotLine);
    p.setPen(pen);
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    /* 【空内容的控件要看得见】刚建出来的「图片」控件图片列表是空的，「文字」
     * 控件文字列表也是空的 —— 屏上确实什么都不画，但编辑器里也什么都不画的话，
     * 用户建完控件面对一片黑，既不知道它在哪，也没法拖动改大小，看着就像
     * "加了图片没反应"。
     *
     * 这里给它画一个明显的占位：虚线框 + 一条对角线。这是编辑器的辅助标记，
     * 和上面那圈辅助线一样受工具栏「隐藏辅助线」管 —— 关掉就是屏上的真实
     * 样子，不会骗人。
     *
     * 【只在选中时画】一页里本来就有一批"内容运行时才填"的空控件（文件名、
     * 歌词……），全都标上的话整屏都是虚线，比不画还难看。而新建出来的控件是
     * **自动选中**的，正好落在这一条上：建完立刻看得见、拖得动。想挨个查有
     * 哪些还没配内容，点过去就是了。 */
    if (m_node && isSelected() && isContentEmpty()) {
        QPen ph(QColor(0x2b, 0x7d, 0xd1, 200));
        ph.setStyle(Qt::DashLine);
        p.setPen(ph);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.drawRect(r);
        p.drawLine(r.topLeft(), r.bottomRight());
    }

    /* 名字只在选中时显示，否则 200 多个控件叠上去全是字 */
    if (m_node && isSelected() && height() >= 10) {
        p.setPen(QColor(0x2b, 0x7d, 0xd1));
        const QString t = m_node->name.isEmpty() ? m_node->type : m_node->name;
        p.drawText(rect().adjusted(2, 0, -2, 0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(t, Qt::ElideRight, width() - 4));
    }
}

/**
 * 这个控件"该有内容但还是空的"吗？
 *
 * 只对**内容来自资源**的那几类控件成立：图片列表空的「图片」、文字列表空的
 * 「文字/数字/时间」、图片表空的「电池电量」。布局/图层/列表这些容器本来就
 * 没有自己的内容，不算。
 *
 * 用途只有一个：画布上给它画个占位框，让人看得见、摸得着（见 paintEvent）。
 */
bool CanvasItem::isContentEmpty() const
{
    if (!m_node) {
        return false;
    }
    static const QStringList kContentTypes{
        QStringLiteral("ImageList"), QStringLiteral("Text"),
        QStringLiteral("Number"), QStringLiteral("number"),
        QStringLiteral("Time"), QStringLiteral("Battery"),
    };
    if (!kContentTypes.contains(m_node->type)) {
        return false;
    }
    /* 有背景图也算有内容 —— 屏上看得见东西 */
    if (!m_node->cssField(0, QStringLiteral("background_image"),
                          QStringLiteral("background-image")).toString().isEmpty()) {
        return false;
    }
    return Preview::contentOf(m_node, Preview::monoLit()).isNull();
}

/** 画 css 里的"内边框线"。单色屏下颜色只决定画不画。 */
void CanvasItem::paintBorder(QPainter &p)
{
    if (!m_node) {
        return;
    }
    const QJsonObject b = m_node->cssField(0, QStringLiteral("border"),
                                           QStringLiteral("border")).toObject();
    if (b.isEmpty()) {
        return;
    }
    const QString css = m_node->cssField(0, QStringLiteral("border"),
                                         QStringLiteral("color")).toString();
    if (!Preview::borderVisible(css)) {
        return;
    }
    const int z = qMax(1, m_zoom / 100);
    const int l = b.value(QStringLiteral("left")).toInt() * z;
    const int t = b.value(QStringLiteral("top")).toInt() * z;
    const int r = b.value(QStringLiteral("right")).toInt() * z;
    const int bo = b.value(QStringLiteral("bottom")).toInt() * z;
    if (l <= 0 && t <= 0 && r <= 0 && bo <= 0) {
        return;
    }
    p.save();
    p.setPen(Qt::NoPen);
    /* 内边框是**屏上真画出来的东西**（jlui_draw_rect），所以用点亮色，
     * 不是写死白色 —— 和填充、文字走同一套配色。 */
    p.setBrush(Preview::monoLit());
    if (l > 0) {
        p.drawRect(QRect(0, 0, l, height()));
    }
    if (r > 0) {
        p.drawRect(QRect(width() - r, 0, r, height()));
    }
    if (t > 0) {
        p.drawRect(QRect(0, 0, width(), t));
    }
    if (bo > 0) {
        p.drawRect(QRect(0, height() - bo, width(), bo));
    }
    p.restore();
}

void CanvasItem::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_moving      = true;
    m_pressGlobal = e->globalPos();
    m_startGeo    = geometry();
    setSelected(true);
}

void CanvasItem::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_moving) {
        return;
    }
    const QPoint d = e->globalPos() - m_pressGlobal;
    /* 【不要钳制坐标】我一度在这儿把 x/y 夹到 [0, 父级尺寸]，理由是"负坐标
     * 写进 .sty 会被当成无符号读，控件飞屏外"。查了固件才知道这个理由是错的：
     * left/top 是**有符号**的，而且 SmallColor_oled.json 里就有 (0,-11)
     * 这样的控件 —— 让内容从父容器上边缘露出去是常用手法。
     * 加了钳制等于凭空多出一条限制，还会把用户已有的负坐标改掉。 */
    move(m_startGeo.topLeft() + d);
}

void CanvasItem::mouseReleaseEvent(QMouseEvent *e)
{
    Q_UNUSED(e)
    if (!m_moving) {
        return;
    }
    m_moving = false;
    syncRectToNode();
    notifyGeometryChanged(m_startGeo);
}

/* ===================== 右键菜单 ===================== */

QVector<CanvasItem *> CanvasItem::subForms() const
{
    return QVector<CanvasItem *>::fromList(
        findChildren<CanvasItem *>(QString(), Qt::FindDirectChildrenOnly));
}

void CanvasItem::contextMenuEvent(QContextMenuEvent *e)
{
    /* 右键先把自己选中，再弹菜单 —— 不然菜单里的"删除当前-xxx"说的是
     * 上一个选中的东西，点下去删错。 */
    setSelected(true);
    showContextMenu(e->globalPos());
    e->accept();
}

void CanvasItem::showContextMenu(const QPoint &globalPos)
{
    if (!m_node) {
        return;
    }
    QMenu menu(this);
    const QString label = m_node->name.isEmpty() ? m_node->type : m_node->name;

    QAction *aDel  = menu.addAction(QStringLiteral("删除当前-%1").arg(label));
    QAction *aSave = menu.addAction(QStringLiteral("保存成控件"));
    appendTypeActions(menu);            // 列表 / 表格的专有项
    menu.addSeparator();

    /* 这两项的文字随当前状态在"显示/隐藏"之间翻 —— 四条串
     * （显示同类容器 / 显示 / 隐藏同类容器 / 隐藏）就是两个开关的两种文字。 */
    const QVector<CanvasItem *> subs = subForms();
    const bool subShown = !subs.isEmpty() && subs.first()->isVisible();
    QAction *aSub  = subs.isEmpty()
                     ? nullptr
                     : menu.addAction(subShown ? QStringLiteral("隐藏同类容器")
                                               : QStringLiteral("显示同类容器"));
    QAction *aSelf = menu.addAction(isVisible() ? QStringLiteral("隐藏")
                                                : QStringLiteral("显示"));
    menu.addSeparator();

    QAction *aCopy  = menu.addAction(QStringLiteral("复制"));
    QAction *aPaste = menu.addAction(QStringLiteral("粘贴"));
    menu.addSeparator();

    QAction *aTop    = menu.addAction(QStringLiteral("移到顶层"));
    QAction *aUp     = menu.addAction(QStringLiteral("移上一层"));
    QAction *aDown   = menu.addAction(QStringLiteral("移下一层"));
    QAction *aBottom = menu.addAction(QStringLiteral("移到底层"));
    menu.addSeparator();

    QAction *aFind = menu.addAction(QStringLiteral("查找控件"));

    QAction *c = menu.exec(globalPos);
    if (!c) {
        return;
    }
    if (c == aDel) {
        onDeleteMe();                       // 里面会问一次
    } else if (c == aSave) {
        saveAsTemplate();
    } else if (aSub && c == aSub) {
        for (CanvasItem *f : subs) {
            emit userHideRequested(f->node());
        }
    } else if (c == aSelf) {
        /* 交给画布记账，理由同 ObjectTreeDock::onSwapShowHideObject */
        emit userHideRequested(m_node);
    } else if (c == aCopy) {
        EditorOps::copyToClip(m_node);
    } else if (c == aPaste) {
        doPaste();
    } else if (c == aTop || c == aUp || c == aDown || c == aBottom) {
        const EditorOps::ZMove how = (c == aTop)  ? EditorOps::ZTop
                                   : (c == aUp)   ? EditorOps::ZUp
                                   : (c == aDown) ? EditorOps::ZDown
                                                  : EditorOps::ZBottom;
        if (EditorOps::moveZ(m_node, how)) {
            emit structureChanged();
        }
    } else if (c == aFind) {
        emit findRequested();
    }
}

/** 粘贴。谁能收、收不了说什么，见 EditorOps 里那几条提示语。 */
void CanvasItem::doPaste()
{
    if (EditorOps::clipEmpty()) {
        return;
    }
    if (!EditorOps::acceptsChild(m_node)) {
        /* 选中的不是布局 */
        EditorOps::tip(this, QStringLiteral(
            "选中的这个装不下粘贴的内容，先选一个布局。"));
        return;
    }
    if (!EditorOps::pasteInto(m_node)) {
        /* 是布局，但剪贴板里那个东西不能往布局里塞（比如整个图层） */
        EditorOps::tip(this, QStringLiteral("这个容器收不了剪贴板里的东西。"));
        return;
    }
    emit structureChanged();
}

/**
 * "保存成控件"：把当前节点存成一份控件模板，落到工具目录的 assets/widgets.d/。
 *
 * 存的格式就是 control.json 的格式（顶层 "compoents" 数组，键名沿用
 * 那个拼写错误），ControlLibrary 启动时扫这个目录，下次就出现在
 * "自定义控件" 那一组里。
 */
void CanvasItem::saveAsTemplate()
{
    if (!m_node) {
        return;
    }
    QString name = m_node->name;
    if (!EditorOps::silent()) {
        bool ok = false;
        name = QInputDialog::getText(this, QStringLiteral("提示"),
                                     QStringLiteral("请输入控件名称"),
                                     QLineEdit::Normal, m_node->name, &ok);
        if (!ok || name.isEmpty()) {
            return;
        }
    }
    const QString dir = EditorOps::customWidgetDir();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        EditorOps::warn(this, QStringLiteral("找不到控件文件,请查看[全局设置]里的路径目录是否正确."));
        return;
    }
    QJsonObject o = ProjectModel::toJsonObject(m_node);
    o.insert(QStringLiteral("-name"), name);
    o.insert(QStringLiteral("caption"), name);
    QJsonArray arr;
    arr.append(o);
    QJsonObject root;
    root.insert(QStringLiteral("compoents"), arr);

    QFile f(QDir(dir).filePath(name + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        EditorOps::warn(this, QStringLiteral("找不到控件文件,请查看[全局设置]里的路径目录是否正确."));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

/* --- 属性面板过来的槽。签名固定，行为按原工具的语义重写 --- */

void CanvasItem::onXYWHChangedValue(int v)
{
    /* 发信号的 spinBox 用 objectName 区分是 x / y / w / h，
     * 这是原工具的做法：四个 spinBox 接同一个槽。 */
    QObject *s = sender();
    if (!s || !m_node) {
        return;
    }
    QRect g = geometry();
    const QString which = s->objectName();
    if (which == QLatin1String("spinX")) {
        g.moveLeft(v);
    } else if (which == QLatin1String("spinY")) {
        g.moveTop(v);
    } else if (which == QLatin1String("spinW") || which == QLatin1String("spinH")) {
        /* ★ 限制：宽高不许是 0。这里不能像以前那样偷偷 qMax(1,v) 兜过去
         * —— 用户敲了 0，界面上还显示 0，存进去却是 1，下次打开数字自己变了。
         * 直接拦下来告诉用户，别自作主张。 */
        if (v == 0) {
            EditorOps::tip(this, QStringLiteral("宽和高不能设成 0。"));
            return;
        }
        if (which == QLatin1String("spinW")) {
            g.setWidth(v);
        } else {
            g.setHeight(v);
        }
    } else {
        return;
    }
    const QRect old = geometry();
    setGeometry(g);
    syncRectToNode();
    notifyGeometryChanged(old);
}

void CanvasItem::onSwapViewObject()
{
    setVisible(!isVisible());
}

void CanvasItem::onClearJsonValue()
{
    if (!m_node) {
        return;
    }
    for (UiProperty &p : m_node->props) {
        if (p.name != QLatin1String("id") && p.name != QLatin1String("rect")) {
            p.raw = QJsonObject();
            p.dirty = true;
        }
    }
    m_node->markDirty();
    update();
}

void CanvasItem::onTextChanged(QString str)
{
    if (!m_node) {
        return;
    }
    QObject *s = sender();
    const QString key = s ? s->objectName() : QString();
    if (UiProperty *p = m_node->findProp(key)) {
        p->raw.insert(QStringLiteral("default"), str);
        p->dirty = true;
        m_node->markDirty();
    }
    update();
}

void CanvasItem::onTextSelected()
{
    setSelected(true);
}

void CanvasItem::onNumberChanged(int num)
{
    if (!m_node) {
        return;
    }
    QObject *s = sender();
    const QString key = s ? s->objectName() : QString();
    if (UiProperty *p = m_node->findProp(key)) {
        p->raw.insert(QStringLiteral("default"), num);
        p->dirty = true;
        m_node->markDirty();
    }
}

void CanvasItem::onEnumItemChanged(QString txt)
{
    if (!m_node) {
        return;
    }
    QObject *s = sender();
    const QString key = s ? s->objectName() : QString();
    if (UiProperty *p = m_node->findProp(key)) {
        p->raw.insert(QStringLiteral("default"), txt);
        p->dirty = true;
        m_node->markDirty();
    }
}

void CanvasItem::onColorButtonClicked()
{
    if (!m_node) {
        return;
    }
    const QColor c = QColorDialog::getColor(Qt::black, this, QStringLiteral("请选择颜色"));
    if (!c.isValid()) {
        return;
    }
    QObject *s = sender();
    const QString key = s ? s->objectName() : QStringLiteral("color");
    if (UiProperty *p = m_node->findProp(key)) {
        p->raw.insert(QStringLiteral("default"), int(c.rgb() & 0xFFFFFF));
        p->dirty = true;
        m_node->markDirty();
    }
    update();
}

void CanvasItem::onBorderChangedValue(int v)
{
    if (!m_node) {
        return;
    }
    if (UiProperty *p = m_node->findProp(QStringLiteral("border"))) {
        p->raw.insert(QStringLiteral("default"), v);
        p->dirty = true;
        m_node->markDirty();
    }
    update();
}

void CanvasItem::onBackgroundImageDialog()
{
    if (!m_node) {
        return;
    }
    const QString f = QFileDialog::getOpenFileName(
        this, tr("选择背景图"), QString(), tr("图片 (*.bmp *.png *.jpg)"));
    if (f.isEmpty()) {
        return;
    }
    if (UiProperty *p = m_node->findProp(QStringLiteral("background"))) {
        p->raw.insert(QStringLiteral("default"), f);
        p->dirty = true;
        m_node->markDirty();
    }
    update();
}

void CanvasItem::onActionDialog()
{
    /* 这里该弹 EventActionDialog 对话框编辑 element_event_action。
     * 这一段的二进制布局还没定下来，先不做。 */
    QMessageBox::information(this, tr("事件动作"),
                             tr("事件动作编辑尚未实现。"));
}

void CanvasItem::onDeleteMe()
{
    if (!m_node || !m_node->parent) {
        deleteLater();
        return;
    }
    /* ★ 删除一定先问一次，而且按钮就叫 <删除>（正文里那句"请选择<删除>
     * 删除"说的就是它）。删除不可撤消 —— 这个工具没有 undo。 */
    const QString what = EditorOps::isLayer(m_node)  ? QStringLiteral("图层")
                       : EditorOps::isLayout(m_node) ? QStringLiteral("布局")
                       : (m_node->name.isEmpty() ? m_node->type : m_node->name);
    if (!EditorOps::confirmDelete(this, what)) {
        return;
    }
    UiNode *parentNode = m_node->parent;
    for (int i = 0; i < parentNode->children.size(); ++i) {
        if (parentNode->children.at(i).second == m_node) {
            delete parentNode->children.at(i).second;
            parentNode->children.remove(i);
            parentNode->markDirty();
            break;
        }
    }
    m_node = nullptr;
    emit structureChanged();
    deleteLater();
}

void CanvasItem::onListImageChanged(QString a0)
{
    if (!m_node) {
        return;
    }
    if (UiProperty *p = m_node->findProp(QStringLiteral("imagelist"))) {
        p->raw.insert(QStringLiteral("default"), a0);
        p->dirty = true;
        m_node->markDirty();
    }
    update();
}

/* ===================== 五个子类 ===================== */

NewLayer::NewLayer(QWidget *parent) : CanvasItem(parent) {}
NewLayer::~NewLayer() = default;
QColor NewLayer::frameColor() const { return QColor(0x1f, 0x6f, 0xb5); }
void NewLayer::onDeleteMe() { CanvasItem::onDeleteMe(); }

NewLayout::NewLayout(QWidget *parent) : CanvasItem(parent) {}
NewLayout::~NewLayout() = default;
QColor NewLayout::frameColor() const { return QColor(0x2e, 0x8b, 0x57); }
void NewLayout::onDeleteMe() { CanvasItem::onDeleteMe(); }

void NewLayout::onBeComeTemplateWidget()
{
    /* 把当前布局存成模板，落到自定义控件目录下供以后复用。 */
    if (!m_node) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("存为模板"), tr("模板名"),
                                               QLineEdit::Normal, m_node->name, &ok);
    if (ok && !name.isEmpty()) {
        m_node->setExtra(QStringLiteral("template"), name);
    }
}

NewFrame::NewFrame(QWidget *parent) : CanvasItem(parent) {}
NewFrame::~NewFrame() = default;
QColor NewFrame::frameColor() const { return QColor(0xb5, 0x5f, 0x1f); }
void NewFrame::onDeleteMe() { CanvasItem::onDeleteMe(); }

NewList::NewList(QWidget *parent) : CanvasItem(parent) {}
NewList::~NewList() = default;
QColor NewList::frameColor() const { return QColor(0x7a, 0x3f, 0xb5); }
void NewList::onDeleteMe() { CanvasItem::onDeleteMe(); }

/** 竖列表 = 一行行往下排，横列表 = 一列列往右排。orientation 决定叫法。 */
static bool listIsVertical(UiNode *n)
{
    return !n || n->extraValue(QStringLiteral("orientation")).toString()
                 != QLatin1String("Horizontal");
}

void NewList::onAddManyLine()
{
    if (!m_node) {
        return;
    }
    const bool vert = listIsVertical(m_node);
    int n = 1;
    if (!EditorOps::silent()) {
        bool ok = false;
        /* 输入框的上限提示是"9999 内的整数" */
        n = QInputDialog::getInt(this,
                                 vert ? QStringLiteral("添加行") : QStringLiteral("添加列"),
                                 QStringLiteral("9999 内的整数"), 1, 1, 9999, 1, &ok);
        if (!ok) {
            return;
        }
    }
    /* 【行是克隆出来的，不是现搭的】列表的每一行在工程文件里是一个完整的
     * NewLayout 子树（listwidget -> NewLayout -> 若干 NewFrame），它带着
     * element_css、事件、图片引用。之前这里现搭了一个空的 NewFrame 塞进
     * listwidget：既不符合结构约定（listwidget 下只能是 NewLayout），
     * 生成资源时也拿不到任何内容。有第一行就照着克隆，没有就退回建空布局。 */
    const UiNode *tpl = nullptr;
    for (const auto &c : m_node->children) {
        if (c.first == QLatin1String("listwidget")) {
            tpl = c.second;
            break;
        }
    }
    for (int i = 0; i < n; ++i) {
        UiNode *row = nullptr;
        if (tpl) {
            row = ProjectModel::cloneNode(tpl, m_node);
        } else {
            /* 【列表还空着时，照 control.json 的模板建，不要现搭空壳】
             * 以前这里搭的是个只有 -class/-type/-name 的壳：一条属性都没有，
             * 属性面板上空空如也，生成资源时既没几何也没样式 ——
             * 用户报的"右键添加行新增的布局参数比正常的少"就是这条路。
             * 行和普通布局是**同一套参数**（既有工程 167 个行
             * vs 65 个图层下的布局，property 名单、element_css 字段和状态数
             * 全部一致，见 docs/UI_BEHAVIOR.md §14.12），所以这里必须走和
             * 控件栏那条路同一份模板。 */
            row = EditorOps::makeFromTemplate(m_node, QStringLiteral("NewLayout"),
                                              QStringLiteral("布局"));
        }
        /* 【名字要和正常新建的一样】规则是"中文名_全局序号"（布局_37）。
         * 以前克隆出来的沿用被克隆那一行的名字、退化路径更是直接叫
         * "NewLayout" —— 同一个工程里冒出英文名，树上一眼就看出来是两条路。 */
        if (row->caption.isEmpty()) {
            row->caption = QStringLiteral("布局");
        }
        if (row->icon.isEmpty()) {
            row->icon = QStringLiteral("config/images/layout.ico");
        }
        row->name = EditorOps::uniqueName(
            m_node, EditorOps::defaultNodeName(row->caption));
        /* 【ID 号也要重分配】行是从第一行整棵克隆出来的，ename 一起带过来了；
         * 不换的话 ename.h 里同一个宏会 #define 好几次，业务代码引用到哪一个
         * 全看运气。和粘贴是同一类问题，走同一个函数。 */
        EditorOps::reassignEnames(row, m_node);
        m_node->children.append(qMakePair(QStringLiteral("listwidget"), row));
    }
    m_node->markDirty();
    emit structureChanged();
}

void NewList::onSetFixedHeight()
{
    if (!m_node) {
        return;
    }
    /* 【改的是 sizehw，不是控件自己的高度】列表整体多高由它的 rect 决定，
     * 每一行/每一列多大是 json 里那个独立的 "sizehw" 字段（既有工程文件里
     * VerticalList 是 sizehw=16 / space=0）。以前这里去 resize 控件本身，
     * 改的是整个列表的高度，等于把"设置行高"做成了"改列表大小"。 */
    const bool vert = listIsVertical(m_node);
    const int cur = m_node->extraValue(QStringLiteral("sizehw")).toInt(16);
    bool ok = false;
    const int v = QInputDialog::getInt(this,
                                       vert ? QStringLiteral("设置行高")
                                            : QStringLiteral("设置列宽"),
                                       vert ? QStringLiteral("单元高度:")
                                            : QStringLiteral("单元宽度:"),
                                       cur, 1, 9999, 1, &ok);
    if (!ok) {
        return;
    }
    setCellSize(v);
}

void NewList::setCellSize(int v)
{
    if (!m_node || v <= 0) {
        return;
    }
    m_node->setExtra(QStringLiteral("sizehw"), v);
    cellParamsChanged();
}

void NewList::setCellSpace(int v)
{
    if (!m_node || v < 0) {
        return;
    }
    m_node->setExtra(QStringLiteral("space"), v);
    cellParamsChanged();
}

void NewList::setOrientation(bool vertical)
{
    if (!m_node) {
        return;
    }
    m_node->setExtra(QStringLiteral("orientation"),
                     vertical ? QStringLiteral("Vertical")
                              : QStringLiteral("Horizontal"));
    /* 方向一换，格子从"整宽 x sizehw"变成"sizehw x 整高"，每一行都得重算 */
    cellParamsChanged();
}

/**
 * 滚轮翻行 —— 手册 2.11 明说的操作。
 *
 * 【只动显示，不动数据】翻行改的是"从第几行开始画"，各行在 json 里的
 * 次序和坐标一个字节都不能变。所以这里既不 markDirty 也不写 rect，
 * 只是把画布上那些行挪一挪位置、超出可视区的藏起来。
 */
void NewList::wheelEvent(QWheelEvent *e)
{
    const QVector<CanvasItem *> rows = subForms();
    if (rows.isEmpty()) {
        CanvasItem::wheelEvent(e);
        return;
    }
    const int delta = e->angleDelta().y();
    if (delta == 0) {
        CanvasItem::wheelEvent(e);
        return;
    }
    setFirstVisible(m_first + (delta > 0 ? -1 : 1));
    e->accept();
}

void NewList::setFirstVisible(int i)
{
    const QVector<CanvasItem *> rows = subForms();
    const int maxFirst = qMax(0, rows.size() - 1);
    const int want = qBound(0, i, maxFirst);
    if (want == m_first) {
        return;
    }
    m_first = want;
    relayoutRows();
}

bool CanvasItem::reflowCells()
{
    if (!m_node) {
        return false;
    }
    bool changed = false;
    int i = 0;
    for (const auto &c : m_node->children) {
        const QRect r = EditorOps::cellRectFor(m_node, i++);
        if (!r.isValid() || c.second->rect == r) {
            continue;
        }
        c.second->rect = r;
        c.second->setRectOf(0, r);      // 几何真正存的地方是 element_css
        c.second->markDirty();
        changed = true;
    }
    return changed;
}

void CanvasItem::cellParamsChanged()
{
    reflowCells();
    /* 【必须往上发一次】只 update() 的话，重画的只有这个控件自己：各行还在
     * 老位置老尺寸（relayoutRows 只有滚轮翻行时才跑），右栏预览不知道，
     * 工程也不算改过。走 structureChanged 这条现成的路 —— 画布照新参数
     * 整个重建（重建里 onSubtreeBuilt() 会把行摆好），树和页面栏跟着刷，
     * EditorSession 顺手置脏。 */
    update();
    emit structureChanged();
}

void NewList::resizeEvent(QResizeEvent *e)
{
    CanvasItem::resizeEvent(e);
    /* 列表一改大小，格子也跟着变（水平列表的行高 = 列表高） */
    relayoutRows();
}

void NewList::relayoutRows()
{
    if (!m_node) {
        return;
    }
    const QVector<CanvasItem *> rows = subForms();
    const bool vert = listIsVertical(m_node);
    /* 【行高/间隔也要乘倍率】sizehw / space 是工程里的 1:1 逻辑像素，而这里
     * 摆的是**屏幕**坐标。以前直接拿来用，放大之后列表外框和别的控件都按倍率
     * 变大了，行却还是原尺寸 —— 一滚轮就露馅：行高和周围对不上。
     * 和 CanvasItem::syncRectFromNode() 用同一套换算，别在这儿自成一派。 */
    const int z = qMax(1, displayZoom());
    const int size = qMax(1, m_node->extraValue(QStringLiteral("sizehw")).toInt(16)
                             * z / 100);
    const int space = m_node->extraValue(QStringLiteral("space")).toInt(0) * z / 100;
    const int step = size + space;

    for (int i = 0; i < rows.size(); ++i) {
        CanvasItem *r = rows.at(i);
        const int off = (i - m_first) * step;
        if (vert) {
            r->move(r->x(), off);
            r->resize(r->width(), size);
            r->setVisible(off + size > 0 && off < height());
        } else {
            r->move(off, r->y());
            r->resize(size, r->height());
            r->setVisible(off + size > 0 && off < width());
        }
    }
}

void NewList::appendTypeActions(QMenu &menu)
{
    if (!m_node) {
        return;
    }
    const bool vert = listIsVertical(m_node);
    menu.addSeparator();
    QAction *aAdd = menu.addAction(vert ? QStringLiteral("添加行")
                                        : QStringLiteral("添加列"));
    connect(aAdd, &QAction::triggered, this, &NewList::onAddManyLine);
    QAction *aSize = menu.addAction(vert ? QStringLiteral("设置行高")
                                         : QStringLiteral("设置列宽"));
    connect(aSize, &QAction::triggered, this, &NewList::onSetFixedHeight);

    QAction *aSpace = menu.addAction(QStringLiteral("设置间隔"));
    connect(aSpace, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int cur = m_node->extraValue(QStringLiteral("space")).toInt(0);
        const int v = QInputDialog::getInt(this, QStringLiteral("设置间隔"),
                                           QStringLiteral("单元间隔:"), cur, 0, 9999, 1, &ok);
        if (ok) {
            setCellSpace(v);
        }
    });

    /* 滚动方向就是 orientation 字段，菜单上是"垂直滚动/水平滚动"两项 */
    QAction *aVert = menu.addAction(QStringLiteral("垂直滚动"));
    QAction *aHorz = menu.addAction(QStringLiteral("水平滚动"));
    aVert->setCheckable(true);
    aHorz->setCheckable(true);
    aVert->setChecked(vert);
    aHorz->setChecked(!vert);
    connect(aVert, &QAction::triggered, this, [this]() { setOrientation(true); });
    connect(aHorz, &QAction::triggered, this, [this]() { setOrientation(false); });
}

NewGrid::NewGrid(QWidget *parent) : CanvasItem(parent) {}
NewGrid::~NewGrid() = default;
QColor NewGrid::frameColor() const { return QColor(0xb5, 0x1f, 0x5f); }
void NewGrid::onDeleteMe() { CanvasItem::onDeleteMe(); }

/* 【说清楚不确定的地方】NewGrid 在 SmallColorTFT.json 里一个实例
 * 都没有（277 个节点：NewFrame 175 / NewLayout 85 / NewList 14 / NewLayer 3），
 * control.json 的模板里也只有 id / element_css / scroll / highlight_index /
 * action，没有存行列数的字段。行列数究竟落在哪个键上我没有样本可对，这里
 * 先用 rows / cols。等拿到带 NewGrid 的样本工程，要按样本改键名。 */
void NewGrid::onAddOneRow()
{
    if (!m_node) {
        return;
    }
    setGrid(m_node->extraValue(QStringLiteral("rows")).toInt(1) + 1,
            m_node->extraValue(QStringLiteral("cols")).toInt(1));
}

void NewGrid::onAddOneCol()
{
    if (!m_node) {
        return;
    }
    setGrid(m_node->extraValue(QStringLiteral("rows")).toInt(1),
            m_node->extraValue(QStringLiteral("cols")).toInt(1) + 1);
}

void NewGrid::setGrid(int rows, int cols)
{
    if (!m_node || rows <= 0 || cols <= 0) {
        return;
    }
    m_node->setExtra(QStringLiteral("rows"), rows);
    m_node->setExtra(QStringLiteral("cols"), cols);
    cellParamsChanged();
}

void NewGrid::setCellSize(int w, int h)
{
    if (!m_node || w <= 0 || h <= 0) {
        return;
    }
    m_node->setExtra(QStringLiteral("cell_w"), w);
    m_node->setExtra(QStringLiteral("cell_h"), h);
    cellParamsChanged();
}

void NewGrid::setCellSpace(int v)
{
    if (!m_node || v < 0) {
        return;
    }
    m_node->setExtra(QStringLiteral("space"), v);
    cellParamsChanged();
}

void NewGrid::appendTypeActions(QMenu &menu)
{
    if (!m_node) {
        return;
    }
    menu.addSeparator();
    QAction *aRow = menu.addAction(QStringLiteral("添加行"));
    connect(aRow, &QAction::triggered, this, &NewGrid::onAddOneRow);
    QAction *aCol = menu.addAction(QStringLiteral("添加列"));
    connect(aCol, &QAction::triggered, this, &NewGrid::onAddOneCol);

    QAction *aCell = menu.addAction(QStringLiteral("单元尺寸"));
    connect(aCell, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int w = QInputDialog::getInt(this, QStringLiteral("每个单元的大小"),
                                           QStringLiteral("单元宽度:"),
                                           m_node->extraValue(QStringLiteral("cell_w")).toInt(16),
                                           1, 9999, 1, &ok);
        if (!ok) {
            return;
        }
        const int h = QInputDialog::getInt(this, QStringLiteral("每个单元的大小"),
                                           QStringLiteral("单元高度:"),
                                           m_node->extraValue(QStringLiteral("cell_h")).toInt(16),
                                           1, 9999, 1, &ok);
        if (!ok) {
            return;
        }
        setCellSize(w, h);
    });

    QAction *aSpace = menu.addAction(QStringLiteral("单元间距"));
    connect(aSpace, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int v = QInputDialog::getInt(this, QStringLiteral("单元间距"),
                                           QStringLiteral("单元间隔:"),
                                           m_node->extraValue(QStringLiteral("space")).toInt(0),
                                           0, 9999, 1, &ok);
        if (ok) {
            setCellSpace(v);
        }
    });

    QAction *aSet = menu.addAction(QStringLiteral("设置行列"));
    connect(aSet, &QAction::triggered, this, [this]() {
        bool ok = false;
        const int r = QInputDialog::getInt(this, QStringLiteral("提示"),
                                           QStringLiteral("请输入行列整数,必需满足 (行*列>0)"),
                                           m_node->extraValue(QStringLiteral("rows")).toInt(1),
                                           0, 9999, 1, &ok);
        if (!ok) {
            return;
        }
        const int c = QInputDialog::getInt(this, QStringLiteral("提示"),
                                           QStringLiteral("请输入行列整数,必需满足 (行*列>0)"),
                                           m_node->extraValue(QStringLiteral("cols")).toInt(1),
                                           0, 9999, 1, &ok);
        if (!ok) {
            return;
        }
        if (r * c <= 0) {
            EditorOps::tip(this, QStringLiteral("请输入行列整数,必需满足 (行*列>0)"));
            return;
        }
        m_node->setExtra(QStringLiteral("rows"), r);
        m_node->setExtra(QStringLiteral("cols"), c);
        update();
    });
}

/* ===================== 工厂 ===================== */

CanvasItem *createFormForClass(const QString &cls, QWidget *parent)
{
    if (cls == QLatin1String("NewLayer")) {
        return new NewLayer(parent);
    }
    if (cls == QLatin1String("NewLayout")) {
        return new NewLayout(parent);
    }
    if (cls == QLatin1String("NewList")) {
        return new NewList(parent);
    }
    if (cls == QLatin1String("NewGrid")) {
        return new NewGrid(parent);
    }
    /* 工程 json 里绝大多数控件("-type" = Battery / Text / ImageList ...)
     * 的 "-class" 都是 NewFrame，兜底也用它 */
    return new NewFrame(parent);
}
