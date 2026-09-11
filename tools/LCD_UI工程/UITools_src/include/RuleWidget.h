/* RuleWidget.h ——
 * 画布边上的标尺。横竖两种用同一个类，按 orientation 分。
 */
#ifndef RULEWIDGET_H
#define RULEWIDGET_H

#include <QWidget>

class RuleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RuleWidget(QWidget *parent = nullptr);
    ~RuleWidget() override;

    void setOrientation(Qt::Orientation o);
    /// 画布缩放百分比；刻度间距要跟着变
    void setZoom(int percent);
    void setOffset(int px);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    Qt::Orientation m_orient = Qt::Horizontal;
    int m_zoom = 100;
    int m_offset = 0;
};

#endif // RULEWIDGET_H
