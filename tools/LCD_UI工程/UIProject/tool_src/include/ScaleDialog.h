/* ScaleDialog.h ——
 * 控件名：lab_oldw / lab_oldh / spinBoxW / spinBoxH / groupBox / buttonBox
 *
 * 注意：这个"工程缩放"**不是画布缩放**，是把整个工程换一个屏幕尺寸，
 * 所有控件坐标按比例缩放（128x64 的工程改成 240x240 之类）。
 * 画布那个百分比缩放是另一回事，在 CanvasPage::setZoom()。
 */
#ifndef ZOOMPROJECT_H
#define ZOOMPROJECT_H

#include <QDialog>
#include <QSize>

class QSpinBox;
class QLabel;

class ScaleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScaleDialog(QWidget *parent = nullptr);
    ~ScaleDialog() override;

    void  setOldSize(const QSize &s);
    QSize newSize() const;

private:
    QLabel   *m_oldW = nullptr;
    QLabel   *m_oldH = nullptr;
    QSpinBox *m_w = nullptr;
    QSpinBox *m_h = nullptr;
};

#endif // ZOOMPROJECT_H
