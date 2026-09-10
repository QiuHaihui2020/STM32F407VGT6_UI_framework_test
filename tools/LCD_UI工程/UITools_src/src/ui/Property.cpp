#include "Property.h"

#include "ActionList.h"
#include "EditorOps.h"
#include "Preview.h"
#include "I18nLanguage.h"
#include "ImageFileDialog.h"
#include "ImageListView.h"

#include <QJsonArray>
#include <QMessageBox>
#include "ProjectModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QColorDialog>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QIcon>
#include <QFont>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QJsonArray>
#include <QSignalBlocker>

/**
 * 挡掉数值框/下拉框上的鼠标滚轮。
 *
 * 属性栏又长又密，滚页面时鼠标必然从一堆 spin/combo 上扫过去，Qt 默认会把
 * 滚轮当成"改值"，一不留神坐标就被改了，而且**没有撤消**，很难发现。
 *
 * 【不能简单地 return true 吃掉】那样鼠标停在控件上就没法滚动属性栏了。
 * 做法是把滚轮事件转给最近的滚动区，让页面照常滚、值不动。
 * 控件仍然可以点进去用键盘或箭头改 —— 只是不再"扫过就改"。
 */
/**
 * 带尾巴的浮动提示气泡。
 *
 * 【为什么不用面板里那行字】属性栏只有两百来像素宽，一句话的警告塞进
 * QFormLayout 的一行里会被挤成好几行还显示不全 —— 等于没提示。
 * 气泡是**顶层窗口**，不受面板宽度限制，尾巴指着出问题的那个输入框，
 * 一眼就知道说的是谁。
 *
 * 【为什么不用 QToolTip】QToolTip 要鼠标悬停才出来，而这种"配置本身有问题"
 * 的提示得**主动弹**；而且 QToolTip 没有指向性尾巴，属性栏里上下十几个框
 * 挨着，光一个气泡看不出在说哪一个。
 */
class CalloutTip : public QWidget
{
public:
    explicit CalloutTip(QWidget *parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint
                          | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        m_hide.setSingleShot(true);
        connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
    }

    /** 贴着 anchor 弹出来；msec<=0 表示不自动收。 */
    void showFor(QWidget *anchor, const QString &text, int msec = 8000)
    {
        if (!anchor || text.isEmpty()) {
            hide();
            return;
        }
        m_text = text;
        m_anchor = anchor;

        const QFontMetrics fm(font());
        const int maxW = 320;
        QRect need = fm.boundingRect(QRect(0, 0, maxW - 2 * kPad, 10000),
                                     Qt::TextWordWrap, text);
        const int bodyW = qMin(maxW, need.width() + 2 * kPad);
        const int bodyH = need.height() + 2 * kPad;

        /* 默认挂在输入框下面，下面放不下就翻到上面去（尾巴跟着翻） */
        const QPoint tl = anchor->mapToGlobal(QPoint(0, 0));
        const QRect scr = screenRectFor(tl);
        m_below = (tl.y() + anchor->height() + bodyH + kTail) <= scr.bottom();

        int x = tl.x();
        /* 尾巴对着输入框左侧往里一点，别顶在角上 */
        m_tailX = qMin(24, anchor->width() / 2);
        if (x + bodyW > scr.right()) {
            x = scr.right() - bodyW;
            m_tailX = qBound(kPad, tl.x() + qMin(24, anchor->width() / 2) - x,
                             bodyW - kPad);
        }
        const int y = m_below ? (tl.y() + anchor->height())
                              : (tl.y() - bodyH - kTail);
        setFixedSize(bodyW, bodyH + kTail);
        move(x, y);
        show();
        raise();
        if (msec > 0) {
            m_hide.start(msec);
        } else {
            m_hide.stop();
        }
    }

    /** 自测：当前尺寸放得下当前文字吗（"显示不全"就是这条要防的）。 */
    bool fits() const
    {
        if (m_text.isEmpty()) {
            return true;
        }
        const QRect body(0, m_below ? kTail : 0, width(), height() - kTail);
        const QRect inner = body.adjusted(kPad, kPad, -kPad, -kPad);
        const QRect need = QFontMetrics(font())
                           .boundingRect(QRect(0, 0, inner.width(), 10000),
                                         Qt::TextWordWrap, m_text);
        return need.width() <= inner.width() && need.height() <= inner.height();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect body = m_below ? QRect(0, kTail, width(), height() - kTail)
                                   : QRect(0, 0, width(), height() - kTail);
        QPainterPath path;
        path.addRoundedRect(QRectF(body).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        /* 尾巴：朝着输入框那一侧伸出去 */
        QPolygonF tail;
        if (m_below) {
            tail << QPointF(m_tailX, kTail) << QPointF(m_tailX + kTail * 2, kTail)
                 << QPointF(m_tailX + kTail, 0.5);
        } else {
            tail << QPointF(m_tailX, height() - kTail)
                 << QPointF(m_tailX + kTail * 2, height() - kTail)
                 << QPointF(m_tailX + kTail, height() - 0.5);
        }
        path.addPolygon(tail);
        p.setPen(QPen(QColor(0xC0, 0x39, 0x2B), 1));
        p.setBrush(QColor(0xFF, 0xF4, 0xE5));
        p.drawPath(path.simplified());
        p.setPen(QColor(0x8A, 0x2A, 0x1F));
        p.drawText(body.adjusted(kPad, kPad, -kPad, -kPad),
                   Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_text);
    }

    void mousePressEvent(QMouseEvent *) override { hide(); }   // 点一下就收

private:
    static QRect screenRectFor(const QPoint &g)
    {
        if (const QScreen *s = QGuiApplication::screenAt(g)) {
            return s->availableGeometry();
        }
        return QGuiApplication::primaryScreen()
               ? QGuiApplication::primaryScreen()->availableGeometry()
               : QRect(0, 0, 1920, 1080);
    }

    static const int kPad = 8;
    static const int kTail = 7;
    QString  m_text;
    QWidget *m_anchor = nullptr;
    bool     m_below = true;
    int      m_tailX = 12;
    QTimer   m_hide;
};

class NoWheelFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() != QEvent::Wheel) {
            return QObject::eventFilter(o, e);
        }
        auto *w = qobject_cast<QWidget *>(o);
        if (!w) {
            return QObject::eventFilter(o, e);
        }
        /* 往上找滚动区，把滚轮让给它 */
        for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
            if (auto *sa = qobject_cast<QAbstractScrollArea *>(p)) {
                QCoreApplication::sendEvent(sa->viewport(), e);
                return true;
            }
        }
        return true;                 // 没有滚动区就直接吃掉，总之别改值
    }
};

/** 给 root 底下所有数值框/下拉框装上"滚轮不改值"。 */
static void applyNoWheel(QWidget *root)
{
    if (!root) {
        return;
    }
    static NoWheelFilter *filter = new NoWheelFilter;
    const auto spins = root->findChildren<QAbstractSpinBox *>();
    for (QAbstractSpinBox *w : spins) {
        w->installEventFilter(filter);
        /* 顺手把焦点策略收紧：默认 WheelFocus 会让滚轮先抢焦点 */
        w->setFocusPolicy(Qt::StrongFocus);
    }
    const auto combos = root->findChildren<QComboBox *>();
    for (QComboBox *w : combos) {
        w->installEventFilter(filter);
        w->setFocusPolicy(Qt::StrongFocus);
    }
}

/* 属性区里数值/枚举一律等宽字体 —— 原厂就是这个观感（截图里
 * ALIGN_CENTER / ELM_FLAG_NORMAL / SCROLL 都是等宽的）。 */
/* 原厂属性区的数值/枚举是等宽的老式点阵观感（ALIGN_CENTER / ELM_FLAG_NORMAL /
 * BACKLIGHT_VALUE_LIST），对应 Windows 上的 Courier New；Consolas 太"现代"，
 * 字形对不上。 */
static QFont monoFont()
{
    QFont f(QStringLiteral("Courier New"));
    f.setStyleHint(QFont::TypeWriter);
    f.setPointSize(9);
    return f;
}

/* ===================== BaseScrollArea ===================== */

BaseScrollArea::BaseScrollArea(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::StyledPanel);
}

BaseScrollArea::~BaseScrollArea() = default;

/* ===================== DragButton ===================== */

DragButton::DragButton(QWidget *parent)
    : QPushButton(parent)
{
}

DragButton::~DragButton() = default;

void DragButton::mousePressEvent(QMouseEvent *e)
{
    m_press = e->pos();
    QPushButton::mousePressEvent(e);
}

