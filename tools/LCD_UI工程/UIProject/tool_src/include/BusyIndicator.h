/* BusyIndicator.h ——
 *
 * 一个无边框的转圈提示。onRotate() 是定时器槽 —— 拿 QTimer
 * 每 80 ms 转一格，所以这个槽才会出现在元数据里。
 */
#ifndef BUSYINDICATOR_H
#define BUSYINDICATOR_H

#include <QDialog>

class QTimer;

class BusyIndicator : public QDialog
{
    Q_OBJECT

public:
    explicit BusyIndicator(QWidget *parent = nullptr);
    ~BusyIndicator() override;

    void setText(const QString &t);

public slots:
    void onStart();
    void onStop();
    void onRotate();

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QTimer *m_timer = nullptr;
    int     m_angle = 0;
    QString m_text;
};

#endif // BUSYINDICATOR_H
