#include "findDlg.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

findDlg::findDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("查找对像"));   // 沿用"对像"这个写法

    auto *label = new QLabel(QStringLiteral("对像名称:"), this);
    m_edit = new QLineEdit(this);
    auto *btn = new QPushButton(QStringLiteral("查找"), this);
    btn->setDefault(true);

    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(btn);

    auto *root = new QVBoxLayout(this);
    root->addWidget(label);
    root->addWidget(m_edit);
    root->addLayout(row);

    connect(btn, &QPushButton::clicked, this, &findDlg::onStartSearch);
    connect(m_edit, &QLineEdit::returnPressed, this, &findDlg::onStartSearch);
}

findDlg::~findDlg() = default;

QString findDlg::keyword() const
{
    return m_edit->text().trimmed();
}

void findDlg::onStartSearch()
{
    const QString k = keyword();
    if (!k.isEmpty()) {
        emit findNext(k);
    }
}
