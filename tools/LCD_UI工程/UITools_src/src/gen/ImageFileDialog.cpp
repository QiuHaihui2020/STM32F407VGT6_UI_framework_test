#include "BaseDialog.h"
#include "AppIcon.h"
#include "ImageFileDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

/// 工程里图片路径一律是相对工程目录的正斜杠形式
QString toRel(const QString &projectDir, const QString &abs)
{
    if (projectDir.isEmpty()) {
        return QDir::fromNativeSeparators(abs);
    }
    const QString rel = QDir(projectDir).relativeFilePath(abs);
    return QDir::fromNativeSeparators(rel);
}

const QStringList &imageFilters()
{
    static const QStringList f{ QStringLiteral("*.bmp"), QStringLiteral("*.BMP"),
                                QStringLiteral("*.png"), QStringLiteral("*.PNG"),
                                QStringLiteral("*.jpg"), QStringLiteral("*.JPG"),
                                QStringLiteral("*.jpeg") };
    return f;
}

/** 缩略图的外框；比这个大的按比例缩小，小的按**原尺寸**画。 */
const QSize kThumbMax(128, 40);

/** 取一张图的缩略图，带缓存 —— 一个目录里可能有两百多张，每次重画都去解
 *  BMP 会卡。 */
QIcon thumbOf(const QString &absPath)
{
    /* 【缓存存 QImage 不存 QIcon/QPixmap】函数局部 static 是在 main 返回
     * **之后**才析构的，那时 QGuiApplication 已经没了，销毁 QPixmap 会崩。
     * QImage 是纯数据，什么时候析构都安全。 */
    static QHash<QString, QImage> cache;
    QImage img;
    auto it = cache.constFind(absPath);
    if (it != cache.constEnd()) {
        img = it.value();
    } else {
        if (img.load(absPath)
            && (img.width() > kThumbMax.width() || img.height() > kThumbMax.height())) {
            img = img.scaled(kThumbMax, Qt::KeepAspectRatio, Qt::FastTransformation);
        }
        cache.insert(absPath, img);
    }
    return img.isNull() ? QIcon() : QIcon(QPixmap::fromImage(img));
}

/**
 * 让文件列表显示**图片本身**，而不是 Windows 的通用文件图标。
 *
 * 【原来这里是坏的】文件列表用的是 QFileSystemModel，它给的是"一个文件"
 * 的通用图标 —— 一屏两百多个一模一样的小方块，等于没有预览，得靠文件名
 * 猜哪张是哪张。这个对话框要把每张位图画出来。
 *
 * 【为什么不换成自己填 QListWidget】QFileSystemModel 顺带管了排序、过滤、
 * 换目录、增删同步，换掉就得自己再写一遍。挂个图标提供器最省事，也不动
 * 既有的选中/双击那套逻辑。
 */
class ThumbIconProvider : public QFileIconProvider
{
public:
    QIcon icon(const QFileInfo &info) const override
    {
        if (info.isFile()) {
            const QIcon t = thumbOf(info.absoluteFilePath());
            if (!t.isNull()) {
                return t;
            }
        }
        return QFileIconProvider::icon(info);
    }
    using QFileIconProvider::icon;      // 别把基类那几个重载藏了
};

} // namespace