void DragButton::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton) || m_type.isEmpty()) {
        QPushButton::mouseMoveEvent(e);
        return;
    }
    if ((e->pos() - m_press).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    auto *mime = new QMimeData;
    mime->setData(QLatin1String(EditorOps::kControlMime),
                  (m_cls + QLatin1Char('|') + m_type).toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);

    /* 【拖影】不给 pixmap 的话，鼠标底下什么都没有，用户根本看不出自己正在
     * 拖东西 —— 只有指针形状变一下。这里拿按钮自己的样子做拖影：控件列表的
     * 按钮就是纯文字的（原厂 control.json 里引用的 config\images\*.ico
     * 在原厂目录里压根不存在，所以原厂按钮也没图标），拖着一个写着"垂直列表"
     * 的小方块跟着鼠标走，一眼就知道拖的是什么。
     * 半透明是为了不挡住底下的落点。 */
    QPixmap shot = grab();
    QPixmap ghost(shot.size());
    ghost.fill(Qt::transparent);
    {
        QPainter gp(&ghost);
        gp.setOpacity(0.75);
        gp.drawPixmap(0, 0, shot);
        gp.setOpacity(1.0);
        gp.setPen(QPen(QColor(0x2b, 0x7d, 0xd1), 1));
        gp.drawRect(QRect(QPoint(0, 0), shot.size() - QSize(1, 1)));
    }
    drag->setPixmap(ghost);
    drag->setHotSpot(m_press);          // 抓哪儿就跟哪儿，不会跳一下

    drag->exec(Qt::CopyAction);
    /* 拖完把按钮的按下态复位：拖拽期间不会收到 release，不复位它会一直凹着 */
    setDown(false);
}

/* ===================== 取值范围 =====================
 *
 * 原厂对控件参数是**数据驱动**地限范围的：范围写在 control.json 的属性里
 * （min / max / maxlength），界面按它建编辑器。两条提示语能佐证：
 *
 *     0xc94de8  "请输入%1~%2的整数"     —— 属性写了 min/max 时用它
 *     0xc94e44  "请输入 0~9999 内的整数" —— 没写时的兜底
 *
 * 实际数据（control.json 183 条属性里带约束的）：
 *     int8   highlight / z_order / highlight_index   min=0  max=128
 *     int16  cent_x / cent_y                         min=0  max=32768
 *     text-str  source / code   maxlength=8 ；format maxlength=16
 *     text-pic  str             maxlength=100
 *     piclist   normal_image    maxlength=30 ；image/charge_image maxlength=0(不限)
 *     arrlist   number/delimiter maxlength=10 ；space maxlength=2
 */

/** 属性声明的类型能装下的范围。0 表示"这个类型没有天然上限"。 */
static void typeBound(const QString &ptype, int *lo, int *hi)
{
    if (ptype == QLatin1String("int8")) {
        *lo = 0;
        *hi = 255;
    } else if (ptype == QLatin1String("int16")) {
        *lo = 0;
        *hi = 32768;
    } else {
        *lo = 0;
        *hi = 9999;
    }
}

/**
 * 按原厂规则给整数编辑器定范围，并挂上原厂那句提示。
 *
 * @param po    属性的 json（要读 min / max / -type）
 * @param allowNegative 允许负值（坐标 X/Y 用 —— oled 工程里真有 y=-11，
 *                      钳成 0 会把用户已有的布局改掉）
 */
static void applyIntRange(QSpinBox *s, const QJsonObject &po, bool allowNegative = false)
{
    const QString ptype = po.value(QStringLiteral("-type")).toString();
    int lo = 0, hi = 9999;
    typeBound(ptype, &lo, &hi);

    const QJsonValue mn = po.value(QStringLiteral("min"));
    const QJsonValue mx = po.value(QStringLiteral("max"));
    const bool declared = !mn.isUndefined() || !mx.isUndefined();
    if (!mn.isUndefined()) {
        lo = mn.toInt();
    }
    if (!mx.isUndefined()) {
        hi = mx.toInt();
    }
    if (allowNegative && lo >= 0 && !declared) {
        lo = -9999;             // 坐标：模板没写下限就不设下限
    }
    if (hi < lo) {
        hi = lo;
    }
    s->setRange(lo, hi);
    /* 原厂这两句是干巴巴的 "请输入%1~%2的整数" / "请输入 0~9999 内的整数"，
     * 只说数字不说来历。这里补一句上限是哪来的 —— 模板写了就说模板，
     * 没写就说是按声明的类型兜的底，出问题时好定位。 */
    s->setToolTip(declared
                  ? QStringLiteral("请输入 %1 ~ %2 的整数\n"
                                   "范围来自控件模板 control.json 里这条属性的 min/max")
                        .arg(lo).arg(hi)
                  : QStringLiteral("请输入 %1 ~ %2 的整数\n"
                                   "模板没写范围，这里按声明的类型 %3 兜底")
                        .arg(lo).arg(hi)
                        .arg(ptype.isEmpty() ? QStringLiteral("(未声明)") : ptype));
}

/* ===================== FileEdit ===================== */

/** 按钮上那张缩略图的外框；比它大的按比例缩，小的保持原尺寸。 */
static const QSize kBgThumbMax(96, 28);

FileEdit::FileEdit(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    m_main = new QPushButton(QStringLiteral("背景图片"), this);
    /* 缩略图有高度，按钮得留得下，不然图被压扁 */
    m_main->setMinimumHeight(kBgThumbMax.height() + 6);
    m_pick = new QPushButton(this);
    m_pick->setIcon(QIcon(QStringLiteral(":/icon/icons/fileopen.png")));
    m_pick->setFixedWidth(34);
    m_clear = new QPushButton(this);
    m_clear->setIcon(QIcon(QStringLiteral(":/icon/icons/act_del.png")));
    m_clear->setFixedWidth(24);
    m_clear->setToolTip(QStringLiteral("删除背景图片"));
    m_clear->setEnabled(false);
    lay->addWidget(m_main, 1);
    lay->addWidget(m_pick, 0);
    lay->addWidget(m_clear, 0);

    /* 【必须用原厂那个弹窗，不能用系统文件对话框】
     *
     * 原厂点"背景图片"弹的是 ImageListView（标题"图片编辑(双击选中图片并更新
     * 到控件)"，左目录树右缩略图，双击选中），和图片列表那个弹窗一个版式。
     *
     * 更要命的是路径形式：工程 json 里存的是**相对工程目录**的
     * "config/pic_lcd/v_block.bmp"，而 QFileDialog 给的是绝对路径 ——
     * 存进去之后预览找不到图、ResBuilder 也收不到这张图（StyBuilder 是按
     * 相对路径去 picId 表里查号的），等于设了个寂寞。 */
    auto choose = [this]() {
        const PropertyContext &ctx = PropertyContext::instance();
        ImageListView dlg(this);
        dlg.setProjectDir(ctx.projectDir);
        dlg.setSelected(m_path);
        if (dlg.exec() != QDialog::Accepted || dlg.selected().isEmpty()) {
            return;
        }
        setFilePath(dlg.selected());
    };
    connect(m_clear, &QPushButton::clicked, this, [this]() { setFilePath(QString()); });
    connect(m_main, &QPushButton::clicked, this, choose);
    connect(m_pick, &QPushButton::clicked, this, choose);
}

FileEdit::~FileEdit() = default;

void FileEdit::setCaption(const QString &c)
{
    m_caption = c;
    refreshFace();
}

void FileEdit::setFilePath(const QString &p)
{
    if (m_path == p) {
        return;
    }
    m_path = p;
    refreshFace();
    emit filePathChanged(p);
}

/**
 * 按钮上的样子：**选了图就显示这张图的缩略图**，和图片列表那边一致；
 * 没选就是原来的"背景图片"四个字。
 *
 * 【为什么不显示文件名】原厂就是直接把图画在按钮上 —— 点阵屏的图大多是
 * v_block / A0007JL 这类没有语义的名字，看名字根本认不出是哪张。
 */
