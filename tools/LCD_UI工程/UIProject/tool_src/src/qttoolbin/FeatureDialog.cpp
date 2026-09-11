#include "FeatureDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFontDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace toolbin {

namespace {

/* 表格列。头两列都叫「语言」：前一列是中文名，后一列是 xml 里的 LANG 属性。 */
enum Col { ColName, ColKey, ColFace, ColPoint, ColItalic, ColBold, ColUnderline, ColCount };

/** 表格里 true/false 显示成小写字面量（只读，不是编辑器）。 */
QString boolText(bool on)
{
    return on ? QStringLiteral("true") : QStringLiteral("false");
}

/** "0x00FFFFFF" / "00FFFFFF" -> 数值；解不出来返回 def。 */
quint32 hexOf(const QString &s, quint32 def)
{
    QString t = s.trimmed();
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        t = t.mid(2);
    }
    bool ok = false;
    const quint32 v = t.toUInt(&ok, 16);
    return ok ? v : def;
}

QString hex8(quint32 v)
{
    return QStringLiteral("0x%1").arg(
        QString::number(v, 16).toUpper().rightJustified(8, QLatin1Char('0')));
}

} // namespace

FeatureDialog::FeatureDialog(const ResbuilderOptions &opt, QWidget *parent)
    : QDialog(parent), m_in(opt)
{
    setWindowTitle(QStringLiteral("配置界面"));

    // ---------------- 配置语言 ----------------
    auto *langBox = new QGroupBox(QStringLiteral("配置语言"), this);
    m_tab = new QTableWidget(0, ColCount, langBox);
    m_tab->setHorizontalHeaderLabels(QStringList{
        QStringLiteral("语言"), QStringLiteral("语言"), QStringLiteral("字体"),
        QStringLiteral("字号"), QStringLiteral("斜体"), QStringLiteral("加粗"),
        QStringLiteral("下划线") });
    m_tab->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tab->setSelectionMode(QAbstractItemView::MultiSelection);   // 选中 = 启用
    /* 【单元格一律不可直接编辑】字体/字号/斜体/加粗/下划线
     * 五项都不在表格里改，**双击整行**弹一个标准的 Select Font
     * （Qt 的 QFontDialog，见 temp/select font.jpg）统一设。
     * 语言名和 LANG 是那 22 种语言的固定表，也不给改。 */
    m_tab->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tab->verticalHeader()->setDefaultSectionSize(22);
    m_tab->horizontalHeader()->setStretchLastSection(true);
    m_tab->setMinimumHeight(200);

    auto *up = new QPushButton(QStringLiteral("上移"), langBox);
    auto *down = new QPushButton(QStringLiteral("下移"), langBox);
    auto *add = new QPushButton(QStringLiteral("增加"), langBox);
    auto *del = new QPushButton(QStringLiteral("删除"), langBox);
    m_sel = new QLabel(langBox);
    m_sel->setToolTip(QStringLiteral(
        "选中哪几行 = 启用哪几种语言，拼出来就是 Resbuilder.xml 里的 <language>。\n"
        "默认 0x13 = 简体中文 + 繁体中文 + 英语。"));

    /* 【改顺序有风险，说清楚】表格里第 i 行就是 Resbuilder.xml 里的第 i 条
     * language_name，也就是 font{i}，也是 <language> 掩码的第 i 位；而
     * ResBuilder 是拿这个下标去取 xls 的第 1+i 列的。上移/下移/增加/删除
     * 会把这个下标关系整体挪动 —— xls 的列顺序没跟着改的话，出来的就是
     * 张冠李戴的译文。
     * 【这条是推出来的】依据是 Resbuilder.xml 的结构和
     * ResConfig::activeLanguages() 的取列方式，还没有实机验证过。 */
    const QString orderTip = QStringLiteral(
        "第 i 行 = Resbuilder.xml 里第 i 条语言 = font{i} = 掩码第 i 位，\n"
        "ResBuilder 按这个下标去取多国语言表的第 1+i 列。\n"
        "改动顺序/增删行会整体挪动这个对应关系 —— xls 的列没跟着改就会串语言。");
    up->setToolTip(orderTip);
    down->setToolTip(orderTip);
    add->setToolTip(orderTip);
    del->setToolTip(orderTip);

    auto *langBtns = new QVBoxLayout;
    langBtns->addWidget(up);
    langBtns->addWidget(down);
    langBtns->addWidget(add);
    langBtns->addWidget(del);
    langBtns->addSpacing(8);
    langBtns->addWidget(m_sel);
    langBtns->addStretch();

    auto *langLay = new QHBoxLayout(langBox);
    langLay->addWidget(m_tab, 1);
    langLay->addLayout(langBtns);

    // ---------------- 左下 ----------------
    m_pic = new QLineEdit(opt.picturePath, this);
    m_res = new QLineEdit(opt.res, this);
    m_bmpKey = new QLineEdit(hex8(opt.bmpTransparentColor), this);
    m_bmpKey->setToolTip(QStringLiteral(
        "位图里等于这个颜色的像素算「透明」（点阵屏上就是不点亮）。\n"
        "本工程是 0x00FFFFFF，也就是白色透明。"));
    m_pngBg = new QLineEdit(hex8(opt.pngBackgroundColor), this);
    m_pngBg->setToolTip(QStringLiteral("PNG 半透明像素先合到这个底色上再判透明。"));
    m_rotate = new QCheckBox(QStringLiteral("旋转"), this);
    m_rotate->setChecked(opt.rotateFlag);
    m_rotate->setToolTip(QStringLiteral(
        "这里保留这一项。实际的旋转角度在主界面那个「旋转」下拉框里，\n"
        "这个勾选框还管什么没有确认，产出不依赖它。"));
    m_excel = new QLineEdit(opt.excelPath, this);
    auto *pickExcel = new QPushButton(QStringLiteral("…"), this);
    pickExcel->setFixedWidth(28);

    auto *picBox = new QGroupBox(QStringLiteral("图片"), this);
    (new QVBoxLayout(picBox))->addWidget(m_pic);
    auto *resBox = new QGroupBox(QStringLiteral("资源文件名"), this);
    (new QVBoxLayout(resBox))->addWidget(m_res);
    auto *bmpBox = new QGroupBox(QStringLiteral("bmp_transparent_color"), this);
    (new QVBoxLayout(bmpBox))->addWidget(m_bmpKey);
    auto *pngBox = new QGroupBox(QStringLiteral("png_background_color"), this);
    (new QVBoxLayout(pngBox))->addWidget(m_pngBg);
    auto *othBox = new QGroupBox(QStringLiteral("其他配置"), this);
    (new QVBoxLayout(othBox))->addWidget(m_rotate);

    auto *left = new QVBoxLayout;
    left->addWidget(picBox);
    left->addWidget(resBox);
    left->addWidget(bmpBox);
    left->addWidget(pngBox);
    left->addWidget(othBox);
    left->addStretch();

    // ---------------- 右下 ----------------
    m_tft = new QRadioButton(QStringLiteral("彩屏(TFTPANEL)"), this);
    m_lcd = new QRadioButton(QStringLiteral("点阵(LCDPANEL)"), this);
    (opt.panelType == QLatin1String("TFTPANEL") ? m_tft : m_lcd)->setChecked(true);
    auto *panelBox = new QGroupBox(QStringLiteral("屏幕类型:"), this);
    auto *panelLay = new QHBoxLayout(panelBox);
    panelLay->addWidget(m_tft);
    panelLay->addWidget(m_lcd);

    m_little = new QRadioButton(QStringLiteral("小端(LITTLEENDIAN)"), this);
    m_big = new QRadioButton(QStringLiteral("大端(BIGENDIAN)"), this);
    (opt.endian == QLatin1String("BIGENDIAN") ? m_big : m_little)->setChecked(true);
    auto *endBox = new QGroupBox(QStringLiteral("大小端模式:"), this);
    auto *endLay = new QHBoxLayout(endBox);
    endLay->addWidget(m_little);
    endLay->addWidget(m_big);

    m_rgb = new QRadioButton(QStringLiteral("RGB"), this);
    m_yuv = new QRadioButton(QStringLiteral("YUV"), this);
    (opt.paletteType.compare(QLatin1String("yuv"), Qt::CaseInsensitive) == 0
         ? m_yuv : m_rgb)->setChecked(true);
    auto *palBox = new QGroupBox(QStringLiteral("颜色表类型:"), this);
    auto *palLay = new QHBoxLayout(palBox);
    palLay->addWidget(m_rgb);
    palLay->addWidget(m_yuv);

    const QStringList zips{ QStringLiteral("none"), QStringLiteral("rle"),
                            QStringLiteral("quicklz") };
    m_imgZip = new QComboBox(this);
    m_imgZip->addItems(zips);
    m_imgZip->setCurrentText(opt.imageCompress);
    m_strZip = new QComboBox(this);
    m_strZip->addItems(zips);
    m_strZip->setCurrentText(opt.stringCompress);
    /* rle / quicklz 这两种压缩还没实现，选了也压不出来，先摆在这儿并说明。 */
    const QString zipTip = QStringLiteral(
        "只实现了 none。rle / quicklz 还没做，\n"
        "选了也不会真压缩 —— 别指望产物变小。");
    m_imgZip->setToolTip(zipTip);
    m_strZip->setToolTip(zipTip);
    auto *zipBox = new QGroupBox(QStringLiteral("压缩方式:"), this);
    auto *zipLay = new QGridLayout(zipBox);
    zipLay->addWidget(new QLabel(QStringLiteral("图片压缩方式:"), zipBox), 0, 0);
    zipLay->addWidget(m_imgZip, 0, 1);
    zipLay->addWidget(new QLabel(QStringLiteral("字符压缩方式:"), zipBox), 1, 0);
    zipLay->addWidget(m_strZip, 1, 1);

    auto *right = new QVBoxLayout;
    right->addWidget(panelBox);
    right->addWidget(endBox);
    right->addWidget(palBox);
    right->addWidget(zipBox);
    right->addStretch();

    auto *cols = new QHBoxLayout;
    cols->addLayout(left, 1);
    cols->addLayout(right, 1);

    // ---------------- 多国语言文件 ----------------
    auto *xlsBox = new QGroupBox(QStringLiteral("多国语言文件(Excel):"), this);
    auto *xlsLay = new QHBoxLayout(xlsBox);
    xlsLay->addWidget(m_excel, 1);
    xlsLay->addWidget(pickExcel);

    // ---------------- 底部 ----------------
    auto *ver = new QLabel(QStringLiteral("对应ResBuilder.exe 版本 1.0.0.162以上"), this);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(ver);
    bottom->addStretch();
    bottom->addWidget(box);

    auto *root = new QVBoxLayout(this);
    root->addWidget(langBox);
    root->addLayout(cols);
    root->addWidget(xlsBox);
    root->addLayout(bottom);
    resize(720, 660);

    fillTable(opt);

    connect(up, &QPushButton::clicked, this, &FeatureDialog::onMoveUp);
    connect(down, &QPushButton::clicked, this, &FeatureDialog::onMoveDown);
    connect(add, &QPushButton::clicked, this, &FeatureDialog::onAddLang);
    connect(del, &QPushButton::clicked, this, &FeatureDialog::onDelLang);
    connect(pickExcel, &QPushButton::clicked, this, &FeatureDialog::onPickExcel);
    connect(m_tab, &QTableWidget::itemSelectionChanged,
            this, &FeatureDialog::refreshSelectedMask);
    connect(m_tab, &QTableWidget::cellDoubleClicked, this, &FeatureDialog::onEditFont);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

FeatureDialog::~FeatureDialog() = default;

void FeatureDialog::fillTable(const ResbuilderOptions &opt)
{
    m_tab->setRowCount(0);
    for (int i = 0; i < opt.langs.size(); ++i) {
        m_tab->insertRow(i);
        setRow(i, opt.langs.at(i));
        /* 掩码里置了位的行 = 选中，界面上显示成蓝底 */
        if (opt.languageMask & (1u << i)) {
            m_tab->selectRow(i);
        }
    }
    refreshSelectedMask();
}

void FeatureDialog::setRow(int row, const LangRow &r)
{
    /* 整行只读；真正的值挂在第 0 格的 UserRole 上，改字体走 onEditFont()。
     * 【为什么不把值从单元格文字反解】字号那一列显示的是磅值，而斜体/加粗
     * 显示的是 true/false —— 反解一遍既啰嗦又容易在四舍五入上丢精度。
     * 直接存结构体，显示只是显示。 */
    const QStringList cells{ r.name, r.key, r.face, QString::number(r.point),
                             boolText(r.italic), boolText(r.bold),
                             boolText(r.underline) };
    for (int c = 0; c < cells.size() && c < ColCount; ++c) {
        auto *it = new QTableWidgetItem(cells.at(c));
        it->setToolTip(QStringLiteral("双击这一行改字体（弹标准 Select Font）"));
        m_tab->setItem(row, c, it);
    }
    m_tab->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(packRow(r)));
}

