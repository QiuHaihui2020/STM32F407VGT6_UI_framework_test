#include "BaseDialog.h"
#include "GlobalSettings.h"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

/*
 * 原厂[全局设置]（版式见 temp/全局设置.jpg）：
 *
 *   ┌──────────────────────────────────────────────┐
 *   │ 更新设置要重启软件才能生效.        ← 红底警示，在**最上面** │
 *   ├───────────────┬──────────────────────────────┤
 *   │ 全局配置项    │ 内容                          │
 *   ├───────────────┼──────────────────────────────┤
 *   │ ▶ 界面尺寸    │                               │
 *   │   图片资源目…│ config                    [...] │
 *   │   多国语言文…│ ../../../UITools/多国语言…xls [...]│
 *   │   工程目录:   │ .                         [...] │
 *   │   控件文件:   │ …/control/control.json    [...] │
 *   │   自定义控件…│ …/control/ex              [...] │
 *   └───────────────┴──────────────────────────────┘
 *                                    [确定]  [取消]
 *
 * 五条的标题、说明文字、文件过滤器全部逐字来自 ui-tools.exe 的字符串
 * （0xc98a31 起那一段，扫描方法见 docs/FACTORY_UI.md 5.10）：
 *
 *   多国语言文件:   工程控件要用的语言文,可选office2003版本的xls文件,或者utf8格式,分号(;)间隔的csv文件.
 *                   xls 文件 , CSV UTF-8 文件 (*.xls *.csv )
 *   控件文件:       原始的模版控件文件,json格式.            json 文件 (*.json)
 *   图片资源目录:   工程中要用到的图片资源目录,默认是 images
 *   工程目录:       工程保存的目录,默认是程序运行目录.
 *   自定义控件目录: 自定义的模版控件目录,默认是widgets目录.
 *
 * 「界面尺寸」是个可展开的组，两个子项 "宽度:" / "高度:"（字面量在
 * 0xc9455e / 0xc94566），合起来存成 Project/Size = "宽*高"。
 *
 * 【行的先后】截图上的次序是 界面尺寸 / 图片资源目录 / 多国语言文件 /
 * 工程目录 / 控件文件 / 自定义控件目录 —— 五条路径按标题的 Unicode 码点
 * 升序（图 56FE < 多 591A < 工 5DE5 < 控 63A7 < 自 81EA），像是从
 * QMap<QString,…> 里遍历出来的。这里直接按截图钉死，不去猜它内部用的容器。
 *
 * 【值编辑器】原厂那个类叫 FileEdit，有 filePath 属性和 filePathChanged(QString)
 * 信号（moc 元数据 0xcc1060）。名字照抄，方便日后对照。
 */

