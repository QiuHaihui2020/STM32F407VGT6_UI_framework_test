#include "ProjectDialog.h"

#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>

ProjectDialog::ProjectDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ProjectDialog"));
    setWindowTitle(QStringLiteral("新建工程"));

    auto *verticalLayout = new QVBoxLayout(this);
    verticalLayout->setObjectName(QStringLiteral("verticalLayout"));

    /* 工程名 */
    auto *nameRow = new QHBoxLayout;
    nameRow->setObjectName(QStringLiteral("horizontalLayout"));
    auto *label_3 = new QLabel(QStringLiteral("工程名称:"), this);
    label_3->setObjectName(QStringLiteral("label_3"));
    m_prjname = new QLineEdit(this);
    m_prjname->setObjectName(QStringLiteral("prjname"));
    m_prjname->setText(QStringLiteral("untitled"));
    nameRow->addWidget(label_3);
    nameRow->addWidget(m_prjname);
    verticalLayout->addLayout(nameRow);

    /* 屏幕尺寸 */
    auto *groupBox = new QGroupBox(QStringLiteral("页面尺寸"), this);
    groupBox->setObjectName(QStringLiteral("groupBox"));
    auto *gridLayout = new QGridLayout(groupBox);
    gridLayout->setObjectName(QStringLiteral("gridLayout"));
    auto *label = new QLabel(QStringLiteral("宽度:"), groupBox);
    label->setObjectName(QStringLiteral("label"));
    m_spinBox = new QSpinBox(groupBox);
    m_spinBox->setObjectName(QStringLiteral("spinBox"));
    m_spinBox->setRange(1, 9999);
    m_spinBox->setValue(128);
    auto *label_2 = new QLabel(QStringLiteral("高度:"), groupBox);
    label_2->setObjectName(QStringLiteral("label_2"));
    m_spinBox2 = new QSpinBox(groupBox);
    m_spinBox2->setObjectName(QStringLiteral("spinBox_2"));
    m_spinBox2->setRange(1, 9999);
    m_spinBox2->setValue(64);
    gridLayout->addWidget(label,      0, 0);
    gridLayout->addWidget(m_spinBox,  0, 1);
    gridLayout->addWidget(label_2,    1, 0);
    gridLayout->addWidget(m_spinBox2, 1, 1);
    verticalLayout->addWidget(groupBox);

    /* 多国语言表 */
    auto *langRow = new QHBoxLayout;
    auto *pushButton = new QPushButton(QIcon(QStringLiteral(":/icons/browse.png")),
                                       QStringLiteral("打开多国语言文件"), this);
    pushButton->setObjectName(QStringLiteral("pushButton"));   // ★ 名字决定自动连接
    /* 这一栏的默认内容是"行车记录仪.xls"（一个样例表名） */
    m_filestatus = new QLabel(QStringLiteral("行车记录仪.xls"), this);
    m_filestatus->setObjectName(QStringLiteral("filestatus"));
    langRow->addWidget(pushButton);
    langRow->addWidget(m_filestatus, 1);
    verticalLayout->addLayout(langRow);

    m_viewLang = new QListWidget(this);
    m_viewLang->setObjectName(QStringLiteral("view_lang"));
    verticalLayout->addWidget(m_viewLang, 1);

    m_buttonBox = new QDialogButtonBox(this);
    m_buttonBox->setObjectName(QStringLiteral("buttonBox"));
    /* 确定按钮叫"创建"，不是 OK */
    m_buttonBox->addButton(QStringLiteral("创建"), QDialogButtonBox::AcceptRole);
    m_buttonBox->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    verticalLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ProjectDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    /* 原程序靠 connectSlotsByName 把 pushButton 接到 on_pushButton_clicked()。
     * 这里控件是代码建的，同样调用一次即可生效 —— 名字已经对上了。 */
    QMetaObject::connectSlotsByName(this);

    resize(420, 360);
}

ProjectDialog::~ProjectDialog() = default;

QString ProjectDialog::projectName() const
{
    return m_prjname->text().trimmed();
}

QSize ProjectDialog::pageSize() const
{
    return QSize(m_spinBox->value(), m_spinBox2->value());
}

void ProjectDialog::onAccepted()
{
    if (projectName().isEmpty()) {
        m_prjname->setFocus();
        return;
    }
    accept();
}

void ProjectDialog::on_pushButton_clicked()
{
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择多国语言文件"), QString(),
        QStringLiteral("xls 文件 , CSV UTF-8 文件 (*.xls *.csv )"));
    if (f.isEmpty()) {
        return;
    }
    if (!QFileInfo::exists(f)) {
        QMessageBox::warning(this, QStringLiteral("警告"),
                             QStringLiteral("多国语言文件不存在"));
        return;
    }
    m_langXls = f;
    m_filestatus->setText(QFileInfo(f).fileName());

    /* 多国语言表默认是 UITools/多国语言_128_64.xls，
     * 第一行是语言列名（Chinese_Simplified / English / ...），
     * 与 result.h 里的 LANGUAGEID 一一对应。这里只列出文件名，
     * 真正的解析要等 .str 资源写入实现。 */
    m_viewLang->clear();
    m_viewLang->addItem(tr("已选择：%1").arg(f));
}
