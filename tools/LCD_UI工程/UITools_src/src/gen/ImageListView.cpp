#include "ImageListView.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QListView>
#include <QTreeView>
#include <QVBoxLayout>

ImageListView::ImageListView(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("图片编辑(双击选中图片并更新到控件)"));
    resize(720, 460);

    m_dirModel = new QFileSystemModel(this);
    m_dirModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_tree = new QTreeView(this);
    m_tree->setModel(m_dirModel);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_dirModel->columnCount(); ++c) {
        m_tree->hideColumn(c);
    }

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setFilter(QDir::Files);
    m_fileModel->setNameFilters(QStringList{ QStringLiteral("*.bmp"), QStringLiteral("*.BMP"),
                                             QStringLiteral("*.png"), QStringLiteral("*.PNG"),
                                             QStringLiteral("*.jpg"), QStringLiteral("*.JPG") });
    m_fileModel->setNameFilterDisables(false);
    m_list = new QListView(this);
    m_list->setModel(m_fileModel);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(64, 64));
    m_list->setGridSize(QSize(112, 100));
    m_list->setResizeMode(QListView::Adjust);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *row = new QHBoxLayout;
    row->addWidget(m_tree, 2);
    row->addWidget(m_list, 5);
    auto *root = new QVBoxLayout(this);
    root->addLayout(row, 1);
    root->addWidget(box);

    connect(m_tree, &QTreeView::clicked, this, &ImageListView::onTreeViewClicked);
    // QFileSystemModel 是异步填充的，一批目录读完才发 directoryLoaded
    connect(m_fileModel, &QFileSystemModel::directoryLoaded,
            this, [this](const QString &) { emit loadImageDone(); });
}

ImageListView::~ImageListView() = default;

void ImageListView::setRootDir(const QString &dir)
{
    m_dirModel->setRootPath(dir);
    m_tree->setRootIndex(m_dirModel->index(dir));
    m_fileModel->setRootPath(dir);
    m_list->setRootIndex(m_fileModel->index(dir));
}

void ImageListView::onTreeViewClicked(QModelIndex a0)
{
    const QString path = m_dirModel->filePath(a0);
    m_fileModel->setRootPath(path);
    m_list->setRootIndex(m_fileModel->index(path));
}
