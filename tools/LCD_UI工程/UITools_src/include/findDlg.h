/* findDlg.h —— 类名/基类/槽签名来自 ui-tools.exe 的 moc 元数据；
 * 控件名（label / lineEdit / pushButton）来自 uic 的 QStringLiteral。
 *
 * 用途：按 ename 或名字在对象树里找控件。找到就发 found()，由主窗口去选中。
 */
#ifndef FINDDLG_H
#define FINDDLG_H

#include <QDialog>

class QLineEdit;

class findDlg : public QDialog
{
    Q_OBJECT

public:
    explicit findDlg(QWidget *parent = nullptr);
    ~findDlg() override;

    QString keyword() const;

signals:
    /// 每点一次"查找"发一条；主窗口负责在树里定位下一个匹配项
    void findNext(const QString &keyword);

private slots:
    void onStartSearch();

private:
    QLineEdit *m_edit = nullptr;
};

#endif // FINDDLG_H