namespace {

/** 当前用的是哪个目录下的 ui-config。空 = 还没定过，按当前目录算。 */
QString g_cfgDir;

/** 设置文件：<工程目录>/Application Data/ui-config，和原厂同一个文件名。
 *
 *  【为什么要缓存】QSettings 存的是相对路径的话，Qt 只在构造的那一刻解析
 *  一次；而工具跑起来之后会因为各种文件对话框改掉进程的当前目录。所以这里
 *  一定要用绝对路径。
 *
 *  【为什么是工程目录不是当前目录】见 GlobalSettings::setProjectDir 的说明。
 *  没打开工程时（空启动、自测）退回当前目录，行为和以前一样。 */
QString configPath()
{
    static QString kFallback;
    if (!g_cfgDir.isEmpty()) {
        return QDir(g_cfgDir).absoluteFilePath(
            QStringLiteral("Application Data/ui-config"));
    }
    if (kFallback.isEmpty()) {
        kFallback = QDir(QDir::currentPath()).absoluteFilePath(
            QStringLiteral("Application Data/ui-config"));
    }
    return kFallback;
}

/** QSettings 不可拷贝，给个共享实例。
 *
 *  【路径变了要换一个】QSettings 的文件是构造时定死的，改不了。工程一换
 *  就得整个换掉 —— 换之前先 sync()，否则上一个工程还没落盘的改动会丢。 */
QSettings &settings()
{
    static QSettings *st = nullptr;
    static QString stPath;
    const QString want = configPath();
    if (!st || stPath != want) {
        if (st) {
            st->sync();
            delete st;
        }
        st = new QSettings(want, QSettings::IniFormat);
        stPath = want;
    }
    return *st;
}

struct PathRow {
    const char *key;        ///< Project/ 下的键名，原厂拼写照抄
    const char *caption;
    const char *tip;
    const char *filter;     ///< 空 = 选目录，非空 = 选文件
    const char *def;        ///< 说明文字里写的那个默认值
};

/* 次序 = 原厂截图上的次序 */
const PathRow kRows[] = {
    { "Project/ImageDir", "图片资源目录:",
      "工程中要用到的图片资源目录,默认是 images", "", "images" },
    { "Project/LangugeFile", "多国语言文件:",
      "工程控件要用的语言文,可选office2003版本的xls文件,或者utf8格式,分号(;)间隔的csv文件.",
      "xls 文件 , CSV UTF-8 文件 (*.xls *.csv )", "" },
    { "Project/Dir", "工程目录:",
      "工程保存的目录,默认是程序运行目录.", "", "." },
    { "Project/TemplateJson", "控件文件:",
      "原始的模版控件文件,json格式.",
      "json 文件 (*.json)", "" },
    { "Project/CustomTemplateDir", "自定义控件目录:",
      "自定义的模版控件目录,默认是widgets目录.", "", "widgets" },
};

/**
 * 一行的值编辑器：一个看不见边框的输入框 + 右端一个 [...] 按钮。
 *
 * 原厂长这样 —— 值就贴在行的底色上，没有输入框的凹陷边框，只有最右边
 * 那个小方按钮。所以这里把 QLineEdit 设成无边框、背景透明。
 */
class FileEdit : public QWidget
{
public:
    FileEdit(const QString &filter, QWidget *parent = nullptr)
        : QWidget(parent), m_filter(filter)
    {
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        m_edit = new QLineEdit(this);
        m_edit->setFrame(false);
        m_edit->setStyleSheet(QStringLiteral("background: transparent;"));
        auto *btn = new QPushButton(QStringLiteral("..."), this);
        /* 不清 padding 的话 30px 宽度里放不下 "..."，按钮会画成一个空方块 */
        btn->setStyleSheet(QStringLiteral("padding: 0px;"));
        btn->setFixedWidth(30);
        lay->addWidget(m_edit, 1);
        lay->addWidget(btn, 0);
        connect(btn, &QPushButton::clicked, this, [this]() {
            const QString p = m_filter.isEmpty()
                ? QFileDialog::getExistingDirectory(this, QStringLiteral(" 目录选择..."),
                                                    filePath())
                : QFileDialog::getOpenFileName(this, QStringLiteral(" 文件选择..."),
                                               filePath(), m_filter);
            if (p.isEmpty()) {
                return;
            }
            /* 原厂存的是相对当前目录的路径（ui-config 里就是
             * "../../../UITools/control/control.json" 这种），跟着工程走。
             * 能算出相对路径就存相对的，跨盘符才退回绝对路径。 */
            const QDir base(QDir::currentPath());
            const QString rel = base.relativeFilePath(p);
            setFilePath(QDir::isAbsolutePath(rel) ? p : rel);
        });
    }

    QString filePath() const { return m_edit->text(); }
    void setFilePath(const QString &p) { m_edit->setText(p); }

private:
    QLineEdit *m_edit = nullptr;
    QString    m_filter;
};

/**
 * 颜色行的值编辑器：一条填成该颜色的色条 + 右端一个 [...] 按钮。
 *
 * 和 FileEdit 一个形状，只是点开的是取色对话框。存成 "#rrggbb"。
 */
class ColorEdit : public QWidget
{
public:
    ColorEdit(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent), m_title(title)
    {
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 1, 0, 1);
        lay->setSpacing(0);
        m_swatch = new QLabel(this);
        m_swatch->setAutoFillBackground(true);
        m_swatch->setAlignment(Qt::AlignCenter);
        auto *btn = new QPushButton(QStringLiteral("..."), this);
        btn->setStyleSheet(QStringLiteral("padding: 0px;"));
        btn->setFixedWidth(30);
        lay->addWidget(m_swatch, 1);
        lay->addWidget(btn, 0);
        connect(btn, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(m_color, this, m_title);
            if (c.isValid()) {
                setColor(c);
            }
        });
    }

    QColor color() const { return m_color; }

    void setColor(const QColor &c)
    {
        m_color = c.isValid() ? c : QColor(Qt::black);
        /* 色条上把十六进制值也写出来 —— 深色底配浅字，浅色底配深字，
         * 不然选到接近底色的值就看不见了。 */
        const bool darkBg = m_color.lightness() < 128;
        m_swatch->setStyleSheet(
            QStringLiteral("background: %1; color: %2;")
            .arg(m_color.name(), darkBg ? QStringLiteral("#FFFFFF")
                                        : QStringLiteral("#000000")));
        m_swatch->setText(m_color.name().toUpper());
    }

