#include "GridHelpLine.h"

#include <QPainter>

GridHelpLine::GridHelpLine(QWidget *parent)
    : QWidget(parent)
{
    // 纯覆盖层：不吃鼠标事件，也不画自己的背景
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

GridHelpLine::~GridHelpLine() = default;

void GridHelpLine::setStep(int px)
{
    m_step = qMax(1, px);
    update();
}

void GridHelpLine::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e)
    if (m_step <= 1) {
        return;
    }
    QPainter p(this);
    p.setPen(QPen(QColor(0, 0, 0, 40), 1));
    for (int x = m_step; x < width(); x += m_step) {
        p.drawLine(x, 0, x, height());
    }
    for (int y = m_step; y < height(); y += m_step) {
        p.drawLine(0, y, width(), y);
    }
}
