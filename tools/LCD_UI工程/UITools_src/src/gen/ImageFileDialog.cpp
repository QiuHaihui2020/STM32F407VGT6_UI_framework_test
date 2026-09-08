#include "BaseDialog.h"
#include "ImageFileDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

/// 原厂工程里图片路径一律是相对工程目录的正斜杠形式
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

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files);
    m_fileModel->setNameFilters(imageFilters());
    m_fileModel->setNameFilterDisables(false);
    m_listView = new QListView(this);
    m_listView->setModel(m_fileModel);
    m_listView->setViewMode(QListView::IconMode);
    m_listView->setIconSize(QSize(48, 48));
    m_listView->setGridSize(QSize(96, 84));
    m_listView->setResizeMode(QListView::Adjust);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_selListView = new QListWidget(this);
    m_selListView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *btnAdd = new QPushButton(tr("添加 →"), this);
    auto *btnDel = new QPushButton(tr("← 删除"), this);
    auto *btnUp = new QPushButton(QIcon(QStringLiteral(":/icon/icons/go-up.png")),
                                  QStringLiteral("上移一行"), this);
    auto *btnDown = new QPushButton(QIcon(QStringLiteral(":/icon/icons/go-down.png")),
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
    m_fileModel->setRootPath(root);
    m_listView->setRootIndex(m_fileModel->index(root));
}

void ImageFileDialog::setSelected(const QStringList &rel)
{
    m_selListView->clear();
    for (const QString &r : rel) {
        if (!r.isEmpty()) {
            m_selListView->addItem(r);
        }
    }
}

QStringList ImageFileDialog::selected() const
{
    QStringList out;
    for (int i = 0; i < m_selListView->count(); ++i) {
        out.append(m_selListView->item(i)->text());
    }
    return out;
}

void ImageFileDialog::onTreeViewClicked(QModelIndex index)
{
    const QString path = m_dirModel->filePath(index);
    m_fileModel->setRootPath(path);
    m_listView->setRootIndex(m_fileModel->index(path));
}

void ImageFileDialog::addPath(const QString &absPath)
{
    if (m_maxCount > 0 && m_selListView->count() >= m_maxCount) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 tr("最多只能放 %1 项").arg(m_maxCount));
        return;
    }
    const QString rel = toRel(m_projectDir, absPath);
    // 允许重复：原厂的数字图片列表就可能出现同一张图占多位
    m_selListView->addItem(rel);
}

void ImageFileDialog::onListViewDoubleClicked(QModelIndex index)
{
    addPath(m_fileModel->filePath(index));
}

void ImageFileDialog::onSelListViewDoubleClicked(QModelIndex index)
{
    delete m_selListView->takeItem(index.row());
}

void ImageFileDialog::onAddSelectedItems()
{
    const QModelIndexList sel = m_listView->selectionModel()->selectedIndexes();
    for (const QModelIndex &i : sel) {
        addPath(m_fileModel->filePath(i));
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
