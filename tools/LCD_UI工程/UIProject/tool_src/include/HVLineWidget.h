/* HVLineWidget.h ——
 * 拖动控件时显示的十字辅助线（对齐参考），同样是透明的覆盖层。
 */
#ifndef HVLINEWIDGET_H
#define HVLINEWIDGET_H

#include <QPoint>
#include <QWidget>

class HVLineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HVLineWidget(QWidget *parent = nullptr);
    ~HVLineWidget() override;

    /// 传一个无效点就把线藏起来
    void setCross(const QPoint &p);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QPoint m_cross{ -1, -1 };
};

#endif // HVLINEWIDGET_H
