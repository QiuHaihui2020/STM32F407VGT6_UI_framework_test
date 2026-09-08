/* I18nLanguage.h —— 类名/基类/槽签名来自 ui-tools.exe 的 moc 元数据；
 * 控件名（item_selectall / item_dselectall / item_re / itemSelected /
 * btnUp / btnDown / btn_ok / btnCancel）来自 uic 留在二进制里的 QStringLiteral。
 *
 * 用途：编辑 Text 控件的"文字列表"（json 里 -type == "text-pic" 的属性）。
 * 列表项是多国语言表里的 ResID（m1 / m30 …），顺序即资源顺序。
 */
#ifndef I18NLANGUAGE_H
#define I18NLANGUAGE_H

#include <QDialog>
#include <QModelIndex>
#include <QStringList>

class QListWidget;
class QLineEdit;
class QSpinBox;

class I18nLanguage : public QDialog
{
    Q_OBJECT

public:
    explicit I18nLanguage(QWidget *parent = nullptr);
    ~I18nLanguage() override;

    /// 多国语言 .xls（BIFF8）。第一列是 ResID，其余列是各语言译文。
    bool loadExcel(const QString &xlsPath, QString *error = nullptr);

    void        setSelected(const QStringList &ids);
    QStringList selected() const;
    int         maxCount() const { return m_maxCount; }
    void        setMaxCount(int n) { m_maxCount = n; }

    /** 字号。0 = 用控件自己的默认字号（原厂那句"字号(0 表示默认):"就是它）。 */
    int         fontSize() const;
    void        setFontSize(int px);

private slots:
    void on_item_selectall_clicked();
    void on_item_dselectall_clicked();
    void on_item_re_clicked();
    void onItemSelected(QModelIndex index);
    void onBtnUpClicked();
    void onBtnDownClicked();

private:
    void applyFilter();
    void moveCurrent(int delta);

    QListWidget *m_itemWidget = nullptr;     ///< 全部 ResID，带勾选框
    QListWidget *m_itemSelected = nullptr;   ///< 已选，顺序即资源顺序
    QLineEdit   *m_filter = nullptr;
    QSpinBox    *m_fontSize = nullptr;
    int          m_maxCount = 0;
};

#endif // I18NLANGUAGE_H