private:
    QLabel *m_swatch = nullptr;
    QColor  m_color;
    QString m_title;
};

} // namespace

GlobalSettings::GlobalSettings(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("全局设置"));
    /* 原厂截图实测客户区 655x458 */
    resize(655, 458);
    /* 淡黄底 —— 原厂这个对话框整体就是这个色，表格才是白的 */
    setStyleSheet(QStringLiteral("QDialog { background: #FFFFCC; }"));

    /* ---- 红底警示，在最上面 ---- */
    auto *warn = new QLabel(QStringLiteral("更新设置要重启软件才能生效."), this);
    warn->setObjectName(QStringLiteral("label_4"));
    /* 样式表逐字来自 uic；原厂没有改前景色，所以是黑字红底 */
    warn->setStyleSheet(QStringLiteral("background-color: rgb(223, 28, 28);"));
    QFont wf = warn->font();
    wf.setBold(true);
    warn->setFont(wf);
    warn->setMargin(3);

    /* ---- 配置树 ---- */
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("treeWidget"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels(QStringList{ QStringLiteral("全局配置项"),
                                         QStringLiteral("内容") });
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 100);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);      // 「界面尺寸」前面那个展开箭头
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: #FFFFFF; alternate-background-color: #ECECEC; }"));

    QSettings &st = settings();

    /* 界面尺寸：一个组，两个子项。存成 Project/Size = "宽*高" */
    auto *sizeGrp = new QTreeWidgetItem(m_tree);
    sizeGrp->setText(0, QStringLiteral("界面尺寸"));
    const QStringList wh = st.value(QStringLiteral("Project/Size")).toString()
                             .split(QLatin1Char('*'));
    auto addSize = [&](const char *cap, int def) {
        auto *it = new QTreeWidgetItem(sizeGrp);
        it->setText(0, QString::fromUtf8(cap));
        auto *sp = new QSpinBox(m_tree);
        sp->setRange(1, 4096);
        sp->setValue(def);
        sp->setFrame(false);
        sp->setStyleSheet(QStringLiteral("background: transparent;"));
        m_tree->setItemWidget(it, 1, sp);
        return it;
    };
    m_width  = addSize("宽度:", wh.value(0).toInt() > 0 ? wh.value(0).toInt() : 128);
    m_height = addSize("高度:", wh.value(1).toInt() > 0 ? wh.value(1).toInt() : 64);

    /* 五条路径。说明文字挂 tooltip 上 —— 原厂也是鼠标悬停才出来的 */
    for (const PathRow &r : kRows) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString::fromUtf8(r.caption));
        item->setToolTip(0, QString::fromUtf8(r.tip));
        item->setData(0, Qt::UserRole, QLatin1String(r.key));
        auto *ed = new FileEdit(QString::fromUtf8(r.filter), m_tree);
        ed->setFilePath(st.value(QLatin1String(r.key),
                                 QString::fromUtf8(r.def)).toString());
        ed->setToolTip(QString::fromUtf8(r.tip));
        m_tree->setItemWidget(item, 1, ed);
    }

    /* ---- 点阵屏预览配色（原厂没有这一组）--------------------------------
     * 屏是单色的，但不同的点阵屏"亮/灭"呈现的颜色差很多：OLED 黑底白字、
     * STN 黄绿底黑字、蓝屏 LCD 蓝底白字……预览要接近真机就得能配。
     *
     * 【只影响预览】这两个颜色**不进任何资源文件**。资源里每个像素只有
     * "亮/灭"一个 bit，判定走的还是 Resbuilder.xml 的 bmp_transparent_color，
     * 和这里无关。改了也不会让产物变一个字节。 */
    auto *pvGrp = new QTreeWidgetItem(m_tree);
    pvGrp->setText(0, QStringLiteral("点阵屏预览"));
    pvGrp->setToolTip(0, QStringLiteral(
        "只影响编辑器里的预览效果，不写进资源文件."));
    auto addColor = [&](const char *cap, const char *key, const char *def) {
        auto *it = new QTreeWidgetItem(pvGrp);
        it->setText(0, QString::fromUtf8(cap));
        it->setToolTip(0, QStringLiteral("只影响预览，不写进资源文件."));
        it->setData(0, Qt::UserRole, QLatin1String(key));
        auto *ed = new ColorEdit(QString::fromUtf8(cap), m_tree);
        ed->setColor(QColor(st.value(QLatin1String(key),
                                     QLatin1String(def)).toString()));
        m_tree->setItemWidget(it, 1, ed);
        return it;
    };
    m_lit  = addColor("点亮颜色:", "Preview/LitColor", "#ffffff");
    m_dark = addColor("熄灭颜色:", "Preview/DarkColor", "#101010");
    /* 网格线是**实色**画的，亮区和熄灭区同一个颜色。所以它得能改 ——
     * 万一配得和点亮/熄灭色接近，那一片就看不见线了。 */
    m_grid = addColor("网格颜色:", "Preview/GridColor", "#808080");
    m_grid->setToolTip(0, QStringLiteral(
        "辅助线打开、放大到 300% 以上时画的像素网格.\n"
        "点亮和熄灭的像素上是同一个颜色，别配得和这两个太接近."));
    pvGrp->setExpanded(true);

    /* 【列宽按内容来，别写死】原厂那 100px 是照截图量的，它自己的
     * 「图片资源目录:」就被截成「图片资源目…」了。本版又多了带缩进的子项，
     * 写死的话「点亮颜色:」只剩三个字。让 Qt 按最长那条算，再留 8px。 */
    m_tree->resizeColumnToContents(0);
    m_tree->setColumnWidth(0, qMax(100, m_tree->columnWidth(0) + 8));

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);
    box->setObjectName(QStringLiteral("buttonBox"));
    connect(box, &QDialogButtonBox::accepted, this, &GlobalSettings::onAccepted);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(warn);
    root->addWidget(m_tree, 1);
    root->addWidget(box);
}

