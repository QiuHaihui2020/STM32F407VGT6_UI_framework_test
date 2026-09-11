#include "UiTheme.h"

#include "GlobalSettings.h"

namespace {

/** 两色按比例混，t=1 全取 a。整套派生色都靠它。 */
QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor(int(a.red()   * t + b.red()   * (1 - t)),
                  int(a.green() * t + b.green() * (1 - t)),
                  int(a.blue()  * t + b.blue()  * (1 - t)));
}

/** 按明度缩放（保色相和饱和度），用来从一个底色推出更深/更浅的层次。 */
QColor shade(const QColor &c, qreal f)
{
    int h, s, v, a;
    c.getHsv(&h, &s, &v, &a);
    return QColor::fromHsv(h, s, qBound(0, int(v * f), 255), a);
}

UiTheme::Palette mk(const char *win, const char *panel, const char *field,
                    const char *border, const char *canvas, const char *text,
                    const char *accent)
{
    UiTheme::Palette p;
    p.win    = QColor(QLatin1String(win));
    p.panel  = QColor(QLatin1String(panel));
    p.field  = QColor(QLatin1String(field));
    p.border = QColor(QLatin1String(border));
    p.canvas = QColor(QLatin1String(canvas));
    p.text   = QColor(QLatin1String(text));
    p.accent = QColor(QLatin1String(accent));
    return p;
}

/* 样式表模板。@xxx 是占位符，qss() 按调色板换成真实色值。 */
const char *const kTpl = R"(
QMainWindow, QMainWindow > QWidget { background: @win; }
QWidget { color: @text; }
/* 画布宿主比面板深一档 —— 128x64 那块屏要跳出来。
 * 名字是 centralWidget（QScrollArea），别写成 canvasHost：那是里面的 QWidget。 */
QScrollArea#centralWidget, QScrollArea#centralWidget > QWidget > QWidget
    { background: @canvas; border: none; }

QDockWidget { background: @panel; }
QDockWidget > QWidget { background: @panel; }
QDockWidget::title { background: @title; padding: 0px; max-height: 10px; }

QTreeWidget, QTableWidget {
    background: @field; border: 1px solid @border;
    alternate-background-color: @list;
}
QTreeWidget::item, QListWidget::item { padding: 1px 2px; }
QTreeWidget::item:hover, QListWidget::item:hover { background: @accentHover; }
QTreeWidget::item:selected, QListWidget::item:selected {
    background: @accent; color: @onAccent;
}
QListWidget { background: @list; border: 1px solid @border; }
QHeaderView::section {
    background: @title; color: @dim; border: none;
    border-right: 1px solid @border; border-bottom: 1px solid @border;
    padding: 2px 4px;
}

QGroupBox {
    background: @panel;
    border: 1px solid @border;
    border-radius: 3px;
    margin-top: 14px;
    padding-top: 4px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    padding: 0 4px; background: @panel; color: @dim;
}

QScrollArea { background: @list; border: 1px solid @border; }
QScrollArea > QWidget > QWidget { background: @list; }

QTabWidget::pane { background: @field; border: 1px solid @border; top: -1px; }
QTabBar::tab {
    background: @title; color: @dim;
    border: 1px solid @border; border-bottom: none;
    padding: 3px 10px; margin-right: 2px;
}
QTabBar::tab:hover { background: @accentSoft; }
QTabBar::tab:selected {
    background: @field; color: @text;
    border-top: 2px solid @accent;
}

QLineEdit, QSpinBox, QComboBox {
    background: @field; color: @text;
    border: 1px solid @border; border-radius: 2px;
    padding: 1px 3px;
    selection-background-color: @accent; selection-color: @onAccent;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid @accent; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    background: @disabledBg; color: @disabledFg;
}
QComboBox QAbstractItemView {
    background: @field; color: @text;
    border: 1px solid @border; selection-background-color: @accent;
}

