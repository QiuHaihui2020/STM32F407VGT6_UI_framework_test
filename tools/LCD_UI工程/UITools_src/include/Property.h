/*
 * Property.h —— 属性面板一族
 *
 * 【为什么是这些类】把原厂 ui-tools.exe 跑起来对着截图看，第二列下半部分的
 * 属性区结构是：
 *     ID号   [BACKLIGHT_VALUE_LIST]
 *     ┌ CSS属性_0 ┐                       <- PropertyTab (QTabWidget)
 *     │ 对齐方式  [ALIGN_CENTER   ▼]
 *     │ 默认隐藏  [false          ▼]
 *     │ 标志      [ELM_FLAG_NORMAL▼]
 *     │ 位置坐标  ┌ X / Y / 宽度 / 高度 ┐   <- Position (QGroupBox)
 *     │ [背景颜色][🖌]                      <- Backgroud
 *     │ [背景图片][🖌]                      <- FileEdit
 *     │ 内边框线  ┌ 左/上/右/下 + [边框] ┐   <- Border (QGroupBox)
 *     └
 *     滚动方式   [SCROLL         ▼]        <- 控件专有属性，来自 control.json
 *     默认高亮行号 [0]
 * 这与逆向出的类名一一吻合：BaseProperty / ComProperty / CssProperty /
 * Position / Border / Backgroud / FileEdit（signal filePathChanged）/
 * DragButton / BaseScrollArea 全部各就各位。
 *
 * ★ 标记的是原始二进制里确实存在的成员，签名逐字一致。
 */
#ifndef PROPERTY_H
#define PROPERTY_H

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QPushButton>
#include <QString>
#include <QRect>
#include <QPoint>
#include <QColor>
#include <QStringList>
#include <QJsonObject>
#include <QJsonValue>
#include <functional>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;
class QFormLayout;
class QVBoxLayout;

class UiNode;
struct UiProperty;

/** 统一外观的滚动区。原厂第二列上下两块、右侧页面栏都是它。 */
class BaseScrollArea : public QScrollArea
{
    Q_OBJECT
public:
    explicit BaseScrollArea(QWidget *parent = nullptr);
    ~BaseScrollArea() override;
};

/** 控件列表里那些"拖到画布上"的按钮。 */
class DragButton : public QPushButton
{
    Q_OBJECT
public:
    explicit DragButton(QWidget *parent = nullptr);
    ~DragButton() override;

    void setPayload(const QString &cls, const QString &type)
    {
        m_cls = cls;
        m_type = type;
    }
    QString payloadClass() const { return m_cls; }
    QString payloadType() const { return m_type; }

protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QString m_cls, m_type;
    QPoint  m_press;
};

/** "背景图片" 那一行：一个按钮 + 一个小的选择按钮。 */
class FileEdit : public QWidget
{
    Q_OBJECT
public:
    explicit FileEdit(QWidget *parent = nullptr);
    ~FileEdit() override;

    void    setCaption(const QString &c);
    QString filePath() const { return m_path; }
    void    setFilePath(const QString &p);

signals:
    void filePathChanged(QString filePath);   ///< ★

private:
    QPushButton *m_main = nullptr;
    QPushButton *m_pick = nullptr;
    QPushButton *m_clear = nullptr;
    QString      m_path;
};

/** "背景颜色" 行。 */
class Backgroud : public QWidget
{
    Q_OBJECT
public:
    explicit Backgroud(QWidget *parent = nullptr);
    ~Backgroud() override;

    QColor color() const { return m_color; }
    void   setColor(const QColor &c);

signals:
    void colorChanged(const QColor &c);
    /** 点了右边那个箭头 = 不要背景色。手册 2.3："如果不需要背景颜色，
     *  可以点击其右边的箭头来删除背景颜色，背景颜色被删除后，箭头变为
     *  灰色不可选状态。" */
    void colorCleared();

private:
    QPushButton *m_main = nullptr;
    QPushButton *m_pick = nullptr;
    QPushButton *m_clear = nullptr;
    QColor       m_color;
};

/** "位置坐标" 组：X / Y / 宽度 / 高度。 */
class Position : public QGroupBox
{
    Q_OBJECT
public:
    explicit Position(QWidget *parent = nullptr);
    ~Position() override;