LangRow FeatureDialog::rowAt(int row) const
{
    QTableWidgetItem *it = m_tab->item(row, 0);
    return it ? unpackRow(it->data(Qt::UserRole).toStringList()) : LangRow();
}

/* 【为什么用 QStringList 存】QTableWidgetItem 只能挂 QVariant，给 LangRow
 * 注册 metatype 要多一堆样板；这一行就 7 个字段，序列化成字符串列表最省事。 */
QStringList FeatureDialog::packRow(const LangRow &r)
{
    return QStringList{ r.name, r.key, r.face, QString::number(r.point),
                        r.italic ? QStringLiteral("1") : QStringLiteral("0"),
                        r.bold ? QStringLiteral("1") : QStringLiteral("0"),
                        r.underline ? QStringLiteral("1") : QStringLiteral("0"),
                        r.strikeOut ? QStringLiteral("1") : QStringLiteral("0") };
}

LangRow FeatureDialog::unpackRow(const QStringList &v)
{
    LangRow r;
    if (v.size() < 8) {
        return r;
    }
    r.name = v.at(0);
    r.key = v.at(1);
    r.face = v.at(2);
    r.point = qMax(1, v.at(3).toInt());
    r.italic = (v.at(4) == QLatin1String("1"));
    r.bold = (v.at(5) == QLatin1String("1"));
    r.underline = (v.at(6) == QLatin1String("1"));
    r.strikeOut = (v.at(7) == QLatin1String("1"));
    return r;
}