void FileEdit::refreshFace()
{
    m_clear->setEnabled(!m_path.isEmpty());   // 手册：删完箭头变灰
    if (m_path.isEmpty()) {
        m_main->setIcon(QIcon());
        m_main->setText(m_caption);
        m_main->setToolTip(QStringLiteral("还没有背景图片，点一下从图片目录里选一张"));
        return;
    }
    const PropertyContext &ctx = PropertyContext::instance();
    const QString abs = QFileInfo(m_path).isAbsolute()
                        ? m_path : QDir(ctx.projectDir).filePath(m_path);
    QPixmap pm(abs);
    if (pm.isNull()) {
        /* 图丢了：明说，别装作没事 —— 这种情况生成资源时也会缺图 */
        m_main->setIcon(QIcon());
        m_main->setText(QStringLiteral("? ") + QFileInfo(m_path).fileName());
        m_main->setToolTip(QStringLiteral("找不到这张图：%1").arg(abs));
        return;
    }
    /* 按钮就那么高，太大的按比例缩；小图保持原尺寸，别放大糊掉 */
    const QSize cap(kBgThumbMax);
    if (pm.width() > cap.width() || pm.height() > cap.height()) {
        pm = pm.scaled(cap, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    m_main->setText(QString());
    m_main->setIcon(QIcon(pm));
    m_main->setIconSize(pm.size());
    m_main->setToolTip(m_path);
}

/* ===================== Backgroud ===================== */

Backgroud::Backgroud(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    m_main = new QPushButton(QStringLiteral("背景颜色"), this);
    m_pick = new QPushButton(this);
    m_pick->setIcon(QIcon(QStringLiteral(":/icon/icons/gradient.png")));
    m_pick->setFixedWidth(34);
    /* 原厂截图里"背景颜色"右边还有一个小按钮，就是手册 2.3 说的那个
     * "删除背景颜色"的箭头：删掉之后它自己变灰。 */
    m_clear = new QPushButton(this);
    m_clear->setIcon(QIcon(QStringLiteral(":/icon/icons/act_del.png")));
    m_clear->setFixedWidth(24);
    m_clear->setToolTip(QStringLiteral("删除背景颜色"));
    lay->addWidget(m_main, 1);
    lay->addWidget(m_pick, 0);
    lay->addWidget(m_clear, 0);

    auto choose = [this]() {
        const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : QColor(Qt::black),
                                                this, tr("背景颜色"));
        if (c.isValid()) {
            setColor(c);
        }
    };
    connect(m_main, &QPushButton::clicked, this, choose);
    connect(m_pick, &QPushButton::clicked, this, choose);
    connect(m_clear, &QPushButton::clicked, this, [this]() {
        setColor(QColor());            // 无效色 = 没有背景色
        emit colorCleared();
    });
}

Backgroud::~Backgroud() = default;

void Backgroud::setColor(const QColor &c)
{
    m_color = c;
    if (c.isValid()) {
        m_pick->setStyleSheet(QStringLiteral("background:%1;").arg(c.name()));
    } else {
        m_pick->setStyleSheet(QString());
    }
    /* 手册："背景颜色被删除后，箭头变为灰色不可选状态" */
    m_clear->setEnabled(c.isValid());
    emit colorChanged(c);
}

/* ===================== Position ===================== */

Position::Position(QWidget *parent)
    : QGroupBox(tr("位置坐标"), parent)
{
    auto *form = new QFormLayout(this);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setContentsMargins(6, 4, 6, 6);
    form->setVerticalSpacing(4);

    auto mkSpin = [this](const char *objName) {
        auto *s = new QSpinBox(this);
        s->setObjectName(QLatin1String(objName));
        s->setFont(monoFont());
        return s;
    };
    m_x = mkSpin("spinX");
    m_y = mkSpin("spinY");
    m_w = mkSpin("spinW");
    m_h = mkSpin("spinH");
    /* 父级未知时的兜底范围。真正的范围由 setBounds() 按原厂规则重设 ——
     * 面板每次铺开都会调一次，所以这里只要不挡路就行。 */
    m_x->setRange(-999, 999);
    m_y->setRange(-999, 999);
    m_w->setRange(0, 9999);
    m_h->setRange(0, 9999);
    form->addRow(tr("X:"), m_x);
    form->addRow(tr("Y:"), m_y);
    form->addRow(tr("宽度:"), m_w);
    form->addRow(tr("高度:"), m_h);

    for (QSpinBox *s : { m_x, m_y, m_w, m_h }) {
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (!m_loading) {
                emit rectEdited(rect());
            }
        });
    }
}

Position::~Position() = default;

void Position::setRect(const QRect &r)
{
    m_loading = true;
    m_x->setValue(r.x());
    m_y->setValue(r.y());
    m_w->setValue(r.width());
    m_h->setValue(r.height());
    m_loading = false;
}

QRect Position::rect() const
{
    return QRect(m_x->value(), m_y->value(), m_w->value(), m_h->value());
}

void Position::setBounds(const QSize &parentSize, const QSize &ownSize, bool container)
{
    /* 【顺序照抄原厂】先无条件给宽高定上限，再按类型分叉定 X/Y。
     * 上限用的是父容器的宽高，"自身宽/高"取的是节点里存着的值 ——
     * 原厂在这一步还没往框里填值，读的就是模型。 */
    const bool known = parentSize.isValid()
                       && parentSize.width() > 0 && parentSize.height() > 0;
    m_loading = true;
    if (!known) {
        m_x->setRange(-999, 999);
        m_y->setRange(-999, 999);
        m_w->setRange(0, 9999);
        m_h->setRange(0, 9999);
        for (QSpinBox *sp : { m_x, m_y, m_w, m_h }) {
            sp->setToolTip(tr("找不到父容器，暂时不限范围"));
        }
        m_loading = false;
        return;
    }

    const int pw = parentSize.width();
    const int ph = parentSize.height();
    /* 宽高的下限原厂从没设过，QSpinBox 默认就是 0 —— 照抄，不自作主张改成 1。 */
    m_w->setRange(0, pw);
    m_h->setRange(0, ph);
    m_w->setToolTip(tr("宽度：%1 ~ %2\n上限是父容器的宽度，控件不能比装它的容器还宽")
                    .arg(m_w->minimum()).arg(pw));
    m_h->setToolTip(tr("高度：%1 ~ %2\n上限是父容器的高度，控件不能比装它的容器还高")
                    .arg(m_h->minimum()).arg(ph));

    if (container) {
        /* NewLayout / NewLayer：位置不受父容器约束。
         * 这不是宽松处理，是原厂就这么写的 —— 垂直列表的行本身是 NewLayout，
         * 它们的 y 要能排到父容器高度之外（实测原厂工程里 y 排到 96、父高 48），
         * 钳进去列表就没法多于一屏。 */
        m_x->setRange(-999, 999);
        m_y->setRange(-999, 999);
        const QString why = tr("%1：%2 ~ %3\n"
                               "图层和布局的位置不受父容器限制 —— "
                               "垂直列表的每一行都是一个布局，"
                               "要靠 Y 排到父容器下边缘之外，列表才能多于一屏");
        m_x->setToolTip(why.arg(tr("X")).arg(m_x->minimum()).arg(m_x->maximum()));
        m_y->setToolTip(why.arg(tr("Y")).arg(m_y->minimum()).arg(m_y->maximum()));
    } else {
        /* 叶子控件：整个矩形必须留在父容器里。
         * 原厂是 setMaximum(父宽-自身宽) 之后再 setMinimum(0)；宽度大于父宽时
         * 上限算出来是负数，Qt 会把区间收成 [0,0]，这里直接 qMax 到 0，等价。 */
        m_x->setRange(0, qMax(0, pw - qMax(0, ownSize.width())));
        m_y->setRange(0, qMax(0, ph - qMax(0, ownSize.height())));
        const QString why = tr("%1：%2 ~ %3\n"
                               "上限 = 父容器%4 %5 − 本控件%4 %6，"
                               "整块必须留在父容器里；把%4改小，这里就能挪得更远");
        m_x->setToolTip(why.arg(tr("X")).arg(m_x->minimum()).arg(m_x->maximum())
                        .arg(tr("宽")).arg(pw).arg(qMax(0, ownSize.width())));
        m_y->setToolTip(why.arg(tr("Y")).arg(m_y->minimum()).arg(m_y->maximum())
                        .arg(tr("高")).arg(ph).arg(qMax(0, ownSize.height())));
    }
    m_loading = false;
}

/* ===================== Border ===================== */

Border::Border(QWidget *parent)
    : QGroupBox(tr("内边框线"), parent)
{
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(6, 4, 6, 6);
    box->setSpacing(4);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(4);

    auto mkSpin = [this](const QString &side) {
        auto *s = new QSpinBox(this);
        s->setRange(0, 255);
        s->setFont(monoFont());
        /* 0~255 不是随手定的：四条边各占 element_css 里的 1 个字节
         * （StyBuilder 写在 css+28..31），u8 装不下更大的值。 */
        s->setToolTip(tr("%1边框宽度：0 ~ 255\n"
                         "四条边各占资源里的 1 个字节，填 0 就是这条边不画")
                      .arg(side));
        return s;
    };
    m_l = mkSpin(tr("左"));
    m_t = mkSpin(tr("上"));
    m_r = mkSpin(tr("右"));
    m_b = mkSpin(tr("下"));
    form->addRow(tr("左:"), m_l);
    form->addRow(tr("上:"), m_t);
    form->addRow(tr("右:"), m_r);
    form->addRow(tr("下:"), m_b);
    box->addLayout(form);

    auto *row = new QHBoxLayout;
    row->setSpacing(2);
    m_btn = new QPushButton(tr("边框"), this);
    m_pick = new QPushButton(this);
    m_pick->setIcon(QIcon(QStringLiteral(":/icon/icons/gradient.png")));
    m_pick->setFixedWidth(34);
    row->addWidget(m_btn, 1);
    row->addWidget(m_pick, 0);
    /* 单色屏下这两个都藏起来，换成"画/不画" —— 固件 jlui_draw_rect 在 MONO 下
     * 只判 color != RECT_MONO_CLR，颜色本身不起作用。 */
    m_mono = new QComboBox(this);
    m_mono->setFont(monoFont());
    m_mono->addItem(QStringLiteral("画边框"));
    m_mono->addItem(QStringLiteral("不画"));
    m_mono->setVisible(false);
    row->addWidget(m_mono, 1);
    connect(m_mono, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                if (!m_loading) {
                    emit borderVisibilityEdited(i == 0);
                }
            });
    box->addLayout(row);

    for (QSpinBox *s : { m_l, m_t, m_r, m_b }) {
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (!m_loading) {
                emit valueEdited(m_l->value(), m_t->value(), m_r->value(), m_b->value());
            }
        });
    }
}

