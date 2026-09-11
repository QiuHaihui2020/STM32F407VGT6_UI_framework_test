#include "BaseDialog.h"
#include "I18nLanguage.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "XlsReader.h"
#include <QFileInfo>

I18nLanguage::I18nLanguage(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("显示列表"));
    resize(720, 520);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("过滤 ResID 或译文"));
    m_itemWidget = new QListWidget(this);
    m_itemSelected = new QListWidget(this);

    auto *selAll = new QPushButton(QStringLiteral("全选"), this);
    auto *dselAll = new QPushButton(QStringLiteral("全不选"), this);
    auto *re = new QPushButton(QStringLiteral("反选"), this);
    auto *up = new QPushButton(QIcon(QStringLiteral(":/icon/icons/go-up.png")),
                               QStringLiteral("上移一行"), this);
    auto *down = new QPushButton(QIcon(QStringLiteral(":/icon/icons/go-down.png")),
                                 QStringLiteral("下移一行"), this);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);

    auto *leftBtns = new QHBoxLayout;
    leftBtns->addWidget(selAll);
    leftBtns->addWidget(dselAll);
    leftBtns->addWidget(re);
    leftBtns->addStretch();

    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(QStringLiteral("语言列表"), this));
    left->addWidget(m_filter);
    left->addWidget(m_itemWidget);
    left->addLayout(leftBtns);

    auto *rightBtns = new QHBoxLayout;
    rightBtns->addWidget(up);
    rightBtns->addWidget(down);
    rightBtns->addStretch();

    auto *right = new QVBoxLayout;
    /* 字号：0 表示"不指定，跟控件默认走"，
     * 别把 0 当成"字号 0px"去渲染。 */
    m_fontSize = new QSpinBox(this);
    m_fontSize->setRange(0, 9999);
    m_fontSize->setToolTip(QStringLiteral("9999 内的整数"));
    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(new QLabel(QStringLiteral("字号(0 表示默认):"), this));
    fontRow->addWidget(m_fontSize);
    fontRow->addStretch();

    right->addWidget(new QLabel(QStringLiteral("已选列表"), this));
    right->addWidget(m_itemSelected);
    right->addLayout(rightBtns);
    right->addLayout(fontRow);

    auto *row = new QHBoxLayout;
    row->addLayout(left, 3);
    row->addLayout(right, 2);

    auto *root = new QVBoxLayout(this);
    root->addLayout(row, 1);
    root->addWidget(box);

    connect(selAll, &QPushButton::clicked, this, &I18nLanguage::on_item_selectall_clicked);
    connect(dselAll, &QPushButton::clicked, this, &I18nLanguage::on_item_dselectall_clicked);
    connect(re, &QPushButton::clicked, this, &I18nLanguage::on_item_re_clicked);
    connect(up, &QPushButton::clicked, this, &I18nLanguage::onBtnUpClicked);
    connect(down, &QPushButton::clicked, this, &I18nLanguage::onBtnDownClicked);
    connect(m_filter, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_itemWidget, &QListWidget::clicked, this, &I18nLanguage::onItemSelected);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 勾选框变化 -> 同步右边的已选列表
    connect(m_itemWidget, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
        const QString id = it->data(Qt::UserRole).toString();
        int at = -1;
        for (int i = 0; i < m_itemSelected->count(); ++i) {
            if (idOf(m_itemSelected->item(i)) == id) {
                at = i;
                break;
            }
        }
        if (it->checkState() == Qt::Checked) {
            if (at < 0) {
                if (m_maxCount > 0 && m_itemSelected->count() >= m_maxCount) {
                    QSignalBlocker b(m_itemWidget);
                    it->setCheckState(Qt::Unchecked);
                    QMessageBox::information(this, QStringLiteral("提示"),
                                             tr("最多只能放 %1 项").arg(m_maxCount));
                    return;
                }
                addSelectedId(id);
            }
        } else if (at >= 0) {
            delete m_itemSelected->takeItem(at);
        }
    });
}

I18nLanguage::~I18nLanguage() = default;

