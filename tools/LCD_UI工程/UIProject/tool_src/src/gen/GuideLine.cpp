#include "GuideLine.h"

#include <QPainter>

GuideLine::GuideLine(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

GuideLine::~GuideLine() = default;

void GuideLine::setCross(const QPoint &p)
{
    m_cross = p;
    setVisible(p.x() >= 0 && p.y() >= 0);
    update();
}

void GuideLine::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e)
    if (m_cross.x() < 0 || m_cross.y() < 0) {
        return;
    }
    QPainter p(this);
    QPen pen(QColor(220, 40, 40, 180), 1, Qt::DashLine);
    p.setPen(pen);
    p.drawLine(0, m_cross.y(), width(), m_cross.y());
    p.drawLine(m_cross.x(), 0, m_cross.x(), height());
}
