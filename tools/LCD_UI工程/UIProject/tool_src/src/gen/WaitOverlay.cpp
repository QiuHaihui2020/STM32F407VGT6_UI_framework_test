#include "WaitOverlay.h"

#include <QPainter>
#include <QTimer>

WaitOverlay::WaitOverlay(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setModal(true);
    setFixedSize(140, 140);
    m_timer = new QTimer(this);
    m_timer->setInterval(80);
    connect(m_timer, &QTimer::timeout, this, &WaitOverlay::onRotate);
}

WaitOverlay::~WaitOverlay() = default;

void WaitOverlay::setText(const QString &t)
{
    m_text = t;
    update();
}

void WaitOverlay::onStart()
{
    m_angle = 0;
    m_timer->start();
    show();
}

void WaitOverlay::onStop()
{
    m_timer->stop();
    hide();
}

void WaitOverlay::onRotate()
{
    m_angle = (m_angle + 30) % 360;
    update();
}

void WaitOverlay::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0, 0, 0, 160));

    const QPointF c(width() / 2.0, height() / 2.0 - 8);
    const qreal r = 26;
    for (int i = 0; i < 12; ++i) {
        // 离当前角度越远越淡，转起来就是常见的那种"跑马灯"
        const int fade = (i * 30 - m_angle + 360) % 360;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 40 + 215 * (360 - fade) / 360));
        p.save();
        p.translate(c);
        p.rotate(i * 30);
        p.drawEllipse(QPointF(0, -r), 3.2, 3.2);
        p.restore();
    }
    if (!m_text.isEmpty()) {
        p.setPen(Qt::white);
        p.drawText(QRect(0, height() - 34, width(), 24), Qt::AlignCenter, m_text);
    }
}
