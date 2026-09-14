/* AlignGuide.h ——
 * 画布上的像素网格层：透明、不吃鼠标事件，只负责画格子。
 */
#ifndef GRIDHELPLINE_H
#define GRIDHELPLINE_H

#include <QWidget>

class AlignGuide : public QWidget
{
    Q_OBJECT

public:
    explicit AlignGuide(QWidget *parent = nullptr);
    ~AlignGuide() override;

    void setStep(int px);
    int  step() const { return m_step; }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    int m_step = 8;
};

#endif // GRIDHELPLINE_H
