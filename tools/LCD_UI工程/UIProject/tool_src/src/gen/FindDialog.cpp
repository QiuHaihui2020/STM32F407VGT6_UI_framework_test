#include "FindDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FindDialog::FindDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("查找控件"));

    auto *label = new QLabel(QStringLiteral("控件名称："), this);
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

    connect(btn, &QPushButton::clicked, this, &FindDialog::onStartSearch);
    connect(m_edit, &QLineEdit::returnPressed, this, &FindDialog::onStartSearch);
}

FindDialog::~FindDialog() = default;

QString FindDialog::keyword() const
{
    return m_edit->text().trimmed();
}

void FindDialog::onStartSearch()
{
    const QString k = keyword();
    if (!k.isEmpty()) {
        emit findNext(k);
    }
}