bool I18nLanguage::loadExcel(const QString &xlsPath, QString *error)
{
    /* 失败的三种情形：
     *   文件不在        -> "XLS文件找不到."
     *   文件在但打不开  -> "xls 打不开,请查看[全局设置]里的路径目录是否正确"
     *   打开了但内容不对 -> "XLS文件内容不正确,不能有单元合并的单元,请选择一个正解的文件."
     * 特意点名"单元合并"：BIFF8 里合并单元格只有左上角那格有值，
     * 其余是空的，按行列取会整列错位。 */
    if (xlsPath.isEmpty() || !QFileInfo::exists(xlsPath)) {
        if (error) {
            *error = QStringLiteral("XLS文件找不到.");
        }
        return false;
    }
    res::XlsReader xls;
    if (!xls.load(xlsPath)) {
        if (error) {
            *error = QStringLiteral("xls 打不开,请查看[全局设置]里的路径目录是否正确");
        }
        return false;
    }
    if (xls.sheets().isEmpty() || xls.sheets().first().rows.size() < 2) {
        if (error) {
            *error = QStringLiteral(
                "XLS文件内容不正确,不能有单元合并的单元,请选择一个正解的文件.");
        }
        return false;
    }
    const res::XlsSheet &sh = xls.sheets().first();
    m_itemWidget->clear();
    m_labelOfId.clear();
    // 第 0 行是表头（ResID, Chinese_Simplified, …），从第 1 行起是数据
    for (int r = 1; r < sh.rows.size(); ++r) {
        const QString id = sh.cell(r, 0).trimmed();
        if (id.isEmpty()) {
            continue;
        }
        const QString zh = sh.cell(r, 1).trimmed();
        const QString en = sh.cell(r, 5).trimmed();
        /* 【显示成"内容#ResID"】就是这个写法（
         * "蓝牙#m1"）—— 一眼看到的是**这条到底是什么字**，ResID 只是尾巴。
         * 以前这里是 "m1    蓝牙    Bluetooth"，先看到的是没有意义的编号，
         * 而且已选列表那边只显示 "m1"，根本对不上是哪句话。
         * 英文放进 tooltip，不占行宽。 */
        const QString label = zh.isEmpty() ? id : QStringLiteral("%1#%2").arg(zh, id);
        m_labelOfId.insert(id, label);
        auto *it = new QListWidgetItem(label);
        if (!en.isEmpty()) {
            it->setToolTip(QStringLiteral("%1\n%2").arg(zh, en));
        }
        it->setData(Qt::UserRole, id);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        m_itemWidget->addItem(it);
    }
    return true;
}

void I18nLanguage::setSelected(const QStringList &ids)
{
    m_itemSelected->clear();
    for (const QString &id : ids) {
        if (!id.isEmpty()) {
            addSelectedId(id);
        }
    }
    QSignalBlocker b(m_itemWidget);
    for (int i = 0; i < m_itemWidget->count(); ++i) {
        QListWidgetItem *it = m_itemWidget->item(i);
        it->setCheckState(ids.contains(it->data(Qt::UserRole).toString())
                          ? Qt::Checked : Qt::Unchecked);
    }
}

QString I18nLanguage::labelOf(const QString &id) const
{
    /* 表还没读进来（或这条 ResID 表里没有）就退回裸 ResID —— 总比空着强 */
    return m_labelOfId.value(id, id);
}

QString I18nLanguage::idOf(const QListWidgetItem *it)
{
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void I18nLanguage::addSelectedId(const QString &id, int at)
{
    auto *it = new QListWidgetItem(labelOf(id));
    it->setData(Qt::UserRole, id);          // 值永远是纯 ResID
    if (at < 0) {
        m_itemSelected->addItem(it);
    } else {
        m_itemSelected->insertItem(at, it);
    }
}

QString I18nLanguage::selectedLabelForTest(int i) const
{
    const QListWidgetItem *it = m_itemSelected->item(i);
    return it ? it->text() : QString();
}

QStringList I18nLanguage::selected() const
{
    QStringList out;
    for (int i = 0; i < m_itemSelected->count(); ++i) {
        out.append(idOf(m_itemSelected->item(i)));
    }
    return out;
}

void I18nLanguage::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int i = 0; i < m_itemWidget->count(); ++i) {
        QListWidgetItem *it = m_itemWidget->item(i);
        it->setHidden(!f.isEmpty() && !it->text().contains(f, Qt::CaseInsensitive));
    }
}

void I18nLanguage::on_item_selectall_clicked()
{
    for (int i = 0; i < m_itemWidget->count(); ++i) {
        QListWidgetItem *it = m_itemWidget->item(i);
        if (!it->isHidden()) {
            it->setCheckState(Qt::Checked);
        }
    }
}

void I18nLanguage::on_item_dselectall_clicked()
{
    for (int i = 0; i < m_itemWidget->count(); ++i) {
        m_itemWidget->item(i)->setCheckState(Qt::Unchecked);
    }
}

void I18nLanguage::on_item_re_clicked()
{
    for (int i = 0; i < m_itemWidget->count(); ++i) {
        QListWidgetItem *it = m_itemWidget->item(i);
        if (it->isHidden()) {
            continue;
        }
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
}

void I18nLanguage::onItemSelected(QModelIndex index)
{
    // 点行也当成切换勾选，省一次瞄准复选框的操作
    QListWidgetItem *it = m_itemWidget->item(index.row());
    if (it) {
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
}

void I18nLanguage::moveCurrent(int delta)
{
    const int r = m_itemSelected->currentRow();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= m_itemSelected->count()) {
        return;
    }
    QListWidgetItem *it = m_itemSelected->takeItem(r);
    m_itemSelected->insertItem(to, it);
    m_itemSelected->setCurrentRow(to);
}

void I18nLanguage::onBtnUpClicked()
{
    moveCurrent(-1);
}

void I18nLanguage::onBtnDownClicked()
{
    moveCurrent(1);
}

int I18nLanguage::fontSize() const
{
    return m_fontSize ? m_fontSize->value() : 0;
}

void I18nLanguage::setFontSize(int px)
{
    if (m_fontSize) {
        m_fontSize->setValue(px);
    }
}
