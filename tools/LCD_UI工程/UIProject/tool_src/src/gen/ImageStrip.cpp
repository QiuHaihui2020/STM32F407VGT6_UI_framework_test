#include "BaseDialog.h"
#include "ImageStrip.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

const QStringList &imageFilters()
{
    static const QStringList f{ QStringLiteral("*.bmp"), QStringLiteral("*.BMP"),
                                QStringLiteral("*.png"), QStringLiteral("*.PNG"),
                                QStringLiteral("*.jpg"), QStringLiteral("*.JPG"),
                                QStringLiteral("*.jpeg") };
    return f;
}

const QSize kThumbMax(128, 40);

/** 缩略图，带缓存 —— 一个目录里可能两百多张，每次重画都去解 BMP 会卡。
 *
 *  【缓存存 QImage 不存 QIcon/QPixmap】函数局部 static 是在 main 返回**之后**
 *  才析构的，那时 QGuiApplication 已经没了，销毁 QPixmap 会崩。 */
QIcon thumbOf(const QString &absPath)
{
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

} // namespace

ImageStrip::ImageStrip(QWidget *parent)
    : QDialog(parent)
{
    /* 标题 */
    setWindowTitle(QStringLiteral("选图片（双击换到控件上）"));
    resize(720, 460);

    /* ---- 左：目录树 ---- */
    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_tree = new QTreeView(this);
    m_tree->setModel(m_dirModel);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_dirModel->columnCount(); ++c) {
        m_tree->hideColumn(c);
    }

    /* ---- 右：缩略图 ----
     * 【自己填 QListWidget，不用 QFileSystemModel】图片列表那个对话框上踩过：
     * QFileSystemModel 给的是"文件类型图标"，要显示图片本身得套代理，
     * 而 QSortFilterProxyModel 叠在 QFileSystemModel 上会堆损坏（0xC0000374）。
     * 自己列目录、自己塞缩略图，简单也稳。 */
    m_list = new QListWidget(this);
    m_list->setIconSize(kThumbMax);
    m_list->setAlternatingRowColors(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);
    connect(box, &QDialogButtonBox::accepted, this, [this]() {
        if (QListWidgetItem *it = m_list->currentItem()) {
            takeItem(it);
        }
        accept();
    });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    /* 提示语 */
    auto *hint = new QLabel(QStringLiteral("双击一张图，就把它换到控件上。"), this);

    auto *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(new QLabel(QStringLiteral("目录"), this));   // 0xc98944
    left->addWidget(m_tree, 1);

    auto *row = new QHBoxLayout;
    row->addLayout(left, 2);
    row->addWidget(m_list, 5);

    auto *root = new QVBoxLayout(this);
    root->addLayout(row, 1);
    root->addWidget(hint);
    root->addWidget(box);

    connect(m_tree, &QTreeView::clicked, this, &ImageStrip::onTreeViewClicked);
    /* 双击即选中并关闭 —— 标题上写的就是这个行为 */
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        takeItem(it);
        accept();
    });
}

ImageStrip::~ImageStrip() = default;

void ImageStrip::takeItem(QListWidgetItem *it)
{
    if (!it) {
        return;
    }
    /* 条目上存的是绝对路径，对外一律给相对工程目录的正斜杠形式 ——
     * 工程 json 里就是 "config/pic_lcd/v_block.bmp" 这种，
     * 存绝对路径的话预览和 ResBuilder 都找不到图。 */
    const QString abs = it->data(Qt::UserRole).toString();
    m_selected = m_projectDir.isEmpty()
                 ? QDir::fromNativeSeparators(abs)
                 : QDir::fromNativeSeparators(QDir(m_projectDir).relativeFilePath(abs));
}

void ImageStrip::setProjectDir(const QString &dir)
{
    m_projectDir = dir;
    // 图片一般都在 <工程>/config 下；没有就退回工程目录本身
    QString root = QDir(dir).absoluteFilePath(QStringLiteral("config"));
    if (!QFileInfo::exists(root)) {
        root = dir;
    }
    setRootDir(root);
}

void ImageStrip::setRootDir(const QString &dir)
{
    m_dirModel->setRootPath(dir);
    m_tree->setRootIndex(m_dirModel->index(dir));
    showDir(dir);
}

void ImageStrip::setSelected(const QString &rel)
{
    m_selected = rel;
    if (rel.isEmpty()) {
        return;
    }
    /* 直接定位到当前这张图所在的目录 —— 要换的图绝大多数就在同一个目录里，
     * 不这么做每次进来还得自己从树里一层层点进去找。 */
    const QString abs = QFileInfo(rel).isAbsolute()
                        ? rel : QDir(m_projectDir).filePath(rel);
    const QString dir = QFileInfo(abs).absolutePath();
    if (!QFileInfo::exists(dir)) {
        return;
    }
    showDir(dir);
    const QModelIndex di = m_dirModel->index(dir);
    if (di.isValid()) {
        m_tree->setCurrentIndex(di);
        m_tree->scrollTo(di);
        m_tree->expand(di);
    }
    /* 中间栏里把它选中，一眼看得出现在用的是哪张 */
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == QDir::cleanPath(abs)) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void ImageStrip::showDir(const QString &absDir)
{
    m_list->clear();
    const QDir d(absDir);
    for (const QFileInfo &fi : d.entryInfoList(imageFilters(), QDir::Files, QDir::Name)) {
        auto *it = new QListWidgetItem(thumbOf(fi.absoluteFilePath()), fi.fileName());
        it->setData(Qt::UserRole, QDir::cleanPath(fi.absoluteFilePath()));
        it->setToolTip(fi.absoluteFilePath());
        m_list->addItem(it);
    }
    emit loadImageDone();
}

void ImageStrip::onTreeViewClicked(QModelIndex a0)
{
    if (a0.isValid()) {
        showDir(m_dirModel->filePath(a0));
    }
}

bool ImageStrip::pickForTest(int n)
{
    if (n < 0 || n >= m_list->count()) {
        return false;
    }
    m_list->setCurrentRow(n);
    takeItem(m_list->item(n));
    return !m_selected.isEmpty();
}

int ImageStrip::imageCountForTest() const
{
    return m_list->count();
}
