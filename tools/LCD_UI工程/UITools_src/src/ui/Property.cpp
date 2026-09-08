#include "Property.h"

#include "ActionList.h"
#include "EditorOps.h"
#include "Preview.h"
#include "I18nLanguage.h"
#include "ImageFileDialog.h"

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

/* ===================== FileEdit ===================== */

FileEdit::FileEdit(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    m_main = new QPushButton(QStringLiteral("背景图片"), this);
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

    auto choose = [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图片"), QString(), tr("图片 (*.bmp *.png *.jpg)"));
        if (!f.isEmpty()) {
            setFilePath(f);
        }
    };
    connect(m_clear, &QPushButton::clicked, this, [this]() { setFilePath(QString()); });
    connect(m_main, &QPushButton::clicked, this, choose);
    connect(m_pick, &QPushButton::clicked, this, choose);
}

FileEdit::~FileEdit() = default;

void FileEdit::setCaption(const QString &c)
{
    m_main->setText(c);
}

void FileEdit::setFilePath(const QString &p)
{
    if (m_path == p) {
        return;
    }
    m_path = p;
    m_main->setToolTip(p);
    m_clear->setEnabled(!p.isEmpty());   // 手册：删完箭头变灰
    emit filePathChanged(p);
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
        s->setRange(-9999, 9999);
        s->setFont(monoFont());
        return s;
    };
    m_x = mkSpin("spinX");
    m_y = mkSpin("spinY");
    m_w = mkSpin("spinW");
    m_h = mkSpin("spinH");
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

/* ===================== Border ===================== */

Border::Border(QWidget *parent)
    : QGroupBox(tr("内边框线"), parent)
{
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(6, 4, 6, 6);
    box->setSpacing(4);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(4);

    auto mkSpin = [this]() {
        auto *s = new QSpinBox(this);
        s->setRange(0, 255);
        s->setFont(monoFont());
        return s;
    };
    m_l = mkSpin();
    m_t = mkSpin();
    m_r = mkSpin();
    m_b = mkSpin();
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
            m_pos->setRect(n->rectOf(m_state));
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
            auto *w = new FileEdit(this);
            w->setCaption(cap.isEmpty() ? QStringLiteral("背景图片") : cap);
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
            /* 原厂给整数框挂的提示是"9999 内的整数"（模板里有 min/max 时
             * 换成 "%2的整数"，%2 是那个范围）。 */
            s->setToolTip(QStringLiteral("9999 内的整数"));
            s->setRange(po.value(QStringLiteral("min")).toInt(-9999),
                        po.value(QStringLiteral("max")).toInt(9999));
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
            {
                const QJsonValue mn = p.raw.value(QStringLiteral("min"));
                const QJsonValue mx = p.raw.value(QStringLiteral("max"));
                sp->setToolTip((mn.isUndefined() || mx.isUndefined())
                               ? QStringLiteral("9999 内的整数")
                               : QStringLiteral("%1的整数")
                                 .arg(QStringLiteral("%1..%2").arg(mn.toInt()).arg(mx.toInt())));
            }
            sp->setRange(p.raw.value(QStringLiteral("min")).toInt(-99999),
                         p.raw.value(QStringLiteral("max")).toInt(99999));
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

QPushButton *ComProperty::makeListButton(const UiProperty &p, const QString &cap,
                                         const CommitFn &commit)
{
    auto *btn = new QPushButton(m_dyn);
    btn->setFont(monoFont());
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    btn->setText(tr("%1 项…").arg(lst.size()));
    const int maxLen = p.raw.value(QStringLiteral("maxlength")).toInt(0);
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
    });
    return btn;
}

QPushButton *ComProperty::makeTextListButton(const UiProperty &p, const QString &cap,
                                             const CommitFn &commit)
{
    auto *btn = new QPushButton(m_dyn);
    btn->setFont(monoFont());
    const QJsonArray lst = p.raw.value(QStringLiteral("list")).toArray();
    btn->setText(lst.isEmpty() ? tr("（空）…")
                               : QStringLiteral("%1 …").arg(jsonToStringList(lst).join(QLatin1Char(','))));
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
    });
    return btn;
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
