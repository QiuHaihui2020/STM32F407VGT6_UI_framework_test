/* BaseDialog.cpp —— 生成的默认实现骨架。
 * 槽体是空的：原始实现是编译过的机器码，无法还原，只能按行为重写。
 */
#include "BaseDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>

void localizeButtonBox(QDialogButtonBox *box)
{
    if (!box) {
        return;
    }
    if (QPushButton *b = box->button(QDialogButtonBox::Ok)) {
        b->setText(QStringLiteral("确定"));
    }
    if (QPushButton *b = box->button(QDialogButtonBox::Cancel)) {
        b->setText(QStringLiteral("取消"));
    }
    if (QPushButton *b = box->button(QDialogButtonBox::Close)) {
        b->setText(QStringLiteral("关闭"));
    }
}

BaseDialog::BaseDialog(QWidget *parent)
    : QDialog(parent)
{
}

BaseDialog::~BaseDialog() = default;
