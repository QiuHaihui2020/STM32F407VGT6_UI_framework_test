/* ImageFileDialog.h —— 类名/基类/槽签名来自 ui-tools.exe 的 moc 元数据，
 * 界面控件名来自 uic 留在二进制里的 QStringLiteral（treeView / listView …）。
 * 槽体是按行为重写的：原始实现是编译过的机器码，还原不了。
 *
 * 用途：编辑控件的图片列表属性（json 里 -type 为 piclist / arrlist 的那些，
 * 例如 ImageList 的 normal_image、Battery 的 image、Time 的 number）。
 * 返回的路径是**相对工程目录**的正斜杠形式，和原厂工程文件里一致
 * （"config/pic_lcd/BATTLVL1.BMP"）。
 */
#ifndef IMAGEFILEDIALOG_H
#define IMAGEFILEDIALOG_H

#include <QDialog>
#include <QModelIndex>
#include <QStringList>

class QFileSystemModel;
class QTreeView;
class QListView;
class QListWidget;

class ImageFileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageFileDialog(QWidget *parent = nullptr);
    ~ImageFileDialog() override;

    /// 工程目录；左侧目录树的根，也是相对路径的基准
    void setProjectDir(const QString &dir);
    /// 允许的最大条目数，0 = 不限（对应 json 里的 maxlength）
    void setMaxCount(int n) { m_maxCount = n; }

    void        setSelected(const QStringList &rel);
    QStringList selected() const;

private slots:
    void onTreeViewClicked(QModelIndex index);
    void onListViewDoubleClicked(QModelIndex index);
    void onSelListViewDoubleClicked(QModelIndex index);
    void onDelSelectedItems();
    void onAddSelectedItems();
    void onUp();
    void onDown();

private:
    void addPath(const QString &absPath);
    void moveCurrent(int delta);

    QFileSystemModel *m_dirModel = nullptr;
    QFileSystemModel *m_fileModel = nullptr;
    QTreeView        *m_treeView = nullptr;
    QListView        *m_listView = nullptr;
    QListWidget      *m_selListView = nullptr;
    QString           m_projectDir;
    int               m_maxCount = 0;
};

#endif // IMAGEFILEDIALOG_H
