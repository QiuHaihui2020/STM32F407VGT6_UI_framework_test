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
#include <QHash>
#include <QModelIndex>
#include <QStringList>

class QListWidget;
class QListWidgetItem;
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
    /**
     * 往"已选列表"里加一条。
     *
     * 【显示"内容#ResID"，但值仍是纯 ResID】原厂两边显示的都是"蓝牙#m1"
     * （见 temp/文字列表.jpg）。以前已选那边只显示 "m1"，看不出是哪句话。
     * 真正的值（写回工程的那个 ResID）存在 Qt::UserRole 上，selected()
     * 取的是它 —— 显示怎么变都不会写错。
     */
    void addSelectedId(const QString &id, int at = -1);

public:
    /** 自测：已选列表第 i 条**显示出来**的字（应当是"内容#ResID"）。 */
    QString selectedLabelForTest(int i) const;

private:
    /** ResID -> 显示用的"内容#ResID"，loadExcel 时建。 */
    QString labelOf(const QString &id) const;
    /** 已选列表某一行的 ResID。 */
    static QString idOf(const QListWidgetItem *it);

    QListWidget *m_itemWidget = nullptr;     ///< 全部 ResID，带勾选框
    QListWidget *m_itemSelected = nullptr;   ///< 已选，顺序即资源顺序
    QHash<QString, QString> m_labelOfId;     ///< ResID -> "内容#ResID"
    QLineEdit   *m_filter = nullptr;
    QSpinBox    *m_fontSize = nullptr;
    int          m_maxCount = 0;
};

#endif // I18NLANGUAGE_H
