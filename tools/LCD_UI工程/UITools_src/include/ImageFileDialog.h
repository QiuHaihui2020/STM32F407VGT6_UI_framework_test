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

    /** 自测：已选列表第 i 条的图标是不是**真的位图**（不是通用文件图标/空）。 */
    bool selectedIconIsRealForTest(int i) const;
    /** 自测：切到某个目录，返回中间那栏里有几条**目录**（应当恒为 0）。 */
    int dirRowsForTest(const QString &absDir);
    /** 自测：把某个路径喂给"添加"，返回已选列表条数的变化。 */
    int tryAddForTest(const QString &absPath);

private slots:
    void onTreeViewClicked(QModelIndex index);
    void onListViewDoubleClicked(QModelIndex index);   ///< 中间那栏双击 = 添加
    void onSelListViewDoubleClicked(QModelIndex index);
    void onDelSelectedItems();
    void onAddSelectedItems();
    void onUp();
    void onDown();

private:
    void addPath(const QString &absPath);
    /** 往已选列表里加一条（缩略图 + 相对路径）。at<0 = 追加到末尾。 */
    void addSelectedRow(const QString &rel, int at = -1);
    /** 中间那栏切到某个目录：把里面的图片列出来（不含子目录）。 */
    void showDir(const QString &path);
    void moveCurrent(int delta);

    QFileSystemModel *m_dirModel = nullptr;      ///< 只给左边的目录树用
    QTreeView        *m_treeView = nullptr;
    /**
     * 中间那栏：自己填的列表，一行一张"缩略图 + 文件名"。
     *
     * 【为什么不用 QFileSystemModel】它是**异步**填充的，而且即使过滤器里
     * 只写 QDir::Files，子目录照样会冒出来 —— 双击一个子目录就把它当图片
     * 加进列表了。想在它上面套 QSortFilterProxyModel 把目录滤掉，实测直接
     * 堆损坏崩溃（0xC0000374）：那个组合本来就以难伺候著称。
     * 这一栏的需求很简单（列出一个目录里的图片），自己填反而干净可控。
     */
    QListWidget      *m_listView = nullptr;
    QListWidget      *m_selListView = nullptr;
    QString           m_projectDir;
    int               m_maxCount = 0;
};

#endif // IMAGEFILEDIALOG_H