/* 双击整行 -> 标准 Select Font 弹窗。
 * 它一次性管 Font / Font style(常规·粗体·斜体) / Size / Effects(下划线·删除线)。 */
void FeatureDialog::onEditFont(int row, int)
{
    if (row < 0 || row >= m_tab->rowCount()) {
        return;
    }
    LangRow r = rowAt(row);
    QFont f(r.face, r.point);
    f.setBold(r.bold);
    f.setItalic(r.italic);
    f.setUnderline(r.underline);
    f.setStrikeOut(r.strikeOut);

    bool ok = false;
    const QFont picked = QFontDialog::getFont(&ok, f, this, QStringLiteral("Select Font"));
    if (!ok) {
        return;
    }
    applyFontForTest(row, picked);
}

void FeatureDialog::applyFontForTest(int row, const QFont &picked)
{
    if (row < 0 || row >= m_tab->rowCount()) {
        return;
    }
    LangRow r = rowAt(row);
    r.face = picked.family();
    r.point = qMax(1, picked.pointSize());
    r.bold = picked.bold();
    r.italic = picked.italic();
    r.underline = picked.underline();
    r.strikeOut = picked.strikeOut();

    /* 选中状态就是语言掩码，重建这一行别把它弄丢 */
    const bool sel = m_tab->selectionModel()->isRowSelected(row, QModelIndex());
    setRow(row, r);
    if (sel) {
        m_tab->selectRow(row);
    }
}