Border::~Border() = default;

void Border::setMonoMode(bool on, bool visible)
{
    m_loading = true;
    m_btn->setVisible(!on);
    m_pick->setVisible(!on);
    m_mono->setVisible(on);
    m_mono->setCurrentIndex(visible ? 0 : 1);
    m_loading = false;
}

void Border::setValues(int l, int t, int r, int b)
{
    m_loading = true;
    m_l->setValue(l);
    m_t->setValue(t);
    m_r->setValue(r);
    m_b->setValue(b);
    m_loading = false;
}

/* ===================== BaseProperty ===================== */

BaseProperty::BaseProperty(QWidget *parent)
    : QWidget(parent)
{
    m_box = new QVBoxLayout(this);
    m_box->setContentsMargins(6, 4, 6, 4);
    m_box->setSpacing(3);
}

BaseProperty::~BaseProperty() = default;

void BaseProperty::showNode(UiNode *n)
{
    m_node = n;
}

/* ===================== CssProperty ===================== */

static QComboBox *labeledCombo(QVBoxLayout *box, QWidget *owner,
                               const QString &caption, const QStringList &items)
{
    auto *lab = new QLabel(caption, owner);
    auto *cb = new QComboBox(owner);
    cb->addItems(items);
    cb->setFont(monoFont());
    box->addWidget(lab);
    box->addWidget(cb);
    return cb;
}

CssProperty::CssProperty(QWidget *parent)
    : BaseProperty(parent)
{
}

CssProperty::~CssProperty() = default;

/**
 * 把面板上的一次改动写回 element_css.struct[m_state]。
 *
 * 【为什么要有这么一个东西】之前这一页除了"位置坐标"，其余每一行都只
 * 显示不回写 —— 对齐方式、默认隐藏、标志、背景色、背景图、边框、滚动方式
 * 改了都不落盘，存出来还是老值。加载时 setXxx() 也会触发信号，所以要用
 * m_loading 挡一道，否则一选中控件就把自己标脏。
 */
void CssProperty::commitCss(const QString &propName, const QString &key,
                            const QJsonValue &value)
{
    if (m_loading || !m_node) {
        return;
    }
    if (m_node->setCssField(m_state, propName, key, value)) {
        emit nodeEdited(m_node);
    }
}

void CssProperty::clearRows()
{
    m_pos = nullptr;
    while (m_box->count() > 0) {
        QLayoutItem *it = m_box->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }
}

/* 这一页完全由数据驱动：element_css.struct[state] 里有什么属性就铺什么，
 * caption / -type / enum / 默认值全来自工程 json（源头是 control.json）。
 * 原厂那一列"对齐方式 / 默认隐藏 / 位置坐标 / 背景颜色 / 背景图片 /
 * 内边框线"就是这么来的 —— 写死反而会和别的控件对不上。 */
void CssProperty::showNode(UiNode *n)
{
    BaseProperty::showNode(n);
    clearRows();
    setEnabled(n != nullptr);
    if (!n) {
        return;
    }
    /* 铺面板时 setValue/setCurrentIndex 都会发信号，不挡住的话一选中控件
     * 就把它标脏，往返自检立刻不过。 */
    m_loading = true;

    const QJsonArray st = n->cssState(m_state);
    for (const QJsonValue &pv : st) {
        const QJsonObject po = pv.toObject();
        const QString pname = po.value(QStringLiteral("-name")).toString();
        const QString ptype = po.value(QStringLiteral("-type")).toString();
        const QString cap = po.value(QStringLiteral("caption")).toString();
        const QJsonValue def = po.value(QStringLiteral("default"));

        if (ptype == QLatin1String("rect")) {
            m_pos = new Position(this);
            m_pos->setTitle(cap.isEmpty() ? tr("位置坐标") : cap);
            /* 【先定范围再填值】原厂就是这个次序（先 setMaximum 再 setValue）。
             * 反过来的话，值会被上一次的旧范围夹一道。 */
            const QRect own = n->rectOf(m_state);
            const bool container = (n->type == QLatin1String("NewLayout")
                                    || n->type == QLatin1String("NewLayer"));
            m_pos->setBounds(n->parent ? n->parent->rectOf(0).size() : QSize(),
                             own.size(), container);
            m_pos->setRect(own);
            m_box->addWidget(m_pos);
            connect(m_pos, &Position::rectEdited, this, [this](const QRect &r) {
                if (!m_node) {
                    return;
                }
                m_node->setRectOf(m_state, r);
                emit nodeEdited(m_node);
            });
        } else if (ptype == QLatin1String("background-color")) {
            const QString cur = po.value(QStringLiteral("background-color")).toString();
            if (Preview::isMonoLayer(n)) {
                /* 【单色屏不给取色器】屏上只有亮/灭。固件 jlui_fill_rect 在 MONO
                 * 下只认一个值：颜色 == 0x555AAA 才填充，其余**一律清除**。
                 * 也就是说这里放个 RGB 取色器纯属误导 —— 挑了半天绿色，设备上
                 * 只有"不填充"一种结果。实测这套工程 21 种背景色取值里，只有
                 * #ff555aaa（8 处）是有效果的。
                 * 换成两选一，底下写的还是同一个魔数，产物一个字节不变。 */
                m_box->addWidget(new QLabel(cap.isEmpty() ? QStringLiteral("背景") : cap, this));
                auto *cb = new QComboBox(this);
                cb->setFont(monoFont());
                cb->addItem(QStringLiteral("不填充"));
                cb->addItem(QStringLiteral("填充"));
                cb->setCurrentIndex(Preview::fillOf(cur) == Preview::MonoFill::Set ? 1 : 0);
                cb->setToolTip(QStringLiteral(
                    "单色屏只有亮/灭。填充 = 写 %1（固件 BGC_MONO_SET），"
                    "不填充 = 清空").arg(QLatin1String(Preview::kMonoFillOn)));
                m_box->addWidget(cb);
                connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, pname](int i) {
                            commitCss(pname, QStringLiteral("background-color"),
                                      i == 1 ? QString::fromLatin1(Preview::kMonoFillOn)
                                             : QString());
                        });
            } else {
                auto *w = new Backgroud(this);
                w->setColor(QColor(cur));
                m_box->addWidget(w);
                connect(w, &Backgroud::colorChanged, this, [this, pname](const QColor &c) {
                    /* 空串 = 没有背景色 */
                    commitCss(pname, QStringLiteral("background-color"),
                              c.isValid() ? c.name() : QString());
                });
            }
        } else if (ptype == QLatin1String("background-image")) {
            /* 标题单独一行 —— 按钮上要腾出来放缩略图，不能再写字了。
             * 旁边"坐标/背景颜色/边框"每组也都是标题在上、控件在下。 */
            m_box->addWidget(new QLabel(cap.isEmpty() ? QStringLiteral("背景图片") : cap,
                                        this));
            auto *w = new FileEdit(this);
            w->setCaption(QStringLiteral("点击选择图片"));
            w->setFilePath(po.value(QStringLiteral("background-image")).toString());
            m_box->addWidget(w);
            connect(w, &FileEdit::filePathChanged, this, [this, pname](const QString &f) {
                commitCss(pname, QStringLiteral("background-image"), f);
            });
        } else if (ptype == QLatin1String("border")) {
            auto *w = new Border(this);
            w->setTitle(cap.isEmpty() ? QStringLiteral("内边框线") : cap);
            const QJsonObject b = po.value(QStringLiteral("border")).toObject();
            w->setValues(b.value(QStringLiteral("left")).toInt(),
                         b.value(QStringLiteral("top")).toInt(),
                         b.value(QStringLiteral("right")).toInt(),
                         b.value(QStringLiteral("bottom")).toInt());
            m_box->addWidget(w);
            {
                const QString bc = po.value(QStringLiteral("color")).toString();
                w->setMonoMode(Preview::isMonoLayer(n), Preview::borderVisible(bc));
            }
            connect(w, &Border::borderVisibilityEdited, this, [this, pname](bool vis) {
                /* 画 = 写普通颜色（点亮），不画 = 写那个魔数 */
                commitCss(pname, QStringLiteral("color"),
                          QString::fromLatin1(vis ? Preview::kMonoLit
                                                  : Preview::kMonoFillOn));
            });
            connect(w, &Border::valueEdited, this,
                    [this, pname](int l, int t, int r, int bo) {
                        QJsonObject j;
                        j.insert(QStringLiteral("left"), l);
                        j.insert(QStringLiteral("top"), t);
                        j.insert(QStringLiteral("right"), r);
                        j.insert(QStringLiteral("bottom"), bo);
                        commitCss(pname, QStringLiteral("border"), j);
                    });
        } else if (po.contains(QStringLiteral("enum"))) {
            QStringList items;
            for (const QJsonValue &ev : po.value(QStringLiteral("enum")).toArray()) {
                const QJsonObject eo = ev.toObject();
                for (auto it = eo.begin(); it != eo.end(); ++it) {
                    items << it.key();
                }
            }
            QComboBox *cb = labeledCombo(m_box, this, cap.isEmpty() ? pname : cap, items);
            const int i = cb->findText(def.toString());
            if (i >= 0) {
                cb->setCurrentIndex(i);
            }
            connect(cb, &QComboBox::currentTextChanged, this,
                    [this, pname](const QString &t) {
                        commitCss(pname, QStringLiteral("default"), t);
                    });
        } else if (def.isDouble()) {
            m_box->addWidget(new QLabel(cap.isEmpty() ? pname : cap, this));
            auto *s = new QSpinBox(this);
            applyIntRange(s, po);
            s->setFont(monoFont());
            s->setValue(def.toInt());
            m_box->addWidget(s);
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this, pname](int v) {
                        commitCss(pname, QStringLiteral("default"), v);
                    });
        } else if (!pname.isEmpty()) {
            m_box->addWidget(new QLabel(cap.isEmpty() ? pname : cap, this));
            auto *e = new QLineEdit(def.toString(), this);
            e->setFont(monoFont());
            m_box->addWidget(e);
            connect(e, &QLineEdit::editingFinished, this, [this, pname, e]() {
                commitCss(pname, QStringLiteral("default"), e->text());
            });
        }
    }
    m_box->addStretch();
    applyNoWheel(this);          // 滚轮别改值，见 NoWheelFilter
    m_loading = false;
}

