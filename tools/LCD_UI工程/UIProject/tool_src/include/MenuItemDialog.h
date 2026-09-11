/* MenuItemDialog.h ——
 * 列表类控件（VerticalList / HorizontalList / NewGrid）的"菜单项"编辑：
 * 一行一项，编辑图标 + 文字 ResID，确定后由调用方生成子控件。
 */
#ifndef MENUITEMDIALOG_H
#define MENUITEMDIALOG_H

#include <QDialog>
#include <QStringList>

class QTableWidget;

class MenuItemDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MenuItemDialog(QWidget *parent = nullptr);
    ~MenuItemDialog() override;

    struct Item {
        QString image;   ///< 相对工程目录的图片路径
        QString text;    ///< 多国语言表里的 ResID
    };

    void         setItems(const QVector<Item> &items);
    QVector<Item> items() const;

private:
    QTableWidget *m_table = nullptr;
};

#endif // MENUITEMDIALOG_H
