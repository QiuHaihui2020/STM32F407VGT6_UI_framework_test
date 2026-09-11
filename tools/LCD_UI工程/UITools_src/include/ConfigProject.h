/* ConfigProject.h ——
 * 控件名：prjname / openfile / view_lang / filestatus /
 * lang_selectall / lang_dselectall / lang_re
 *
 * 用途：工程级配置 —— 工程名、多国语言 xls 的路径、启用哪些语言。
 * 语言勾选的结果就是 Resbuilder.xml 里那个 <language> 位掩码
 * （bit i 置位 = 启用第 i+1 号语言，见 result.h 里的 Chinese_Simplified=1 …）。
 */
#ifndef CONFIGPROJECT_H
#define CONFIGPROJECT_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QListWidget;

class ConfigProject : public QDialog
{
    Q_OBJECT

public:
    explicit ConfigProject(QWidget *parent = nullptr);
    ~ConfigProject() override;

    void    setProjectName(const QString &n);
    QString projectName() const;

    void    setExcelPath(const QString &p);
    QString excelPath() const { return m_excel; }

    /// 语言位掩码，与 Resbuilder.xml 的 <language> 同一套编码
    void    setLanguageMask(quint32 mask);
    quint32 languageMask() const;

private slots:
    void on_openfile_clicked();
    void on_lang_selectall_clicked();
    void on_lang_dselectall_clicked();
    void on_lang_re_clicked();

private:
    void refreshFileStatus();

    QLineEdit   *m_prjname = nullptr;
    QLabel      *m_filestatus = nullptr;
    QListWidget *m_viewLang = nullptr;
    QString      m_excel;
};

#endif // CONFIGPROJECT_H