PropertyContext &PropertyContext::instance()
{
    static PropertyContext ctx;
    return ctx;
}

/* ===================== ComProperty ===================== */

ComProperty::ComProperty(QWidget *parent)
    : BaseProperty(parent)
{
    m_box->addWidget(new QLabel(tr("ID号"), this));
    m_id = new QLineEdit(this);
    m_id->setFont(monoFont());
    m_box->addWidget(m_id);

    /* m_dyn 不挂进自己的布局：原厂它排在 CSS属性 页签下面，
     * 由第二列的 dock 通过 dynamicSection() 取走摆放。 */
    m_dyn = new QWidget;
    m_dynForm = new QFormLayout(m_dyn);
    m_dynForm->setContentsMargins(6, 2, 6, 2);
    m_dynForm->setVerticalSpacing(3);

    connect(m_id, &QLineEdit::editingFinished, this, [this]() {
        if (!m_node) {
            return;
        }
        for (UiProperty &p : m_node->props) {
            if (p.name == QLatin1String("id")) {
                p.ename = m_id->text();
                p.dirty = true;
                m_node->markDirty();
                emit nodeEdited(m_node);
                break;
            }
        }
    });
}

ComProperty::~ComProperty() = default;

CalloutTip *ComProperty::tip() const
{
    /* 全进程一个就够：同一时刻只会指着一个输入框说话。 */
    static CalloutTip *s_tip = new CalloutTip;
    return s_tip;
}

bool ComProperty::warningTipFitsForTest()
{
    if (m_tipText.isEmpty() || !m_tipAnchor) {
        return true;
    }
    tip()->showFor(m_tipAnchor, m_tipText, 0);
    const bool ok = tip()->fits();
    tip()->hide();
    return ok;
}

void ComProperty::setWarning(QLabel *mark, QWidget *anchor, const QString &msg)
{
    if (!mark) {
        return;
    }
    if (msg.isEmpty()) {
        mark->clear();
        mark->setStyleSheet(QString());
        mark->setToolTip(QString());
        if (m_tipAnchor == anchor) {
            m_tipText.clear();
            m_tipAnchor = nullptr;
            tip()->hide();
        }
        return;
    }
    mark->setText(QStringLiteral("⚠"));
    mark->setStyleSheet(QStringLiteral("color:#c00000;font-weight:bold"));
    mark->setToolTip(msg);
    m_tipText = msg;
    m_tipAnchor = anchor;
    /* 无人值守跑测试时不弹窗，免得顶层窗口干扰截图/自检 */
    if (!EditorOps::silent()) {
        tip()->showFor(anchor, msg);
    }
}

QPixmap ComProperty::warningTipPixmapForTest()
{
    if (m_tipText.isEmpty() || !m_tipAnchor) {
        return QPixmap();
    }
    tip()->showFor(m_tipAnchor, m_tipText, 0);
    const QPixmap pm = tip()->grab();
    tip()->hide();
    return pm;
}

bool ComProperty::eventFilter(QObject *o, QEvent *e)
{
    /* 面板上那个 ⚠：点一下（或鼠标移上去）把气泡再叫出来 —— 气泡会自动收，
     * 收了之后总得有办法看回去。 */
    if ((e->type() == QEvent::MouseButtonPress || e->type() == QEvent::Enter)
        && !m_tipText.isEmpty() && m_tipAnchor) {
        if (auto *w = qobject_cast<QWidget *>(o)) {
            if (w->toolTip() == m_tipText) {
                tip()->showFor(m_tipAnchor, m_tipText);
                return true;
            }
        }
    }
    return BaseProperty::eventFilter(o, e);
}

