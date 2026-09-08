/* ImageListView.h —— 类名/基类/信号/槽签名来自 ui-tools.exe 的 moc 元数据。
 *
 * 只看不选的图片浏览器：左边目录树，右边缩略图。加载完一批发 loadImageDone()。
 * 和 ImageFileDialog 的区别是它不返回选择结果。
 */
#ifndef IMAGELISTVIEW_H
#define IMAGELISTVIEW_H

#include <QDialog>
#include <QModelIndex>

class QFileSystemModel;
class QTreeView;
class QListView;

class ImageListView : public QDialog
{
    Q_OBJECT

public:
    explicit ImageListView(QWidget *parent = nullptr);
    ~ImageListView() override;

    void setRootDir(const QString &dir);

signals:
    void loadImageDone();

private slots:
    void onTreeViewClicked(QModelIndex a0);

private:
    QFileSystemModel *m_dirModel = nullptr;
    QFileSystemModel *m_fileModel = nullptr;
    QTreeView        *m_tree = nullptr;
    QListView        *m_list = nullptr;
};

#endif // IMAGELISTVIEW_H