ImageFileDialog::ImageFileDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("图片编辑"));
    resize(880, 520);

    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_dirModel);
    for (int c = 1; c < m_dirModel->columnCount(); ++c) {
        m_treeView->hideColumn(c);
    }
    m_treeView->setHeaderHidden(true);

    /* 中间那栏：自己填，一行一张"缩略图 + 文件名"（
     * 见 temp/图片列表.jpg）。不用 QFileSystemModel 的原因见头文件。 */
    m_listView = new QListWidget(this);
    m_listView->setIconSize(kThumbMax);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    /* 【隔行底色】图片都是黑白点阵，一张挨一张贴着，不分行根本看不出
     * 哪张是哪张的。隔行深浅。 */
    m_listView->setAlternatingRowColors(true);

    m_selListView = new QListWidget(this);
    m_selListView->setIconSize(kThumbMax);
    m_selListView->setAlternatingRowColors(true);
    m_selListView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *btnAdd = new QPushButton(tr("添加 →"), this);
    auto *btnDel = new QPushButton(tr("← 删除"), this);
    auto *btnUp = new QPushButton(AppIcon::get(QStringLiteral("move-up.png")),
                                  QStringLiteral("上移一行"), this);
    auto *btnDown = new QPushButton(AppIcon::get(QStringLiteral("move-down.png")),
                                    QStringLiteral("下移一行"), this);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);

    auto *mid = new QVBoxLayout;
    mid->addStretch();
    mid->addWidget(btnAdd);
    mid->addWidget(btnDel);
    mid->addSpacing(16);
    mid->addWidget(btnUp);
    mid->addWidget(btnDown);
    mid->addStretch();

    auto *rightCol = new QVBoxLayout;
    rightCol->addWidget(new QLabel(QStringLiteral("已经添加的图片数:"), this));
    rightCol->addWidget(m_selListView);

    auto *midCol = new QVBoxLayout;
    midCol->addWidget(new QLabel(QStringLiteral("双击选中图片并更新到控件显示."), this));
    midCol->addWidget(m_listView);

    auto *leftCol = new QVBoxLayout;
    leftCol->addWidget(new QLabel(QStringLiteral("目录"), this));
    leftCol->addWidget(m_treeView);

    auto *row = new QHBoxLayout;
    row->addLayout(leftCol, 2);
    row->addLayout(midCol, 4);
    row->addLayout(mid, 0);
    row->addLayout(rightCol, 3);

    auto *root = new QVBoxLayout(this);
    root->addLayout(row, 1);
    root->addWidget(box);

    connect(m_treeView, &QTreeView::clicked, this, &ImageFileDialog::onTreeViewClicked);
    connect(m_listView, &QListView::doubleClicked,
            this, &ImageFileDialog::onListViewDoubleClicked);
    connect(m_selListView, &QListWidget::doubleClicked,
            this, &ImageFileDialog::onSelListViewDoubleClicked);
    connect(btnAdd, &QPushButton::clicked, this, &ImageFileDialog::onAddSelectedItems);
    connect(btnDel, &QPushButton::clicked, this, &ImageFileDialog::onDelSelectedItems);
    connect(btnUp, &QPushButton::clicked, this, &ImageFileDialog::onUp);
    connect(btnDown, &QPushButton::clicked, this, &ImageFileDialog::onDown);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ImageFileDialog::~ImageFileDialog() = default;

void ImageFileDialog::setProjectDir(const QString &dir)
{
    m_projectDir = dir;
    // 图片一般都在 <工程>/config 下；没有就退回工程目录本身
    QString root = QDir(dir).absoluteFilePath(QStringLiteral("config"));
    if (!QFileInfo::exists(root)) {
        root = dir;
    }
    m_dirModel->setRootPath(root);
    m_treeView->setRootIndex(m_dirModel->index(root));
    showDir(root);
}

/** 已选列表里的一条：缩略图 + 相对路径。文本仍是纯相对路径，selected() 直接取。 */
void ImageFileDialog::addSelectedRow(const QString &rel, int at)
{
    const QString abs = QFileInfo(rel).isAbsolute()
                        ? rel
                        : QDir(m_projectDir).filePath(rel);
    auto *it = new QListWidgetItem(thumbOf(abs), rel);
    if (at < 0) {
        m_selListView->addItem(it);
    } else {
        m_selListView->insertItem(at, it);
    }
}

void ImageFileDialog::setSelected(const QStringList &rel)
{
    m_selListView->clear();
    for (const QString &r : rel) {
        if (!r.isEmpty()) {
            addSelectedRow(r);
        }
    }
    /* 【直接定位到当前图片所在的目录】打开时 pic_lcd 就该是选中态
     * （见 temp/图片列表.jpg）。不这么做的话，每次进来中间那栏都是空的，
     * 还得自己从树里一层层点进去找到图在哪 —— 而绝大多数时候要换的图
     * 就在同一个目录里。 */
    for (const QString &r : rel) {
        if (r.isEmpty()) {
            continue;
        }
        const QString abs = QFileInfo(r).isAbsolute()
                            ? r : QDir(m_projectDir).filePath(r);
        const QString dir = QFileInfo(abs).absolutePath();
        if (!QFileInfo::exists(dir)) {
            continue;
        }
        showDir(dir);
        const QModelIndex di = m_dirModel->index(dir);
        if (di.isValid()) {
            m_treeView->setCurrentIndex(di);
            m_treeView->scrollTo(di);
            m_treeView->expand(di);
        }
        break;
    }
}