GlobalSettings::~GlobalSettings() = default;

void GlobalSettings::setProjectDir(const QString &dir)
{
    const QString want = dir.isEmpty() ? QString() : QDir(dir).absolutePath();
    if (g_cfgDir == want) {
        return;
    }
    /* 先把当前这份落盘再切 —— 不然刚改的预览文字/配色会跟着旧实例一起没了 */
    settings().sync();
    g_cfgDir = want;
    settings();          // 立刻建出新实例，顺便把目录建好
}

QString GlobalSettings::filePath()
{
    return configPath();
}

QVariant GlobalSettings::value(const QString &key, const QVariant &def)
{
    return settings().value(key, def);
}

void GlobalSettings::setValue(const QString &key, const QVariant &v)
{
    settings().setValue(key, v);
    settings().sync();
}

void GlobalSettings::onAccepted()
{
    QSettings &st = settings();

    if (auto *w = qobject_cast<QSpinBox *>(m_tree->itemWidget(m_width, 1))) {
        if (auto *h = qobject_cast<QSpinBox *>(m_tree->itemWidget(m_height, 1))) {
            st.setValue(QStringLiteral("Project/Size"),
                        QStringLiteral("%1*%2").arg(w->value()).arg(h->value()));
        }
    }

    for (QTreeWidgetItem *it : { m_lit, m_dark, m_grid }) {
        if (auto *ed = dynamic_cast<ColorEdit *>(m_tree->itemWidget(it, 1))) {
            st.setValue(it->data(0, Qt::UserRole).toString(), ed->color().name());
        }
    }

    /* 路径行都是顶层项，界面尺寸/点阵屏预览那两个组也在顶层，靠 UserRole 区分 */
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const QString key = item->data(0, Qt::UserRole).toString();
        if (key.isEmpty()) {
            continue;
        }
        /* FileEdit 是本文件里的局部类，没有 Q_OBJECT，用不了 qobject_cast；
         * QWidget 本身是多态的，dynamic_cast 够用。 */
        if (auto *ed = dynamic_cast<FileEdit *>(m_tree->itemWidget(item, 1))) {
            st.setValue(key, ed->filePath());
        }
    }
    st.sync();
    accept();
}
