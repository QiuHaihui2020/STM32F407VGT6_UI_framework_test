#include "RuleWidget.h"

#include <QPainter>

RuleWidget::RuleWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(18);
}

RuleWidget::~RuleWidget() = default;

void RuleWidget::setOrientation(Qt::Orientation o)
{
    m_orient = o;
    if (o == Qt::Horizontal) {
        setFixedHeight(18);
        setMaximumWidth(QWIDGETSIZE_MAX);
    } else {
        setFixedWidth(18);
        setMaximumHeight(QWIDGETSIZE_MAX);
    }
    update();
}

void RuleWidget::setZoom(int percent)
{
    m_zoom = qMax(1, percent);
    update();
}

void RuleWidget::setOffset(int px)
{
    m_offset = px;
    update();
}

void RuleWidget::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e)
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF0, 0xF0, 0xF0));
    p.setPen(QColor(0x80, 0x80, 0x80));

    // 刻度按"工程像素"算，缩放后再换成屏幕像素；
    // 每 10 个工程像素画短线、每 50 个画长线加数字。
    const double scale = m_zoom / 100.0;
    const int span = (m_orient == Qt::Horizontal) ? width() : height();
    for (int v = 0;; v += 10) {
        const int s = int(v * scale) - m_offset;
        if (s > span) {
            break;
        }
        if (s < 0) {
            continue;
        }
        const bool major = (v % 50) == 0;
        const int len = major ? 10 : 4;
        if (m_orient == Qt::Horizontal) {
            p.drawLine(s, height() - len, s, height());
            if (major) {
                p.drawText(s + 2, height() - 10, QString::number(v));
            }
        } else {
            p.drawLine(width() - len, s, width(), s);
            if (major) {
                p.save();
                p.translate(width() - 12, s + 2);
                p.rotate(90);
                p.drawText(0, 0, QString::number(v));
                p.restore();
            }
        }
    }
}