int ImageFileDialog::dirRowsForTest(const QString &absDir)
{
    showDir(absDir);
    int dirs = 0;
    for (int r = 0; r < m_listView->count(); ++r) {
        if (QFileInfo(m_listView->item(r)->data(Qt::UserRole).toString()).isDir()) {
            ++dirs;
        }
    }
    return dirs;
}

int ImageFileDialog::tryAddForTest(const QString &absPath)
{
    const int before = m_selListView->count();
    addPath(absPath);
    return m_selListView->count() - before;
}

bool ImageFileDialog::selectedIconIsRealForTest(int i) const
{
    const QListWidgetItem *it = m_selListView->item(i);
    if (!it) {
        return false;
    }
    const QIcon ic = it->icon();
    if (ic.isNull()) {
        return false;
    }
    /* 通用文件图标是方的小尺寸；真位图这里是按原图尺寸给的。只要拿得到
     * 非空 pixmap 且尺寸和文件本身一致，就说明画的是图片内容。 */
    const QString abs = QDir(m_projectDir).filePath(it->text());
    const QPixmap file(abs);
    if (file.isNull()) {
        return false;
    }
    const QPixmap got = ic.pixmap(kThumbMax);
    return !got.isNull() && got.size() == file.size().boundedTo(kThumbMax);
}

QStringList ImageFileDialog::selected() const
{
    QStringList out;
    for (int i = 0; i < m_selListView->count(); ++i) {
        out.append(m_selListView->item(i)->text());
    }
    return out;
}

void ImageFileDialog::showDir(const QString &path)
{
    m_listView->clear();
    /* 【只列文件】QDir::Files 这里是真管用的 —— 子目录根本不会进来，
     * 也就不存在"把目录当图片加进列表"这回事。 */
    const QFileInfoList fs = QDir(path).entryInfoList(imageFilters(), QDir::Files,
                                                      QDir::Name);
    for (const QFileInfo &fi : fs) {
        auto *it = new QListWidgetItem(thumbOf(fi.absoluteFilePath()), fi.fileName());
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        m_listView->addItem(it);
    }
}

void ImageFileDialog::onTreeViewClicked(QModelIndex index)
{
    showDir(m_dirModel->filePath(index));
}

void ImageFileDialog::addPath(const QString &absPath)
{
    /* 【目录不是图片】列表那边已经用代理模型把目录滤掉了，这里再兜一道 ——
     * 目录混进列表存回工程就是一条指向目录的"图片路径"，下游 ResBuilder
     * 打开它必然失败，而且要到出资源那一步才报错，很难回溯到是这里点错的。 */
    const QFileInfo fi(absPath);
    if (!fi.isFile()) {
        return;
    }
    if (m_maxCount > 0 && m_selListView->count() >= m_maxCount) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 tr("最多只能放 %1 项").arg(m_maxCount));
        return;
    }
    const QString rel = toRel(m_projectDir, absPath);
    // 允许重复：数字图片列表就可能出现同一张图占多位
    addSelectedRow(rel);
}

void ImageFileDialog::onListViewDoubleClicked(QModelIndex index)
{
    if (QListWidgetItem *it = m_listView->item(index.row())) {
        addPath(it->data(Qt::UserRole).toString());
    }
}

void ImageFileDialog::onSelListViewDoubleClicked(QModelIndex index)
{
    delete m_selListView->takeItem(index.row());
}

void ImageFileDialog::onAddSelectedItems()
{
    for (QListWidgetItem *it : m_listView->selectedItems()) {
        addPath(it->data(Qt::UserRole).toString());
    }
}

void ImageFileDialog::onDelSelectedItems()
{
    QList<int> rows;
    for (const QModelIndex &i : m_selListView->selectionModel()->selectedIndexes()) {
        rows.append(i.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) {
        delete m_selListView->takeItem(r);
    }
}

void ImageFileDialog::moveCurrent(int delta)
{
    const int r = m_selListView->currentRow();
    const int to = r + delta;
    if (r < 0 || to < 0 || to >= m_selListView->count()) {
        return;
    }
    QListWidgetItem *it = m_selListView->takeItem(r);
    m_selListView->insertItem(to, it);
    m_selListView->setCurrentRow(to);
}

void ImageFileDialog::onUp()
{
    moveCurrent(-1);
}

void ImageFileDialog::onDown()
{
    moveCurrent(1);
}
