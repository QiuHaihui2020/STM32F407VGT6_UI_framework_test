/* ImageListView.h —— 类名/基类/信号/槽签名来自 ui-tools.exe 的 moc 元数据。
 *
 * 【它是"背景图片"那一行点开的弹窗】原厂有两个长得很像的图片对话框，
 * 靠 exe 里的字符串能分开（0xc98819 起那一段）：
 *
 *   ImageFileDialog  "图片编辑"
 *                    + onAddSelectedItems/onDelSelectedItems + 上移/下移图标
 *                    + "已经添加的图片数:"          -> 图片列表用，**多选**
 *
 *   ImageListView    "图片编辑(双击选中图片并更新到控件)"
 *                    + "双击选中图片并更新到控件显示."
 *                    + "目录"                        -> 背景图片用，**双击单选**
 *
 * 版式和图片列表那个一样：左边目录树、右边缩略图；区别是这个只选一张，
 * 双击即选中并关闭。
 *
 * 【路径一律相对工程目录】工程 json 里存的就是 "config/pic_lcd/v_block.bmp"
 * 这种形式，绝对路径会让下游（预览、ResBuilder）全部找不到图。
 */
#ifndef IMAGELISTVIEW_H
#define IMAGELISTVIEW_H

#include <QDialog>
#include <QModelIndex>
#include <QString>

class QFileSystemModel;
class QTreeView;
class QListWidget;
class QListWidgetItem;

class ImageListView : public QDialog
{
    Q_OBJECT

public:
    explicit ImageListView(QWidget *parent = nullptr);
    ~ImageListView() override;

    /** 工程目录。树根落在 <工程>/config（没有就退回工程目录本身）。 */
    void setProjectDir(const QString &dir);
    /** 只浏览、不参与选择时用（--dialog-smoke 走这条）。 */
    void setRootDir(const QString &dir);

    /** 预置当前这张图（相对工程目录），顺带把中间那栏定位到它所在目录。 */
    void setSelected(const QString &rel);
    /** 选中的那张图，**相对工程目录**；没选返回空。 */
    QString selected() const { return m_selected; }

    /** 自测用：把当前目录里第 n 张图当成"被双击"。 */
    bool pickForTest(int n);
    /** 自测用：当前中间栏里有几张图。 */
    int  imageCountForTest() const;

signals:
    void loadImageDone();

private slots:
    void onTreeViewClicked(QModelIndex a0);

private:
    void showDir(const QString &absDir);
    void takeItem(QListWidgetItem *it);

    QFileSystemModel *m_dirModel = nullptr;
    QTreeView        *m_tree = nullptr;
    QListWidget      *m_list = nullptr;
    QString           m_projectDir;
    QString           m_selected;
};

#endif // IMAGELISTVIEW_H
