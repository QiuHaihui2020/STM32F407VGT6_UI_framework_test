#include "BaseDialog.h"
#include "GlobalSettings.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

/*
 * 原厂[全局设置]是五条**路径**设置，不是编辑器偏好。
 *
 * 五条的标题、说明文字、文件过滤器、两个按钮的名字，全部逐字来自
 * ui-tools.exe 里的字符串（扫描方法见 docs/FACTORY_UI.md 5.10）：
 *
 *   多国语言文件:   工程控件要用的语言文,可选office2003版本的xls文件,或者utf8格式,分号(;)间隔的csv文件.
 *                   xls 文件 , CSV UTF-8 文件 (*.xls *.csv )
 *   控件文件:       原始的模版控件文件,json格式.            json 文件 (*.json)
 *   图片资源目录:   工程中要用到的图片资源目录,默认是 images
 *   工程目录:       工程保存的目录,默认是程序运行目录.
 *   自定义控件目录: 自定义的模版控件目录,默认是widgets目录.
 *
 * 之前重建版这里放的是自己想出来的一套（网格间距、自动保存……），跟原厂
 * 完全对不上：用户来这里改多国语言表路径，结果压根没有这一项。
 *
 * 结构照 uic 留下的控件名来：treeWidget（两列）+ label_4（那条红底警示，
 * 内容是"更新设置要重启软件才能生效."）+ buttonBox。
 */

namespace {

/** QSettings 不可拷贝，给个共享实例。"QtProject" 这个组织名原厂二进制里就有。 */
QSettings &settings()
{
    static QSettings st(QStringLiteral("QtProject"), QStringLiteral("UITools"));
    return st;
}

struct PathRow {
    const char *key;
    const char *caption;
    const char *tip;
    const char *filter;     ///< 空 = 选目录，非空 = 选文件
};

const PathRow kRows[] = {
    { "path/langExcel", "多国语言文件:",
      "工程控件要用的语言文,可选office2003版本的xls文件,或者utf8格式,分号(;)间隔的csv文件.",
      "xls 文件 , CSV UTF-8 文件 (*.xls *.csv )" },
    { "path/controlJson", "控件文件:",
      "原始的模版控件文件,json格式.",
      "json 文件 (*.json)" },
    { "path/imageDir", "图片资源目录:",
      "工程中要用到的图片资源目录,默认是 images", "" },
    { "path/projectDir", "工程目录:",
      "工程保存的目录,默认是程序运行目录.", "" },
    { "path/widgetDir", "自定义控件目录:",
      "自定义的模版控件目录,默认是widgets目录.", "" },
};

/** 一行的值编辑器：路径框 + [ 文件选择... ] 或 [ 目录选择... ]。
 *  按钮文字前面那个空格是原厂的，照抄。 */
class PathEdit : public QWidget
{
public:
    PathEdit(const QString &filter, QWidget *parent = nullptr)
        : QWidget(parent), m_filter(filter)
    {
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        m_edit = new QLineEdit(this);
        auto *btn = new QPushButton(filter.isEmpty() ? QStringLiteral(" 目录选择...")
                                                     : QStringLiteral(" 文件选择..."),
                                    this);
        lay->addWidget(m_edit, 1);
        lay->addWidget(btn, 0);
        connect(btn, &QPushButton::clicked, this, [this]() {
            const QString p = m_filter.isEmpty()
                ? QFileDialog::getExistingDirectory(this, QStringLiteral(" 目录选择..."),
                                                    m_edit->text())
                : QFileDialog::getOpenFileName(this, QStringLiteral(" 文件选择..."),
                                               m_edit->text(), m_filter);
            if (!p.isEmpty()) {
                m_edit->setText(p);
            }
        });
    }

    QString path() const { return m_edit->text(); }
    void setPath(const QString &p) { m_edit->setText(p); }

private:
    QLineEdit *m_edit = nullptr;
    QString    m_filter;
};

} // namespace

GlobalSettings::GlobalSettings(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("全局设置"));
    resize(620, 300);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("treeWidget"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels(QStringList{ QStringLiteral("全局配置项"),
                                         QStringLiteral("内容") });
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 180);
    m_tree->setRootIsDecorated(false);
    /* 说明文字挂 tooltip 上：原厂那五句话在界面上是鼠标悬停才出来的，
     * 直接铺开会把这个窄对话框撑满。 */
    QSettings &st = settings();
    for (const PathRow &r : kRows) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString::fromUtf8(r.caption));
        item->setToolTip(0, QString::fromUtf8(r.tip));
        item->setData(0, Qt::UserRole, QLatin1String(r.key));
        auto *ed = new PathEdit(QString::fromUtf8(r.filter), m_tree);
        ed->setPath(st.value(QLatin1String(r.key)).toString());
        ed->setToolTip(QString::fromUtf8(r.tip));
        m_tree->setItemWidget(item, 1, ed);
    }

    auto *warn = new QLabel(QStringLiteral("更新设置要重启软件才能生效."), this);
    warn->setObjectName(QStringLiteral("label_4"));
    warn->setStyleSheet(QStringLiteral("background-color: rgb(223, 28, 28); color: white;"));
    warn->setMargin(4);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);
    box->setObjectName(QStringLiteral("buttonBox"));
    connect(box, &QDialogButtonBox::accepted, this, &GlobalSettings::onAccepted);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("软件的全局设置,需要重启软件后生效."), this));
    root->addWidget(m_tree, 1);
    root->addWidget(warn);
    root->addWidget(box);
}

GlobalSettings::~GlobalSettings() = default;

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
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        const QString key = item->data(0, Qt::UserRole).toString();
        /* PathEdit 是本文件里的局部类，没有 Q_OBJECT，用不了 qobject_cast；
         * QWidget 本身是多态的，dynamic_cast 够用。 */
        if (auto *ed = dynamic_cast<PathEdit *>(m_tree->itemWidget(item, 1))) {
            st.setValue(key, ed->path());
        }
    }
    st.sync();
    accept();
}