void ComProperty::clearDynamic()
{
    while (m_dynForm->count() > 0) {
        QLayoutItem *it = m_dynForm->takeAt(0);
        if (QWidget *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }
}

void ComProperty::typeIdForTest(const QString &text)
{
    if (!m_id) {
        return;
    }
    m_id->setFocus();
    m_id->setText(text);          // setText 不发 editingFinished，正是要模拟的状态
}

QString ComProperty::idTextForTest() const
{
    return m_id ? m_id->text() : QString();
}

void ComProperty::showNode(UiNode *n)
{
    BaseProperty::showNode(n);
    clearDynamic();
    setEnabled(n != nullptr);
    if (!n) {
        m_id->clear();
        return;
    }

    QString ename;
    for (const UiProperty &p : n->props) {
        if (p.name == QLatin1String("id")) {
            ename = p.ename;
        }
    }
    QSignalBlocker b(m_id);
    m_id->setText(ename);

    /* 除 id / rect 之外的属性按 control.json 的描述动态铺出来：
     * 有 enum[] 就给下拉框，是整数就给 spin，其余给单行编辑。
     * 原厂那两项 "滚动方式 / 默认高亮行号" 就是这么来的。 */
    for (int pi = 0; pi < n->props.size(); ++pi) {
        const UiProperty &p = n->props.at(pi);
        /* id 单独在最上面；element_css 归 CSS属性 页签管；
         * 剩下的（color_format / action / 滚动方式 / 默认高亮行号 …）铺这里 */
        if (p.name == QLatin1String("id") || p.name == QLatin1String("rect")
            || p.name == QLatin1String("element_css") || p.name.isEmpty()) {
            continue;
        }
        const QString cap = p.caption.isEmpty() ? p.name : p.caption;
        const QJsonValue def = p.raw.value(QStringLiteral("default"));
        const QJsonArray en = p.raw.value(QStringLiteral("enum")).toArray();

        /* 改一个属性 = 改它的 raw["default"]（列表类改 raw["list"]），
         * 然后置脏。回写时 ProjectModel 直接把 raw 原样吐回去，
         * 没建模的键一个都不会丢。 */
        /* 【按下标定位，不能按名字】同一个控件里会有**多条 -name 相同**的属性：
         * 文字控件的 property[4] 和 property[5] 都叫 "color"，靠 caption
         * 区分"文字颜色"和"高亮颜色"。按名字找的话永远命中第一条 —— 改高亮
         * 颜色会写进文字颜色，而且神不知鬼不觉。
         * 下游 StyBuilder 一直是按序号取的（propOf(n,"color",nth)，
         * colorAt(0)=文字 / colorAt(1)=高亮），这里必须对齐。 */
        const int pidx = pi;
        const QString pname2 = p.name;
        auto commit = [this, pidx, pname2](const std::function<void(QJsonObject &)> &f) {
            if (!m_node || pidx < 0 || pidx >= m_node->props.size()) {
                return;
            }
            UiProperty &q = m_node->props[pidx];
            if (q.name != pname2) {
                return;            // 面板和节点对不上了（重建过？），别乱写
            }
            f(q.raw);
            q.dirty = true;
            m_node->markDirty();
            emit nodeEdited(m_node);
        };

        if (p.type == QLatin1String("color")
            || p.type == QLatin1String("background-color")) {
            /* 【以前这一项是坏的】颜色类属性没有 "default" 键，掉进最后的兜底
             * 分支之后变成一个空白单行编辑框，改了还写不进去（写的是 default，
             * 而颜色存在 "color"/"background-color" 上）。
             *
             * 单色屏下换成语义三选一：固件 jlui_draw_text 在 MONO 下
             *     color == 0xAAA555 -> 先把整块点亮再把字画成灭（反显）
             *     color == 0x555AAA -> 字不显示
             *     其余              -> 字点亮
             * 彩屏（OSD16）图层才给真正的取色器。 */
            const QString ckey = (p.type == QLatin1String("color"))
                                 ? QStringLiteral("color")
                                 : QStringLiteral("background-color");
            const QString cur = p.raw.value(ckey).toString();
            if (Preview::isMonoLayer(n)) {
                auto *cb = new QComboBox(m_dyn);
                cb->setFont(monoFont());
                cb->addItem(QStringLiteral("点亮"));
                cb->addItem(QStringLiteral("反显"));
                cb->addItem(QStringLiteral("不显示"));
                switch (Preview::textModeOf(cur)) {
                case Preview::MonoText::Invert: cb->setCurrentIndex(1); break;
                case Preview::MonoText::Hidden: cb->setCurrentIndex(2); break;
                default:                        cb->setCurrentIndex(0); break;
                }
                cb->setToolTip(QStringLiteral("单色屏只有亮/灭，没有颜色可挑"));
                connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [commit, ckey](int i) {
                            const char *v = (i == 1) ? Preview::kMonoInvert
                                          : (i == 2) ? Preview::kMonoFillOn
                                                     : Preview::kMonoLit;
                            const QString val = QString::fromLatin1(v);
                            commit([&ckey, &val](QJsonObject &o) { o.insert(ckey, val); });
                        });
                m_dynForm->addRow(cap, cb);
            } else {
                auto *w = new Backgroud(m_dyn);
                w->setColor(QColor(cur));
                connect(w, &Backgroud::colorChanged, this,
                        [commit, ckey](const QColor &c) {
                            const QString val = c.isValid() ? c.name(QColor::HexArgb)
                                                            : QString();
                            commit([&ckey, &val](QJsonObject &o) { o.insert(ckey, val); });
                        });
                m_dynForm->addRow(cap, w);
            }
        } else if (p.name == QLatin1String("format")) {
            /* 【常用几种 + 自定义】格式本身是模板串，字面字符可以随便写，
             * 枚举不完 —— 但实际用到的就那么几种，所以下拉框里列常用的，
             * 最后留一项"自定义…"，选中它才放出文本框。
             * 预设不是拍脑袋列的，是把两个工程里实际出现过的取值统计出来的。
             *
             * 解析规则（都对着固件抄）：
             *   时间 ui_time.c:65   Y=4 位年，M/D/h/m/s 各 2 位，
             *                       **其余字符原样输出**，每个非数字吃一张分隔符图
             *   数字 ui_number.c:76 只认 %0Nd / %Nd / %d，最多两个，
             *                       写别的整个控件不显示
             *
             * 【真正会坑人的是分隔符不够】分隔符图片取不到（原厂是 0xffff）
             * 时固件**就地截断，后面全不画**，屏上只剩前半截，而且不报错。
             * 工程里就有一个：oled 的 时间_461 用 "h:m:s" 却一张分隔符都没配。
             * 所以下面那行实时算一遍、不够就用红字点出来。 */
            const bool isTime = (n->type == QLatin1String("Time"));
            const QString curFmt = def.toString();
            /* 【预设一律不带结尾的 '/'】原厂工程里有 "m:s/"、"%04d/" 这种写法，
             * 看着像"结束符"，其实**它什么也没做**：
             *   time_vsprintf / number_vsprintf 在循环之后无条件补终止符
             *       buf[i + 1] = 0xff; buf[i] = 0xff;      (ui_time.c:125)
             *   渲染那头是 while (id != 0x00ff && id != 0xffff)
             *                                              (ui_synthesis_oled.c:1120)
             * 也就是说串尾总会被终止，不需要在格式里写字符去结束它。
             * "m:s/" 能用只是因为 ':' 吃掉了唯一那张分隔符图，走到 '/' 时
             * delimiter[1] 是 0xffff 就地截断 —— 落到的还是同一个终止符。
             * 一旦给这个控件补上第 2 张分隔符图，"m:s/" 就会**多画一个符号**，
             * "m:s" 不会。所以预设不带它，免得照着抄出个潜伏的坑。
             * 工程里已经是 "m:s/" 的会落到"自定义"并原样保留，不替用户改。
             * 注意 "Y/M/D" 里的 '/' 是**真分隔符**（要配 2 张图），留着。 */
            const QStringList presets = isTime
                ? QStringList{ QStringLiteral("m:s"), QStringLiteral("h:m"),
                               QStringLiteral("h:m:s"), QStringLiteral("Y/M/D"),
                               QStringLiteral("M/D"), QStringLiteral("Y"),
                               QStringLiteral("M"), QStringLiteral("D"),
                               QStringLiteral("h"), QStringLiteral("m"),
                               QStringLiteral("s") }
                : QStringList{ QStringLiteral("%02d"), QStringLiteral("%03d"),
                               QStringLiteral("%04d"), QStringLiteral("%d"),
                               QStringLiteral("%02d/%02d"),
                               QStringLiteral("%03d.%01d") };

            auto *cb = new QComboBox(m_dyn);
            cb->setFont(monoFont());
            cb->addItems(presets);
            cb->addItem(QStringLiteral("自定义…"));
            auto *ed = new QLineEdit(curFmt, m_dyn);
            ed->setFont(monoFont());
            ed->setMaxLength(qMax(1, p.raw.value(QStringLiteral("maxlength")).toInt(16)));
            /* 工程里是预设之外的写法就直接落到"自定义"，把原值原样放进
             * 文本框 —— 绝不替用户改成某个近似的预设。 */
            const int hit = presets.indexOf(curFmt);
            cb->setCurrentIndex(hit >= 0 ? hit : presets.size());
            ed->setVisible(hit < 0);
            cb->setToolTip(isTime
                ? QStringLiteral(
                    "时间格式是模板串，可以自己写：\n"
                    "  Y=4 位年  M=月 D=日 h=时 m=分 s=秒（各 2 位）\n"
                    "  其它字符原样显示，每个都要吃一张「分隔符图片」\n"
                    "分隔符图片不够时，固件会从那里开始把后面全部截掉\n"
                    "结尾不用写 '/'：串尾固件总会自动终止；写了只会多吃一张\n"
                    "分隔符图，图配够了反而会多画一个符号出来")
                : QStringLiteral(
                    "数字格式只认这三种占位符，最多两个：\n"
                    "  %0Nd 补 0   %Nd 补空格（用「空格图片」）  %d 不补\n"
                    "写成别的（如 %0x）或超过两个，控件在屏上完全不显示"));

            /* 分隔符够不够，实时提示。
             * 【面板里只放一个 ⚠，正文走浮动气泡】属性栏两百来像素宽，
             * 整句话塞进去会被挤得显示不全。⚠ 是"这里有问题"的标记，
             * 正文由带尾巴的气泡指着输入框弹出来。 */
            auto *hint = new QLabel(m_dyn);
            hint->setCursor(Qt::WhatsThisCursor);
            auto refreshHint = [this, hint, cb, isTime]() {
                if (!m_node) {
                    return;
                }
                QString f;
                int have = 0;
                for (const UiProperty &q : m_node->props) {
                    if (q.name == QLatin1String("format")) {
                        f = q.raw.value(QStringLiteral("default")).toString();
                    } else if (q.name == QLatin1String("delimiter")) {
                        have = q.raw.value(QStringLiteral("list")).toArray().size();
                    }
                }
                /* 末尾那个 '/' 是原厂惯用的"到此为止"写法（靠分隔符耗尽
                 * 来截断），不算它缺图 */
                QString body = f;
                if (body.endsWith(QLatin1Char('/'))) {
                    body.chop(1);
                }
                int need = 0;
                if (isTime) {
                    for (const QChar c : body) {
                        if (!QStringLiteral("YMDhms").contains(c)) {
                            ++need;
                        }
                    }
                } else {
                    QString s = body;
                    s.remove(QRegularExpression(QStringLiteral("%0?[1-9]?d")));
                    need = s.size();
                }
                setWarning(hint, cb, need > have ? QStringLiteral(
                    "这个格式要 %1 张「分隔符图片」，只配了 %2 张。\n"
                    "固件取不到图就地截断，屏上会从缺图那里开始整段不显示，"
                    "而且不会报错。").arg(need).arg(have) : QString());
            };
            /* 气泡自动收了之后，点一下 ⚠ 还能再叫出来 */
            hint->installEventFilter(this);
            ed->setToolTip(cb->toolTip());
            auto apply = [commit, refreshHint](const QString &t) {
                commit([&t](QJsonObject &o) {
                    o.insert(QStringLiteral("default"), t);
                });
                refreshHint();
            };
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [apply, ed, presets](int i) {
                        if (i >= 0 && i < presets.size()) {
                            ed->setVisible(false);
                            ed->setText(presets.at(i));
                            apply(presets.at(i));
                            return;
                        }
                        /* 选"自定义…"：放出文本框，值先保持原样，等用户填完
                         * 再写 —— 别一选中就把工程里的格式清掉。 */
                        ed->setVisible(true);
                        ed->setFocus();
                        ed->selectAll();
                    });
            connect(ed, &QLineEdit::editingFinished, this,
                    [apply, ed]() { apply(ed->text()); });

            auto *row = new QWidget(m_dyn);
            auto *rowLay = new QVBoxLayout(row);
            rowLay->setContentsMargins(0, 0, 0, 0);
            rowLay->setSpacing(2);
            rowLay->addWidget(cb);
            rowLay->addWidget(ed);
            m_dynForm->addRow(cap, row);
            m_dynForm->addRow(QString(), hint);
            refreshHint();
        } else if (p.name == QLatin1String("code")) {
            /* 【编码格式是固定三种，不是随便填的】固件 ui_text.c 里是**硬编码
             * 字符串比较**，认且只认这三个（见 ui_text.c:80/89/277 和
             * ui_synthesis_oled.c:764/966）：
             *
             *   strpic  字符串已经在 result.str 里预先光栅化成图片串，
             *           直接贴图（本工程 211 处在用）
             *   text    存的是原始字节，运行时按字库渲染；具体几字节一个字
             *           看 attrs.encode（0=ascii/gbk 1=unicode 2=utf8）
             *   ascii   内容由业务层运行时 set，初始化时 attrs.str = NULL，
             *           资源里的文字列表**不会显示**
             *
             * 填成别的（哪怕只是拼错一个字母）两个分支都不匹配，控件在屏上
             * 就是**一片空白**，而且没有任何报错 —— 以前这里是个自由文本框，
             * 正好是最容易踩的那种坑。
             * 工程里出现过没见过的值就原样保留并选中，不替用户改工程。 */
            const QString cur = def.toString();
            auto *cb = new QComboBox(m_dyn);
            cb->setFont(monoFont());
            cb->addItem(QStringLiteral("strpic"));
            cb->addItem(QStringLiteral("text"));
            cb->addItem(QStringLiteral("ascii"));
            if (cb->findText(cur) < 0 && !cur.isEmpty()) {
                cb->addItem(cur);
            }
            cb->setCurrentIndex(qMax(0, cb->findText(cur)));
            cb->setToolTip(QStringLiteral(
                "strpic：文字已预先光栅化进 result.str，直接贴图；\n"
                "        显示什么由「文字列表」决定（最常用）\n"
                "text  ：内容由程序运行时写（ui_text_set_text_by_id 等），\n"
                "        按字库渲染。资源里的文字列表不会显示\n"
                "ascii ：同上，走 ASCII 字模；程序不写就什么都不显示\n"
                "固件只认这三个，写别的控件在屏上会是空白"));

            /* code 和「文字列表」搭不搭得上，实时校验 */
            auto *codeHint = new QLabel(m_dyn);
            codeHint->setCursor(Qt::WhatsThisCursor);
            codeHint->installEventFilter(this);
            auto refreshCode = [this, codeHint, cb]() {
                if (!m_node) {
                    return;
                }
                QString c;
                int strN = 0;
                for (const UiProperty &q : m_node->props) {
                    if (q.name == QLatin1String("code")) {
                        c = q.raw.value(QStringLiteral("default")).toString();
                    } else if (q.name == QLatin1String("str")) {
                        strN = q.raw.value(QStringLiteral("list")).toArray().size();
                    }
                }
                QString msg;
                if (c != QLatin1String("strpic") && strN > 0) {
                    /* 【text 下不是"显示那句话"，是乱码】init 时 attrs.str 指向的
                     * 是 u16 的 ResID 数组，字库把那几个字节当字符渲染。 */
                    msg = QStringLiteral(
                        "编码格式是 %1，内容要由程序运行时写进来"
                        "（ui_text_set_text_by_id 那几个接口，按 ID号 找控件）。\n"
                        "这里配的 %2 条文字列表不会显示；%3")
                        .arg(c).arg(strN)
                        .arg(c == QLatin1String("text")
                             ? QStringLiteral("text 下字库还会把 ResID 当字符渲染，"
                                              "屏上是乱码。")
                             : QStringLiteral("程序不写就一直是空的。"));
                } else if (c != QLatin1String("strpic")
                           && c != QLatin1String("text")
                           && c != QLatin1String("ascii") && !c.isEmpty()) {
                    msg = QStringLiteral(
                        "固件只认 strpic / text / ascii 三个，\"%1\" 三个分支都不"
                        "匹配 —— 这个控件在屏上会是一片空白，而且不会报错。").arg(c);
                }
                setWarning(codeHint, cb, msg);
            };
            connect(cb, &QComboBox::currentTextChanged, this,
                    [commit, refreshCode](const QString &t) {
                        commit([&t](QJsonObject &o) {
                            o.insert(QStringLiteral("default"), t);
                        });
                        refreshCode();
                    });
            m_dynForm->addRow(cap, cb);
            m_dynForm->addRow(QString(), codeHint);

            /* 【只用于预览的假文字】text / ascii 的内容是运行时由程序写的，
             * 资源里没有，画布上本来只能是空的 —— 排版时看不到字，很难判断
             * 这个框够不够宽、对齐对不对。这里配一句假的顶上。
             *
             * 【绝对不写进工程】写进去就不是原厂那份 json 了（读写要逐字节
             * 相同是硬指标）。存在工具自己的配置里，按"工程名 + ID号"做键，
             * 工程目录和资源一个字节都不碰。 */
            auto *preset = new QLineEdit(Preview::presetText(n), m_dyn);
            preset->setFont(monoFont());
            preset->setPlaceholderText(QStringLiteral("（仅预览，不写进工程）"));
            preset->setToolTip(QStringLiteral(
                "这一栏只影响画布上的预览，**不会**写进工程文件、也不会进资源。\n"
                "text / ascii 的真实内容由程序运行时写（按 ID号 找控件），\n"
                "工具不可能知道会填什么 —— 摆一句假的方便看排版。"));
            connect(preset, &QLineEdit::editingFinished, this, [this, preset]() {
                if (!m_node) {
                    return;
                }
                Preview::setPresetText(m_node, preset->text());
                /* 【不能发 nodeEdited】那条路会把工程标记成"改过"（标题带 *、
                 * 退出问保存）—— 可这次什么都没改到工程里。单独一条信号，
                 * 只让画布和右栏重画。 */
                emit previewOnlyChanged();
            });
            m_dynForm->addRow(QStringLiteral("预览文字"), preset);
            /* 只有 text / ascii 需要这一栏；strpic 的内容来自资源，不该有 */
            auto syncPresetRow = [preset, this]() {
                const bool need = Preview::needsPresetText(m_node);
                preset->setVisible(need);
                if (QWidget *lb = m_dynForm->labelForField(preset)) {
                    lb->setVisible(need);
                }
            };
            connect(cb, &QComboBox::currentTextChanged, this,
                    [syncPresetRow](const QString &) { syncPresetRow(); });
            syncPresetRow();
            refreshCode();
        } else if (p.type == QLatin1String("piclist") || p.type == QLatin1String("arrlist")) {
            m_dynForm->addRow(cap, makeListButton(p, cap, commit));
        } else if (p.type == QLatin1String("text-pic")) {
            m_dynForm->addRow(cap, makeTextListButton(p, cap, commit));
        } else if (p.type == QLatin1String("action")) {
            m_dynForm->addRow(cap, makeActionButton(p, commit));
        } else if (!en.isEmpty()) {
            auto *cb = new QComboBox(m_dyn);
            cb->setFont(monoFont());
            for (const QJsonValue &ev : en) {
                const QJsonObject eo = ev.toObject();
                for (auto it = eo.begin(); it != eo.end(); ++it) {
                    cb->addItem(it.key());
                }
            }
            const int idx = cb->findText(def.toString());
            if (idx >= 0) {
                cb->setCurrentIndex(idx);
            }
            connect(cb, &QComboBox::currentTextChanged, this,
                    [commit](const QString &t) {
                        commit([&t](QJsonObject &o) {
                            o.insert(QStringLiteral("default"), t);
                        });
                    });
            m_dynForm->addRow(cap, cb);
        } else if (def.isDouble()) {
            auto *sp = new QSpinBox(m_dyn);
            applyIntRange(sp, p.raw);      // 范围和提示语都在里面，见它的抬头
            sp->setFont(monoFont());
            sp->setValue(def.toInt());
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [commit](int v) {
                        commit([v](QJsonObject &o) {
                            o.insert(QStringLiteral("default"), v);
                        });
                    });
            m_dynForm->addRow(cap, sp);
        } else {
            auto *e = new QLineEdit(def.toString(), m_dyn);
            e->setFont(monoFont());
            const int maxLen = p.raw.value(QStringLiteral("maxlength")).toInt(0);
            if (maxLen > 0) {
                e->setMaxLength(maxLen);
                e->setToolTip(tr("最多 %1 个字符\n"
                                 "这是控件模板给这条属性定的长度，"
                                 "固件那边按定长存，填不下就会被截掉").arg(maxLen));
            }
            connect(e, &QLineEdit::editingFinished, this, [commit, e]() {
                const QString t = e->text();
                commit([&t](QJsonObject &o) {
                    o.insert(QStringLiteral("default"), t);
                });
            });
            m_dynForm->addRow(cap, e);
        }
    }
    applyNoWheel(m_dyn);         // 滚轮别改值，见 NoWheelFilter
}

