#include "ScaleDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

/*
 * 界面文字：
 *     工程缩放 / 界面尺寸 / 当前尺寸: / 新的尺寸: / 更新工程尺寸
 *     按比例缩放整页。不成比例的话会截断，坐标也会被清零。
 *     注意: 该工程对应的图片资源也要进行缩放，否则显示不完整.
 * uic 留下的控件名：lab_oldw / lab_oldh / spinBoxW / spinBoxH / groupBox / buttonBox
 *
 * 这个"工程缩放"**不是画布缩放**，是把整个工程换一块屏幕尺寸，所有控件坐标
 * 按比例换算（128x64 改成 240x240 之类）。画布那个百分比缩放是另一回事，
 * 在 CanvasPage::setZoom()。
 */

ScaleDialog::ScaleDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("工程缩放"));

    m_oldW = new QLabel(this);
    m_oldW->setObjectName(QStringLiteral("lab_oldw"));
    m_oldH = new QLabel(this);
    m_oldH->setObjectName(QStringLiteral("lab_oldh"));
    m_w = new QSpinBox(this);
    m_w->setObjectName(QStringLiteral("spinBoxW"));
    m_h = new QSpinBox(this);
    m_h->setObjectName(QStringLiteral("spinBoxH"));
    /* 整数输入框的上限提示是"9999 内的整数" */
    m_w->setRange(1, 9999);
    m_h->setRange(1, 9999);

    auto *gb = new QGroupBox(QStringLiteral("界面尺寸"), this);
    gb->setObjectName(QStringLiteral("groupBox"));
    auto *form = new QFormLayout(gb);
    auto *curRow = new QWidget(gb);
    auto *curLay = new QFormLayout(curRow);
    curLay->setContentsMargins(0, 0, 0, 0);
    curLay->addRow(QStringLiteral("宽度:"), m_oldW);
    curLay->addRow(QStringLiteral("高度:"), m_oldH);
    form->addRow(QStringLiteral("当前尺寸:"), curRow);

    auto *newRow = new QWidget(gb);
    auto *newLay = new QFormLayout(newRow);
    newLay->setContentsMargins(0, 0, 0, 0);
    newLay->addRow(QStringLiteral("宽度:"), m_w);
    newLay->addRow(QStringLiteral("高度:"), m_h);
    form->addRow(QStringLiteral("新的尺寸:"), newRow);

    auto *box = new QDialogButtonBox(this);
    box->setObjectName(QStringLiteral("buttonBox"));
    /* 确定按钮叫"更新工程尺寸"，不是 OK */
    box->addButton(QStringLiteral("更新工程尺寸"), QDialogButtonBox::AcceptRole);
    box->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral(
        "按比例缩放整页。不成比例的话会截断，坐标也会被清零。"), this));
    root->addWidget(gb);
    auto *note = new QLabel(QStringLiteral(
        "注意: 该工程对应的图片资源也要进行缩放，否则显示不完整."), this);
    note->setWordWrap(true);
    root->addWidget(note);
    root->addWidget(box);
}

ScaleDialog::~ScaleDialog() = default;

void ScaleDialog::setOldSize(const QSize &s)
{
    m_oldW->setText(QString::number(s.width()));
    m_oldH->setText(QString::number(s.height()));
    m_w->setValue(s.width());
    m_h->setValue(s.height());
}

QSize ScaleDialog::newSize() const
{
    return QSize(m_w->value(), m_h->value());
}
