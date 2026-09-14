/* ProgressBox.h ——
 * 生成资源、批量转换图片这类耗时操作的进度框。
 */
#ifndef PROGRESSDLG_H
#define PROGRESSDLG_H

#include "BaseDialog.h"

class QProgressBar;
class QLabel;

class ProgressBox : public BaseDialog
{
    Q_OBJECT

public:
    explicit ProgressBox(QWidget *parent = nullptr);
    ~ProgressBox() override;

    void setRange(int lo, int hi);
    void setValue(int v);
    void setText(const QString &t);

private:
    QProgressBar *m_bar = nullptr;
    QLabel       *m_label = nullptr;
};

#endif // PROGRESSDLG_H