/* ---- 三个"开子对话框"的按钮 ---------------------------------------------
 * 图片列表 / 文字列表 / 事件动作在 json 里都是数组，塞不进一行编辑框。
 * 原厂也是点开一个独立窗口（ImageFileDialog / I18nLanguage / ActionList），
 * 这里照做，按钮上直接显示当前条目数，省得点进去才知道有没有配。 */

static QStringList jsonToStringList(const QJsonArray &a)
{
    QStringList out;
    for (const QJsonValue &v : a) {
        out.append(v.toString());
    }
    return out;
}

int ComProperty::previewIndexOf(const UiProperty &p) const
{
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    if (lst.isEmpty()) {
        return -1;
    }
    if (p.name == QLatin1String("str")) {
        const QString def = p.raw.value(QStringLiteral("default")).toString();
        for (int i = 0; i < lst.size(); ++i) {
            if (lst.at(i).toString() == def) {
                return i;
            }
        }
        return 0;
    }
    if (p.name == QLatin1String("normal_image") && m_node) {
        for (const UiProperty &q : m_node->props) {
            if (q.name == QLatin1String("highlight")) {
                return qBound(0, q.raw.value(QStringLiteral("default")).toInt(),
                              lst.size() - 1);
            }
        }
    }
    return 0;
}

