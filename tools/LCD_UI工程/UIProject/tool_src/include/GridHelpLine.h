/* GridHelpLine.h ——
 * 画布上的像素网格层：透明、不吃鼠标事件，只负责画格子。
 */
#ifndef GRIDHELPLINE_H
#define GRIDHELPLINE_H

#include <QWidget>

class GridHelpLine : public QWidget
{
    Q_OBJECT

public:
    explicit GridHelpLine(QWidget *parent = nullptr);
    ~GridHelpLine() override;

    void setStep(int px);
    int  step() const { return m_step; }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    int m_step = 8;
};

#endif // GRIDHELPLINE_H