    void  setRect(const QRect &r);
    QRect rect() const;

signals:
    void rectEdited(const QRect &r);

private:
    QSpinBox *m_x = nullptr;
    QSpinBox *m_y = nullptr;
    QSpinBox *m_w = nullptr;
    QSpinBox *m_h = nullptr;
    bool      m_loading = false;
};

/** "内边框线" 组：左 / 上 / 右 / 下 + [边框]。 */
class Border : public QGroupBox
{
    Q_OBJECT
public:
    explicit Border(QWidget *parent = nullptr);
    ~Border() override;

    void setValues(int l, int t, int r, int b);
    /** 单色屏：把"边框+取色器"那一行换成"画/不画"两选一。 */
    void setMonoMode(bool on, bool visible);

signals:
    void valueEdited(int left, int top, int right, int bottom);
    /** 单色屏下"画/不画"被改。 */
    void borderVisibilityEdited(bool visible);

private:
    QSpinBox    *m_l = nullptr;
    QSpinBox    *m_t = nullptr;
    QSpinBox    *m_r = nullptr;
    QSpinBox    *m_b = nullptr;
    QPushButton *m_btn = nullptr;
    QPushButton *m_pick = nullptr;
    QComboBox   *m_mono = nullptr;    ///< 单色屏下取代取色器的"画/不画"
    bool         m_loading = false;
};

/** 属性面板里那几个"打开子对话框"的编辑器需要的上下文。
 *  选图片要知道工程目录（路径得存成相对的），选文字要知道多国语言表在哪，
 *  事件动作的"对象"下拉要全工程的 ename 清单。这些都不属于某个节点，
 *  放一份全局的最省事。MainWindow 打开工程时填。 */
struct PropertyContext {
    QString     projectDir;
    QString     excelPath;
    QStringList objectNames;

    static PropertyContext &instance();
};

/** 属性面板基类：统一持有当前节点 + 一个表单布局。 */
class BaseProperty : public QWidget
{
    Q_OBJECT
public:
    explicit BaseProperty(QWidget *parent = nullptr);
    ~BaseProperty() override;

    virtual void showNode(UiNode *n);
    UiNode *node() const { return m_node; }

signals:
    void nodeEdited(UiNode *n);

protected:
    UiNode      *m_node = nullptr;
    QVBoxLayout *m_box = nullptr;
};

/** CSS 属性页：对齐方式 / 默认隐藏 / 标志 / 位置坐标 / 背景 / 内边框线。 */
class CssProperty : public BaseProperty
{
    Q_OBJECT
public:
    explicit CssProperty(QWidget *parent = nullptr);
    ~CssProperty() override;

    /** 本页对应 element_css.struct 的哪一个状态（就是 "CSS属性_N" 的 N）。 */
    void setState(int s) { m_state = s; }
    int  state() const { return m_state; }

    void showNode(UiNode *n) override;

private:
    void clearRows();
    /** 把面板上的一次改动写回 element_css.struct[m_state]。 */
    void commitCss(const QString &propName, const QString &key, const QJsonValue &value);

    int       m_state = 0;
    Position *m_pos = nullptr;
    /** 铺面板时挡住信号，否则一选中控件就把它标脏。 */
    bool      m_loading = false;
};

/** 通用属性：ID号 + 由 control.json 的 property[] 动态生成的控件专有项。 */
class ComProperty : public BaseProperty
{
    Q_OBJECT
public:
    explicit ComProperty(QWidget *parent = nullptr);
    ~ComProperty() override;

    void showNode(UiNode *n) override;

    /** 控件专有属性（滚动方式 / 默认高亮行号 …）的容器。
     *  原厂把它排在 CSS属性 页签**下面**，所以这块由外层 dock 自己摆放，
     *  ComProperty 本体只显示最上面的 "ID号"。 */
    QWidget *dynamicSection() const { return m_dyn; }

private:
    void clearDynamic();

    /// 属性写回：把改动塞进 UiProperty::raw，再置脏
    typedef std::function<void(const std::function<void(QJsonObject &)> &)> CommitFn;

    QPushButton *makeListButton(const UiProperty &p, const QString &cap,
                                const CommitFn &commit);
    QPushButton *makeTextListButton(const UiProperty &p, const QString &cap,
                                    const CommitFn &commit);
    QPushButton *makeActionButton(const UiProperty &p, const CommitFn &commit);

    QLineEdit   *m_id = nullptr;
    QWidget     *m_dyn = nullptr;
    QFormLayout *m_dynForm = nullptr;
};

#endif // PROPERTY_H
