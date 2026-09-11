#include "ProgressDlg.h"

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

ProgressDlg::ProgressDlg(QWidget *parent)
    : BaseDialog(parent)
{
    setWindowTitle(tr("请稍候"));
    setModal(true);
    setFixedWidth(360);

    m_label = new QLabel(this);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);

    auto *root = new QVBoxLayout(this);
    root->addWidget(m_label);
    root->addWidget(m_bar);
}

ProgressDlg::~ProgressDlg() = default;

void ProgressDlg::setRange(int lo, int hi)
{
    m_bar->setRange(lo, hi);
}

void ProgressDlg::setValue(int v)
{
    m_bar->setValue(v);
}

void ProgressDlg::setText(const QString &t)
{
    m_label->setText(t);
}
