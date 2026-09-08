/*
 * ProjectDialog.h —— 新建工程对话框
 *
 * 【接口来源】ui-tools.exe 的 moc 元数据：
 *     ★ public  slot void onAccepted()
 *     ★ private slot void on_pushButton_clicked()
 * 后者是 Qt 的自动连接命名（connectSlotsByName），说明界面上存在一个
 * objectName == "pushButton" 的按钮。
 *
 * 【界面还原】ProjectDialog.ui 被 uic 编译进了 exe，控件名以 QStringLiteral
 * 形式残留在 .rdata 里，用 re/strlit_scan.py 抓到的原始顺序是：
 *     buttonBox, label_3, prjname, layoutWidget2, verticalLayout,
 *     pushButton, ":/icon/icons/fileopen.png", view_lang, filestatus,
 *     groupBox, layoutWidget, gridLayout, label, spinBox, label_2,
 *     spinBox_2, layoutWidget1, horizontalLayout
 * 本文件用代码搭出同名控件（没有还原 .ui 文件本身，见 README「还原度」）。
 */
#ifndef PROJECTDIALOG_H
#define PROJECTDIALOG_H

#include <QDialog>
#include <QSize>
#include <QString>

class QLineEdit;
class QSpinBox;
class QLabel;
class QListWidget;
class QDialogButtonBox;

class ProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectDialog(QWidget *parent = nullptr);
    ~ProjectDialog() override;

    QString projectName() const;
    QSize   pageSize() const;
    QString languageExcel() const { return m_langXls; }

public slots:
    void onAccepted();                  ///< ★

private slots:
    void on_pushButton_clicked();       ///< ★ 选多国语言 xls

private:
    QLineEdit        *m_prjname = nullptr;   // objectName "prjname"
    QSpinBox         *m_spinBox = nullptr;   // "spinBox"    宽
    QSpinBox         *m_spinBox2 = nullptr;  // "spinBox_2"  高
    QLabel           *m_filestatus = nullptr;// "filestatus"
    QListWidget      *m_viewLang = nullptr;  // "view_lang"
    QDialogButtonBox *m_buttonBox = nullptr; // "buttonBox"
    QString           m_langXls;
};

#endif // PROJECTDIALOG_H
