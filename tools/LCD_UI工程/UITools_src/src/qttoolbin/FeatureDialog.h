/*
 * FeatureDialog.h —— 「功能设置」。
 *
 * 版式：
 *   上半：「配置语言」表格 —— 语言 / 语言 / 字体 / 字号 / 斜体 / 加粗 / 下划线，
 *         右侧 上移 / 下移 / 增加 / 删除，再下面显示"选中: 13"
 *   左下：图片、资源文件名、bmp_transparent_color、png_background_color、
 *         其他配置(旋转)、多国语言文件(Excel)
 *   右下：屏幕类型、大小端模式、颜色表类型、压缩方式(图片/字符)
 *   底部：对应 ResBuilder.exe 版本提示 + 确定
 *
 * 【表格里哪一行算"启用"】靠整行选中来表示，右边那个"选中: 13"就是这几行
 * 拼出来的语言掩码（0x13 = bit0|bit1|bit4 = 简体中文+繁体中文+英语）。
 */
#ifndef FEATUREDIALOG_H
#define FEATUREDIALOG_H

#include <QDialog>
#include <QFont>

#include "ResbuilderOptions.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QTableWidget;

namespace toolbin {

class FeatureDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FeatureDialog(const ResbuilderOptions &opt, QWidget *parent = nullptr);
    ~FeatureDialog() override;

    /** 点了「确定」之后的设置。 */
    ResbuilderOptions options() const;

    /* ---- 自检用（QFontDialog 是模态的，无人值守点不了）---- */
    /** 单元格是不是都不可直接编辑（只读，改字体走双击弹窗）。 */
    bool cellsReadOnlyForTest() const;
    /** 模拟"双击弹窗里选了这个字体然后按 OK"。 */
    void applyFontForTest(int row, const QFont &f);
    /** 表格行数。 */
    int rowCountForTest() const;
    /** 模拟"点增加、在弹窗里填了这两个名字然后按确定"。 */
    void addLangForTest(const QString &name, const QString &key);

private slots:
    /** 双击整行 -> 标准 Select Font 弹窗。 */
    void onEditFont(int row, int col);
    void onMoveUp();
    void onMoveDown();
    void onAddLang();
    void onDelLang();
    void onPickExcel();
    void refreshSelectedMask();

private:
    void fillTable(const ResbuilderOptions &opt);
    /** 表格第 row 行读回一条语言。 */
    LangRow rowAt(int row) const;
    void    setRow(int row, const LangRow &r);
    /* 一行的值挂在第 0 格的 UserRole 上（QTableWidgetItem 只收 QVariant，
     * 给 LangRow 注册 metatype 要一堆样板，序列化成字符串列表最省事）。 */
    static QStringList packRow(const LangRow &r);
    static LangRow     unpackRow(const QStringList &v);

    QTableWidget *m_tab = nullptr;
    QLabel       *m_sel = nullptr;         ///< "选中: 13"
    QLineEdit    *m_pic = nullptr;
    QLineEdit    *m_res = nullptr;
    QLineEdit    *m_bmpKey = nullptr;
    QLineEdit    *m_pngBg = nullptr;
    QCheckBox    *m_rotate = nullptr;
    QLineEdit    *m_excel = nullptr;
    QRadioButton *m_tft = nullptr;
    QRadioButton *m_lcd = nullptr;
    QRadioButton *m_little = nullptr;
    QRadioButton *m_big = nullptr;
    QRadioButton *m_rgb = nullptr;
    QRadioButton *m_yuv = nullptr;
    QComboBox    *m_imgZip = nullptr;
    QComboBox    *m_strZip = nullptr;

    ResbuilderOptions m_in;                ///< 进来时那份，取消时原样带回
};

} // namespace toolbin

#endif // FEATUREDIALOG_H
