#include "BaseDialog.h"
#include "ConfigProject.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "XlsReader.h"

namespace {

/// 与 result.h 里的语言编号一一对应（1..22），位掩码的 bit i 对应第 i+1 号
const char *const kLangs[22] = {
    "Chinese_Simplified", "Chinese_Traditional", "Japanese", "Korean",
    "English", "French", "German", "Italian",
    "Dutch", "Portuguese", "Spanish", "Swedish",
    "Czech", "Danish", "Polish", "Russian",
    "Turkey", "Hebrew", "Thai", "Hungarian",
    "Romanian", "Arabic",
};

} // namespace

ConfigProject::ConfigProject(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("工程设置"));
    resize(460, 520);

    m_prjname = new QLineEdit(this);

    auto *openfile = new QPushButton(QIcon(QStringLiteral(":/icon/icons/fileopen.png")),
                                     QStringLiteral("打开多国语言文件"), this);
    m_filestatus = new QLabel(this);
    m_filestatus->setWordWrap(true);

    m_viewLang = new QListWidget(this);
    for (const char *k : kLangs) {
        auto *it = new QListWidgetItem(QString::fromLatin1(k), m_viewLang);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
    }

    auto *selAll = new QPushButton(QStringLiteral("全选"), this);
    auto *dselAll = new QPushButton(QStringLiteral("全不选"), this);
    auto *re = new QPushButton(QStringLiteral("反选"), this);
    auto *btns = new QHBoxLayout;
    btns->addWidget(selAll);
    btns->addWidget(dselAll);
    btns->addWidget(re);
    btns->addStretch();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("工程名称:"), m_prjname);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(openfile);
    root->addWidget(m_filestatus);
    root->addWidget(new QLabel(QStringLiteral("语言列表"), this));
    root->addWidget(m_viewLang, 1);
    root->addLayout(btns);
    root->addWidget(box);

    connect(openfile, &QPushButton::clicked, this, &ConfigProject::on_openfile_clicked);
    connect(selAll, &QPushButton::clicked, this, &ConfigProject::on_lang_selectall_clicked);
    connect(dselAll, &QPushButton::clicked, this, &ConfigProject::on_lang_dselectall_clicked);
    connect(re, &QPushButton::clicked, this, &ConfigProject::on_lang_re_clicked);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshFileStatus();
}

ConfigProject::~ConfigProject() = default;

void ConfigProject::setProjectName(const QString &n)
{
    m_prjname->setText(n);
}

QString ConfigProject::projectName() const
{
    return m_prjname->text().trimmed();
}

void ConfigProject::setExcelPath(const QString &p)
{
    m_excel = p;
    refreshFileStatus();
}

void ConfigProject::setLanguageMask(quint32 mask)
{
    for (int i = 0; i < m_viewLang->count(); ++i) {
        m_viewLang->item(i)->setCheckState((mask >> i) & 1u ? Qt::Checked : Qt::Unchecked);
    }
}

quint32 ConfigProject::languageMask() const
{
    quint32 m = 0;
    for (int i = 0; i < m_viewLang->count(); ++i) {
        if (m_viewLang->item(i)->checkState() == Qt::Checked) {
            m |= (1u << i);
        }
    }
    return m;
}

void ConfigProject::refreshFileStatus()
{
    if (m_excel.isEmpty()) {
        m_filestatus->setText(QStringLiteral("多国语言文件不存在"));
        return;
    }
    if (!QFileInfo::exists(m_excel)) {
        m_filestatus->setText(QStringLiteral("%1 文件未找到. 请查看[全局设置]里的路径目录是否正确，重新选择多国语言文件. ").arg(m_excel));
        return;
    }
    // 顺手读一下，告诉用户里面有多少条 —— 表选错了当场就能看出来
    res::XlsReader xls;
    if (!xls.load(m_excel)) {
        m_filestatus->setText(QStringLiteral("%1\nxls 打不开,请查看[全局设置]里的路径目录是否正确")
                          .arg(m_excel));
        return;
    }
    const int rows = xls.sheets().first().rows.size();
    m_filestatus->setText(tr("%1\n共 %2 条 ResID").arg(m_excel).arg(qMax(0, rows - 1)));
}

void ConfigProject::on_openfile_clicked()
{
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择多国语言文件"), m_excel,
        QStringLiteral("xls 文件 , CSV UTF-8 文件 (*.xls *.csv )"));
    if (!f.isEmpty()) {
        m_excel = f;
        refreshFileStatus();
    }
}

void ConfigProject::on_lang_selectall_clicked()
{
    for (int i = 0; i < m_viewLang->count(); ++i) {
        m_viewLang->item(i)->setCheckState(Qt::Checked);
    }
}

void ConfigProject::on_lang_dselectall_clicked()
{
    for (int i = 0; i < m_viewLang->count(); ++i) {
        m_viewLang->item(i)->setCheckState(Qt::Unchecked);
    }
}

void ConfigProject::on_lang_re_clicked()
{
    for (int i = 0; i < m_viewLang->count(); ++i) {
        QListWidgetItem *it = m_viewLang->item(i);
        it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
}
