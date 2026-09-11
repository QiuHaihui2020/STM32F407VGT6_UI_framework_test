#include "BaseDialog.h"
#include "MenuItemDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

MenuItemDialog::MenuItemDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("菜单条目列表"));
    resize(520, 360);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels(QStringList{ tr("图片"), tr("文字 ResID") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *add = new QPushButton(QStringLiteral("插入行"), this);
    auto *del = new QPushButton(QStringLiteral("删除当前行"), this);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_table, 1);
    auto *row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch();
    root->addLayout(row);
    root->addWidget(box);

    connect(add, &QPushButton::clicked, this, [this]() {
        m_table->insertRow(m_table->rowCount());
    });
    connect(del, &QPushButton::clicked, this, [this]() {
        const int r = m_table->currentRow();
        if (r >= 0) {
            m_table->removeRow(r);
        }
    });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

MenuItemDialog::~MenuItemDialog() = default;

void MenuItemDialog::setItems(const QVector<Item> &items)
{
    m_table->setRowCount(0);
    for (const Item &it : items) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(it.image));
        m_table->setItem(r, 1, new QTableWidgetItem(it.text));
    }
}

QVector<MenuItemDialog::Item> MenuItemDialog::items() const
{
    QVector<Item> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        Item it;
        it.image = m_table->item(r, 0) ? m_table->item(r, 0)->text() : QString();
        it.text = m_table->item(r, 1) ? m_table->item(r, 1)->text() : QString();
        out.append(it);
    }
    return out;
}