QPushButton {
    background: @field; color: @text;
    border: 1px solid @border; border-radius: 3px;
    padding: 2px 10px;
}
QPushButton:hover  { background: @accentHover; border-color: @accentEdge; }
QPushButton:pressed{ background: @accentPress; border-color: @accent; }
QPushButton:disabled { background: @disabledBg; color: @disabledFg; }

QMenu { background: @field; color: @text; border: 1px solid @border; }
QMenu::item:selected { background: @accent; color: @onAccent; }
QToolTip { background: @field; color: @text; border: 1px solid @border; }

QSplitter::handle { background: @border; }
QScrollBar:vertical   { background: @trough; width: 12px; margin: 0; }
QScrollBar:horizontal { background: @trough; height: 12px; margin: 0; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: @handle; border-radius: 4px; min-height: 24px; min-width: 24px;
}
QScrollBar::handle:hover { background: @handleHover; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* 工具栏：图标在上、文字在下的大按钮 */
QToolBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 @toolTop, stop:1 @toolBottom);
    border-bottom: 1px solid @border;
    spacing: 0px;
    padding: 1px;
}
QToolBar::separator { width: 1px; background: @border; margin: 6px 4px; }
/* 【别给 QToolButton 加 min-width】QStyleSheetStyle 会拿它当**实际宽度**用，
 * 不是当下限：加了 min-width:52px 之后每个按钮都被压成 52+padding，标题就被
 * QCommonStylePrivate::toolButtonElideText 从中间截断成「新建…」。让 Qt 自己按文字算。 */
QToolButton { padding: 2px 5px; border: 1px solid transparent; border-radius: 3px; }
QToolButton:hover { border: 1px solid @accentEdge; background: @accentHover; }
QToolButton:pressed { border: 1px solid @accent; background: @accentPress; }
QToolButton:checked { border: 1px solid @accent; background: @accentSoft; }
QToolButton:disabled { color: @disabledFg; }
)";

} // namespace