bool FeatureDialog::cellsReadOnlyForTest() const
{
    if (m_tab->editTriggers() != QAbstractItemView::NoEditTriggers) {
        return false;
    }
    /* 再确认一遍没有残留的内联编辑器 —— 以前字体/字号/斜体那几列塞的是
     * QComboBox，那样就等于"能直接改"了。 */
    for (int r = 0; r < m_tab->rowCount(); ++r) {
        for (int c = 0; c < m_tab->columnCount(); ++c) {
            if (m_tab->cellWidget(r, c)) {
                return false;
            }
        }
    }
    return true;
}

int FeatureDialog::rowCountForTest() const
{
    return m_tab->rowCount();
}

void FeatureDialog::refreshSelectedMask()
{
    quint32 mask = 0;
    for (int i = 0; i < m_tab->rowCount() && i < 32; ++i) {
        if (m_tab->selectionModel()->isRowSelected(i, QModelIndex())) {
            mask |= (1u << i);
        }
    }
    m_sel->setText(QStringLiteral("选中: %1").arg(mask, 0, 16));
}

/* 上移/下移要把整行（含单元格里的下拉框）搬走。QTableWidget 不支持整行连
 * cellWidget 一起搬，只能取值再重建两行 —— 和 ActionList 里那两个按钮一样。 */
void FeatureDialog::onMoveUp()
{
    const int r = m_tab->currentRow();
    if (r <= 0) {
        return;
    }
    const LangRow a = rowAt(r - 1);
    const LangRow b = rowAt(r);
    setRow(r - 1, b);
    setRow(r, a);
    m_tab->setCurrentCell(r - 1, ColName);
}

