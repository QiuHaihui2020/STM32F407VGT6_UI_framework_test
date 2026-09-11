/* BaseDialog.h —— 对话框基类。
 * 接口（基类 / signals / slots / properties / enums）别随手改签名，
 * 各处都按它连着；实现写在 src/ 下的同名 .cpp。
 */
#ifndef BASEDIALOG_H
#define BASEDIALOG_H

#include <QDialog>

class QDialogButtonBox;

/**
 * 把 QDialogButtonBox 的标准按钮改成中文。
 *
 * Qt 的标准按钮文字来自 qtbase 的翻译文件（qt_zh_CN.qm）。用官方 msvc 包时
 * 运行环境未必装了 translations，
 * 结果一堆中文对话框底下顶着 "OK / Cancel"。这里直接写死中文，不指望
 * 目标机器上有 .qm。
 */
void localizeButtonBox(QDialogButtonBox *box);

class BaseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BaseDialog(QWidget *parent = nullptr);
    ~BaseDialog() override;
};

#endif // BASEDIALOG_H