namespace UiTheme {

const QVector<Preset> &presets()
{
    static const QVector<Preset> kAll = {
        /*                  窗口底     面板底     输入区     边框       画布底     文字       强调色   */
        { QStringLiteral("浅灰蓝"),
          mk("#F4F5F7", "#EDEFF2", "#FFFFFF", "#D0D5DD", "#E4E7EC", "#202B37", "#1E88E5") },
        { QStringLiteral("暖灰"),
          mk("#F7F6F3", "#F0EEE9", "#FFFFFF", "#D9D4CB", "#E8E4DC", "#2B2722", "#C2703B") },
        { QStringLiteral("经典绿"),
          mk("#F0F0F0", "#C0DCC0", "#FFFFFF", "#9BBF9B", "#F0F0F0", "#1B2A1B", "#2B7DD1") },
        { QStringLiteral("石板深色"),
          mk("#1E2227", "#262B31", "#14181C", "#3A424B", "#14181C", "#C8D1DA", "#539BF5") },
        { QStringLiteral("午夜蓝"),
          mk("#16202A", "#1D2A35", "#101820", "#2E3F4D", "#101820", "#C3D1DC", "#2EA9E0") },
    };
    return kAll;
}

Palette presetByName(const QString &name)
{
    for (const Preset &p : presets()) {
        if (p.name == name) {
            return p.pal;
        }
    }
    return presets().first().pal;
}

Palette derive(const QColor &base, const QColor &accent)
{
    const bool dark = base.lightness() < 128;
    Palette p;
    p.panel  = base;
    p.accent = accent;
    /* 输入区：浅色主题下就是纯白（内容区要最亮）；深色主题下比面板更暗，
     * 这样"输入框/树"在面板里仍然是凹下去的一块。 */
    p.field  = dark ? shade(base, 0.72) : QColor(Qt::white);
    p.win    = dark ? shade(base, 0.86) : mix(base, QColor(Qt::white), 0.45);
    /* 画布底比面板再深一档 —— 那块 128x64 的屏要衬得出来 */
    p.canvas = dark ? shade(base, 0.66) : shade(base, 0.94);
    /* 文字：底色深就用浅字，反之亦然。掺一点底色进去，比纯黑白柔和 */
    p.text   = dark ? mix(QColor(Qt::white), base, 0.82)
                    : mix(QColor(Qt::black), base, 0.88);
    p.border = mix(base, p.text, 0.80);
    return p;
}

Palette current()
{
    const QString preset =
        GlobalSettings::value(QStringLiteral("Ui/Preset")).toString();
    const QColor accent(
        GlobalSettings::value(QStringLiteral("Ui/Accent")).toString());

    if (preset.isEmpty()
        && GlobalSettings::value(QStringLiteral("Ui/Base")).isValid()) {
        /* 自定义：底色 + 强调色推一整套 */
        const QColor base(
            GlobalSettings::value(QStringLiteral("Ui/Base")).toString());
        if (base.isValid()) {
            return derive(base, accent.isValid() ? accent
                                                 : presets().first().pal.accent);
        }
    }
    /* 现成方案：整套底色照搬，强调色单独覆盖 —— 这样换强调色不用重存底色，
     * 换方案也不会把用户挑的强调色冲掉。 */
    Palette p = presetByName(preset);
    if (accent.isValid()) {
        p.accent = accent;
    }
    return p;
}

void save(const QString &presetName, const QColor &base, const QColor &accent)
{
    GlobalSettings::setValue(QStringLiteral("Ui/Preset"), presetName);
    if (base.isValid()) {
        GlobalSettings::setValue(QStringLiteral("Ui/Base"), base.name());
    }
    if (accent.isValid()) {
        GlobalSettings::setValue(QStringLiteral("Ui/Accent"), accent.name());
    }
}

QString qss(const Palette &p)
{
    /* 强调色上面压什么字色：按它自己的明度定，别写死白色 ——
     * 选了个浅黄当强调色的话，白字就没了。 */
    const QColor onAccent = p.accent.lightness() < 140 ? QColor(Qt::white)
                                                       : QColor(0x10, 0x18, 0x20);
    QString s = QString::fromLatin1(kTpl);
    /* 【长的先替换】@accent 是 @accentHover 的前缀，反过来会把
     * "@accentHover" 先啃掉一半变成 "#1e88e5Hover"。 */
    const QVector<QPair<QString, QColor>> subs = {
        { QStringLiteral("@accentHover"), mix(p.accent, p.field, 0.14) },
        { QStringLiteral("@accentPress"), mix(p.accent, p.field, 0.28) },
        { QStringLiteral("@accentSoft"),  mix(p.accent, p.panel, 0.18) },
        { QStringLiteral("@accentEdge"),  mix(p.accent, p.border, 0.55) },
        { QStringLiteral("@onAccent"),    onAccent },
        { QStringLiteral("@accent"),      p.accent },
        { QStringLiteral("@disabledBg"),  mix(p.panel, p.field, 0.50) },
        { QStringLiteral("@disabledFg"),  mix(p.text, p.panel, 0.42) },
        { QStringLiteral("@handleHover"), mix(p.border, p.text, 0.55) },
        { QStringLiteral("@handle"),      mix(p.border, p.text, 0.78) },
        { QStringLiteral("@toolBottom"),  mix(p.win, p.panel, 0.50) },
        { QStringLiteral("@toolTop"),     mix(p.field, p.win, 0.70) },
        { QStringLiteral("@trough"),      mix(p.panel, p.field, 0.60) },
        { QStringLiteral("@border"),      p.border },
        { QStringLiteral("@canvas"),      p.canvas },
        { QStringLiteral("@field"),       p.field },
        { QStringLiteral("@panel"),       p.panel },
        { QStringLiteral("@title"),       mix(p.panel, p.text, 0.92) },
        { QStringLiteral("@list"),        mix(p.field, p.panel, 0.55) },
        { QStringLiteral("@text"),        p.text },
        { QStringLiteral("@dim"),         mix(p.text, p.panel, 0.65) },
        { QStringLiteral("@win"),         p.win },
    };
    for (const auto &kv : subs) {
        s.replace(kv.first, kv.second.name());
    }
    return s;
}

} // namespace UiTheme