void FeatureDialog::onMoveDown()
{
    const int r = m_tab->currentRow();
    if (r < 0 || r + 1 >= m_tab->rowCount()) {
        return;
    }
    const LangRow a = rowAt(r);
    const LangRow b = rowAt(r + 1);
    setRow(r, b);
    setRow(r + 1, a);
    m_tab->setCurrentCell(r + 1, ColName);
}

/* 「增加」要先问名字：弹一个小窗填「语言名称」和「语言英文名」。
 * 英文名就是 Resbuilder.xml 里 language_name 的 LANG 属性，ResBuilder 拿它
 * 生成 result.h 的宏，所以只能是 ASCII 标识符，这里顺手挡一道。 */
void FeatureDialog::onAddLang()
{
    QDialog d(this);
    d.setWindowTitle(QStringLiteral("增加语言"));
    auto *name = new QLineEdit(&d);
    auto *key = new QLineEdit(&d);
    key->setToolTip(QStringLiteral(
        "写进 Resbuilder.xml 的 LANG 属性，ResBuilder 用它生成 result.h 里的宏，\n"
        "只能用字母/数字/下划线，例如 Vietnamese。"));
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    box->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("语言名称:"), name);
    form->addRow(QStringLiteral("语言英文名:"), key);
    auto *lay = new QVBoxLayout(&d);
    lay->addLayout(form);
    lay->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    addLangForTest(name->text().trimmed(), key->text().trimmed());
}

/** 加一行；名字空着就给个占位。抽出来是为了自检能不弹窗地走同一条路。 */
void FeatureDialog::addLangForTest(const QString &name, const QString &key)
{
    const int r = m_tab->rowCount();
    LangRow n;
    n.name = name.isEmpty() ? QStringLiteral("新语言") : name;
    /* LANG 只留 ASCII 标识符字符 —— 带中文或空格的话 result.h 里就是个
     * 编译不过的宏名。空了就按行号编一个。 */
    QString k;
    for (const QChar c : key) {
        if ((c.unicode() < 128 && c.isLetterOrNumber()) || c == QLatin1Char('_')) {
            k.append(c);
        }
    }
    n.key = k.isEmpty() ? QStringLiteral("Language%1").arg(r) : k;
    n.face = QString::fromUtf8("\xe5\xae\x8b\xe4\xbd\x93");     // 宋体
    m_tab->insertRow(r);
    setRow(r, n);
    m_tab->setCurrentCell(r, ColName);
    refreshSelectedMask();
}

void FeatureDialog::onDelLang()
{
    const int r = m_tab->currentRow();
    if (r >= 0) {
        m_tab->removeRow(r);
        refreshSelectedMask();
    }
}

void FeatureDialog::onPickExcel()
{
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("多国语言文件"), m_excel->text(),
        QStringLiteral("Excel 97-2003 (*.xls)"));
    if (!f.isEmpty()) {
        m_excel->setText(f);
    }
}

ResbuilderOptions FeatureDialog::options() const
{
    ResbuilderOptions o = m_in;
    o.langs.clear();
    for (int i = 0; i < m_tab->rowCount(); ++i) {
        o.langs.append(rowAt(i));
    }
    quint32 mask = 0;
    for (int i = 0; i < m_tab->rowCount() && i < 32; ++i) {
        if (m_tab->selectionModel()->isRowSelected(i, QModelIndex())) {
            mask |= (1u << i);
        }
    }
    o.languageMask = mask;
    o.picturePath = m_pic->text().trimmed();
    o.res = m_res->text().trimmed();
    o.bmpTransparentColor = hexOf(m_bmpKey->text(), m_in.bmpTransparentColor);
    o.pngBackgroundColor = hexOf(m_pngBg->text(), m_in.pngBackgroundColor);
    o.rotateFlag = m_rotate->isChecked();
    o.excelPath = m_excel->text().trimmed();
    o.panelType = m_tft->isChecked() ? QStringLiteral("TFTPANEL")
                                     : QStringLiteral("LCDPANEL");
    o.endian = m_big->isChecked() ? QStringLiteral("BIGENDIAN")
                                  : QStringLiteral("LITTLEENDIAN");
    o.paletteType = m_yuv->isChecked() ? QStringLiteral("yuv") : QStringLiteral("rgb");
    o.imageCompress = m_imgZip->currentText();
    o.stringCompress = m_strZip->currentText();
    return o;
}

} // namespace toolbin