/** 列表条目下拉框：图片给缩略图 + 文件名，文字给"内容#ResID"。 */
QComboBox *ComProperty::makeEntryCombo(const UiProperty &p, bool isText)
{
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    auto *cb = new QComboBox(m_dyn);
    cb->setFont(monoFont());
    cb->setIconSize(QSize(16, 16));
    if (lst.isEmpty()) {
        cb->setEnabled(false);
        return cb;
    }
    const PropertyContext &ctx = PropertyContext::instance();
    for (const QJsonValue &v : lst) {
        const QString rel = v.toString();
        if (isText) {
            /* 原厂显示成"蓝牙#m1"：前面是这条 ResID 在多国语言表里的内容，
             * 后面是 ResID 本身。只给 ResID 的话，面板上看不出画的是哪句话。 */
            const QString s = Preview::stringOf(rel);
            cb->addItem(s.isEmpty() ? rel : QStringLiteral("%1#%2").arg(s, rel));
        } else {
            const QString abs = QFileInfo(rel).isAbsolute()
                                ? rel : QDir(ctx.projectDir).filePath(rel);
            const QPixmap pm(abs);
            cb->addItem(pm.isNull() ? QIcon() : QIcon(pm),
                        QFileInfo(rel).fileName());
        }
    }
    /* 停在预览真正画的那一条上 —— 这是"面板和画布对得上"的关键 */
    const int pi = previewIndexOf(p);
    if (pi >= 0 && pi < cb->count()) {
        cb->setCurrentIndex(pi);
    }
    return cb;
}

QWidget *ComProperty::makeListButton(const UiProperty &p, const QString &cap,
                                     const CommitFn &commit)
{
    auto *box = new QWidget(m_dyn);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    auto *btn = new QPushButton(box);
    btn->setFont(monoFont());
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    btn->setText(tr("%1 项…").arg(lst.size()));
    lay->addWidget(btn);

    /* 条目下拉框：**只用来看**，不写任何字段。
     * 【为什么不能让它改预览】piclist 的 default 几乎都不在 list 里（模板
     * 残留），拿它当"当前条目"存下去等于凭空改工程文件，产物就和原厂不一样
     * 了。要换预览的那一条，改"默认高亮"（ImageList 有这个参数）。 */
    auto *cb = makeEntryCombo(p, false);
    if (p.name == QLatin1String("normal_image")) {
        cb->setToolTip(QStringLiteral("画布上画的是「默认高亮」指定的这一条；"
                                      "改「默认高亮」就能换"));
    } else {
        cb->setToolTip(QStringLiteral("列表内容（只读）。画布上画的是第 1 条"));
    }
    lay->addWidget(cb);

    const int maxLen = p.raw.value(QStringLiteral("maxlength")).toInt(0);
    btn->setToolTip(maxLen > 0
                    ? tr("现在 %1 项，最多 %2 项\n点开挑内容；上限是控件模板定的")
                          .arg(lst.size()).arg(maxLen)
                    : tr("现在 %1 项，条数不限\n点开挑内容").arg(lst.size()));
    const QJsonArray init = lst;
    connect(btn, &QPushButton::clicked, this, [this, btn, init, maxLen, cap, commit]() {
        const PropertyContext &ctx = PropertyContext::instance();
        ImageFileDialog dlg(this);
        dlg.setWindowTitle(cap);
        dlg.setProjectDir(ctx.projectDir);
        dlg.setMaxCount(maxLen);
        dlg.setSelected(jsonToStringList(init));
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        QJsonArray arr;
        for (const QString &r : dlg.selected()) {
            arr.append(r);
        }
        btn->setText(tr("%1 项…").arg(arr.size()));
        commit([&arr](QJsonObject &o) { o.insert(QStringLiteral("list"), arr); });
        /* 列表变了，下拉框和画布都要跟上 —— 不刷的话面板上还是旧条目 */
        if (m_node) {
            showNode(m_node);
        }
    });
    return box;
}

QWidget *ComProperty::makeTextListButton(const UiProperty &p, const QString &cap,
                                         const CommitFn &commit)
{
    auto *box = new QWidget(m_dyn);
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    auto *btn = new QPushButton(box);
    btn->setFont(monoFont());
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    btn->setText(lst.isEmpty() ? tr("（空）…")
                               : QStringLiteral("%1 …").arg(jsonToStringList(lst).join(QLatin1Char(','))));
    lay->addWidget(btn);

    /* 文字列表的条目下拉框**可以改**：str 的 default 一定在 list 里
     * （实测 209/209），它就是"当前显示的那一条"，改它是正当编辑，
     * 画布也跟着换。 */
    auto *cb = makeEntryCombo(p, true);
    cb->setToolTip(QStringLiteral("当前显示的那一条（画布画的就是它）"));
    lay->addWidget(cb);
    const QStringList ids = jsonToStringList(lst);
    connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, commit, ids](int i) {
                if (i < 0 || i >= ids.size()) {
                    return;
                }
                const QString id = ids.at(i);
                commit([&id](QJsonObject &o) {
                    o.insert(QStringLiteral("default"), id);
                });
            });

    const int maxLen = p.raw.value(QStringLiteral("maxlength")).toInt(0);
    const QJsonArray init = lst;
    connect(btn, &QPushButton::clicked, this, [this, btn, init, maxLen, cap, commit]() {
        const PropertyContext &ctx = PropertyContext::instance();
        I18nLanguage dlg(this);
        dlg.setWindowTitle(cap);
        dlg.setMaxCount(maxLen);
        QString err;
        /* 大表读起来要几百毫秒，原厂在这儿改光标并打两条状态：
         * "加载多国语言..." / "加载多国语言完成.."。没有反馈的话点下去
         * 界面像卡住了。 */
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = !ctx.excelPath.isEmpty() && dlg.loadExcel(ctx.excelPath, &err);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("警告"),
                                 err.isEmpty()
                                 ? QStringLiteral(" 文件未找到. 请查看[全局设置]里的路径目录"
                                                  "是否正确，重新选择多国语言文件. ")
                                 : err);
            return;
        }
        dlg.setSelected(jsonToStringList(init));
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const QStringList sel = dlg.selected();
        QJsonArray arr;
        for (const QString &r : sel) {
            arr.append(r);
        }
        btn->setText(sel.isEmpty() ? tr("（空）…")
                                   : QStringLiteral("%1 …").arg(sel.join(QLatin1Char(','))));
        commit([&arr, &sel](QJsonObject &o) {
            o.insert(QStringLiteral("list"), arr);
            // default 是"当前显示的那条"，原厂就是列表第一项
            o.insert(QStringLiteral("default"), sel.value(0));
        });
        if (m_node) {
            showNode(m_node);            // 条目下拉框跟上新列表
        }
    });
    return box;
}

QPushButton *ComProperty::makeActionButton(const UiProperty &p, const CommitFn &commit)
{
    auto *btn = new QPushButton(m_dyn);
    btn->setFont(monoFont());
    const QJsonArray arr = p.raw.value(QStringLiteral("action")).toArray();
    int n = 0;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("values"))) {
            n = o.value(QStringLiteral("values")).toArray().size();
        }
    }
    btn->setText(tr("%1 条事件…").arg(n));
    connect(btn, &QPushButton::clicked, this, [this, btn, arr, commit]() {
        ActionList dlg(this);
        dlg.setObjectNames(PropertyContext::instance().objectNames);
        QString fmtErr;
        if (!dlg.setActionPropertyChecked(arr, &fmtErr)) {
            QMessageBox::warning(this, QStringLiteral("格式错误"), fmtErr);
            return;
        }
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }
        const QJsonArray out = dlg.actionProperty();
        int m = 0;
        for (const QJsonValue &v : out) {
            const QJsonObject o = v.toObject();
            if (o.contains(QStringLiteral("values"))) {
                m = o.value(QStringLiteral("values")).toArray().size();
            }
        }
        btn->setText(tr("%1 条事件…").arg(m));
        commit([&out](QJsonObject &o) { o.insert(QStringLiteral("action"), out); });
    });
    return btn;
}
