/*
 * Property.h —— 属性面板一族
 *
 * 【面板结构】第二列下半部分的属性区是这么排的：
 *     ID号   [BACKLIGHT_VALUE_LIST]
 *     ┌ CSS属性_0 ┐                       <- PropertyDock (QTabWidget)
 *     │ 对齐方式  [ALIGN_CENTER   ▼]
 *     │ 默认隐藏  [false          ▼]
 *     │ 标志      [ELM_FLAG_NORMAL▼]
 *     │ 位置坐标  ┌ X / Y / 宽度 / 高度 ┐   <- Position (QGroupBox)
 *     │ [背景颜色][🖌]                      <- BackgroundPane
 *     │ [背景图片][🖌]                      <- FileEdit
 *     │ 内边框线  ┌ 左/上/右/下 + [边框] ┐   <- Border (QGroupBox)
 *     └
 *     滚动方式   [SCROLL         ▼]        <- 控件专有属性，来自 control.json
 *     默认高亮行号 [0]
 * 对应的类：BaseProperty / BasicPropertyPane / CssPropertyPane /
 * Position / Border / BackgroundPane / FileEdit（signal filePathChanged）/
 * HandleButton / BaseScrollArea。
 */
#ifndef PROPERTY_H
#define PROPERTY_H

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QPushButton>
#include <QString>
#include <QVector>
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

/** 统一外观的滚动区。第二列上下两块、右侧页面栏都是它。 */
class BaseScrollArea : public QScrollArea
{
    Q_OBJECT
public:
    explicit BaseScrollArea(QWidget *parent = nullptr);
    ~BaseScrollArea() override;
};

/** 控件列表里那些"拖到画布上"的按钮。 */
class HandleButton : public QPushButton
{
    Q_OBJECT
public:
    explicit HandleButton(QWidget *parent = nullptr);
    ~HandleButton() override;

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
    QString      m_caption;      ///< 没选图时按钮上显示的字
    /** 按当前 m_path 刷新按钮的样子（缩略图 / 提示文字）。 */
    void refreshFace();
};

/** "背景颜色" 行。 */
class BackgroundPane : public QWidget
{
    Q_OBJECT
public:
    explicit BackgroundPane(QWidget *parent = nullptr);
    ~BackgroundPane() override;

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

    /**
     * @brief 给四个框定取值范围
     *
     * 规则：
     *
     * @code
     *   宽.setMaximum(父宽);  高.setMaximum(父高);        // 无条件，先做
     *   if (自身类型 == NewLayout(3) || 自身类型 == NewLayer(4)) {
     *       X.setRange(-999, 999);  Y.setRange(-999, 999);
     *   } else {
     *       X.setMaximum(父宽 - 自身宽);  X.setMinimum(0);
     *       Y.setMaximum(父高 - 自身高);  Y.setMinimum(0);
     *   }
     * @endcode
     *
     * 宽/高的**最小值不设**，QSpinBox 默认就是 0，所以宽高可以填 0。
     * 详见 docs/UI_BEHAVIOR.md 第 10 节。
     *
     * @param parentSize 父容器的宽高；给空 QSize 表示父级未知，四个框回到宽松默认
     * @param ownSize    自身当前的宽高（取节点里的值，不是框里的值）
     * @param container  自身是不是 NewLayout / NewLayer
     */
    void  setBounds(const QSize &parentSize, const QSize &ownSize, bool container);

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
    /**
     * 只改了"预览"，工程数据一个字节都没动 —— 画布要重画，但**不能**把工程
     * 标记成改过（那样标题会平白带上 *、退出还问要不要保存）。
     * 目前只有 text/ascii 那句"预览文字"走这条。
     */
    void previewOnlyChanged();

protected:
    UiNode      *m_node = nullptr;
    QVBoxLayout *m_box = nullptr;
};

/** CSS 属性页：对齐方式 / 默认隐藏 / 标志 / 位置坐标 / 背景 / 内边框线。 */
/**
 * 属性面板分成两页之后，每一行归哪一页。
 *
 * 【为什么要分】一个「文字」控件铺满是 22 行，窄边栏里必须滚动才看得全。
 * 拆成两页之后最高的「时间」也只有 13 行。
 *
 * 【怎么分】按"这一项是在配参数，还是在挂资源/调外观"：
 *   Basic    对齐方式 / 默认隐藏 / 标志(图层是 z轴坐标) / 坐标
 *            + 数据源、编码格式、格式、自动记时、滚动方式、默认高亮…、
 *              旋转中心点、颜色类型、文字颜色、高亮颜色、事件属性
 *   Resource 背景颜色 / 背景图片 / 边框
 *            + 各种图片列表、文字列表
 *
 * 边框跟着背景走：它和背景一样是**外观装饰**，而且它一组就占 5 行 ——
 * 留在第一页的话第一页还是 17 行，等于没拆。
 *
 * ★ 拆页签的取舍见 docs/UI_BEHAVIOR.md §17。
 */
enum PropSection {
    SecBasic = 0,       ///< 基础设置
    SecResource = 1,    ///< 资源
    SecCount = 2
};

class CssPropertyPane : public BaseProperty
{
    Q_OBJECT
public:
    explicit CssPropertyPane(QWidget *parent = nullptr);
    ~CssPropertyPane() override;

    /** 本页只铺哪一个分区的字段。 */
    void setSection(PropSection s) { m_section = s; }

    /** 本页对应 element_css.struct 的哪一个状态（就是 "CSS属性_N" 的 N）。 */
    void setState(int s) { m_state = s; }
    int  state() const { return m_state; }

    void showNode(UiNode *n) override;
    /** 自测用：CSS 属性页当前铺了哪几组（按标题/标签文字）。 */
    QStringList rowsForTest() const;

private:
    void clearRows();
    /** 把面板上的一次改动写回 element_css.struct[m_state]。 */
    void commitCss(const QString &propName, const QString &key, const QJsonValue &value);

    int          m_state = 0;
    PropSection  m_section = SecBasic;
    Position    *m_pos = nullptr;
    /** 铺面板时挡住信号，否则一选中控件就把它标脏。 */
    bool      m_loading = false;
};

/** 通用属性：ID号 + 由 control.json 的 property[] 动态生成的控件专有项。 */
class BasicPropertyPane : public BaseProperty
{
    Q_OBJECT
public:
    explicit BasicPropertyPane(QWidget *parent = nullptr);
    ~BasicPropertyPane() override;

    void showNode(UiNode *n) override;

    /**
     * 控件专有属性的容器，一个分区一个。
     *
     * 由外层（PropertyDock）摆进对应的页签里；BasicPropertyPane 本体只显示
     * 最上面那个常驻的 "ID号"。
     */
    QWidget *dynamicSection(PropSection s) const
    {
        return s == SecResource ? m_dynRes : m_dyn;
    }

    /** 自测：某一个分区铺了哪几行。 */
    QStringList dynRowsForTest(PropSection s) const;
    /** 自测/查找：专有属性区拆成了两半，这里给出两个搜索根。 */
    QVector<QWidget *> dynamicSections() const { return { m_dyn, m_dynRes }; }
    /** 自测：两半里所有的下拉框 / 标签（拆页签前是一次 findChildren 就够的）。 */
    QList<QComboBox *> dynCombosForTest() const;
    QList<QLabel *> dynLabelsForTest() const;

    /** 自测：当前的警告正文（空 = 没有警告）。 */
    QString warningTextForTest() const { return m_tipText; }
    /** 自测用：只把字填进 ID 输入框并让它拿到焦点，**不**触发提交 ——
     *  模拟"用户刚敲完还没点别处"。配合 EditorOps::commitPendingEdit() 用。 */
    void typeIdForTest(const QString &text);
    /** 自测用：ID 输入框里当前显示的字。 */
    QString idTextForTest() const;
    /** 自测用：控件专有属性区当前铺了哪几行（按标签文字）。 */
    QStringList dynRowsForTest() const;
    /**
     * 自测：把警告气泡真弹一次，返回"文字放得下吗"。
     *
     * 这条盯的是"提示显示不全" —— 以前警告是塞在属性栏一行里的，两百来像素
     * 宽根本放不下。气泡是顶层窗口、按文字算尺寸，不该再被裁。
     * @return 没有警告时返回 true（没什么要显示的，自然放得下）
     */
    bool warningTipFitsForTest();
    /** 自测：把警告气泡画成图（看尾巴/圆角/文字排版）。没有警告时返回空图。 */
    QPixmap warningTipPixmapForTest();

private:
    void clearDynamic();

    /// 属性写回：把改动塞进 UiProperty::raw，再置脏
    typedef std::function<void(const std::function<void(QJsonObject &)> &)> CommitFn;

    /* 【返回的是"按钮 + 条目下拉框"一整块，不只是按钮】面板上每个列表类
     * 属性下面都跟着一个下拉框，把列表里的条目列出来（图片显示"缩略图 +
     * 文件名"，文字显示"内容#ResID"，如"蓝牙#m1"）—— 只有一个按钮的话，
     * 面板上根本看不出这个控件配了什么、画布上那张图是列表里的哪一条。 */
    QWidget *makeListButton(const UiProperty &p, const QString &cap,
                            const CommitFn &commit);
    QWidget *makeTextListButton(const UiProperty &p, const QString &cap,
                                const CommitFn &commit);
    /**
     * 预览画的是列表里第几条 —— 下拉框要停在这一条上，面板和画布才对得上。
     *
     * 规则不是拍脑袋定的，是按"哪个参数真的指定了条目"来的（实测两个工程）：
     *   str（文字列表）      default 一定在 list 里（209/209），它就是那条
     *   normal_image（图片） default 几乎都不在 list 里（196/202，是模板残留），
     *                        真正指定条目的是"默认高亮"（highlight）
     *   其它 piclist/arrlist 没有任何参数指定，取第 0 条
     */
    int previewIndexOf(const UiProperty &p) const;
    /** 列表条目下拉框。isText=true 走"内容#ResID"，否则走"缩略图 + 文件名"。 */
    QComboBox *makeEntryCombo(const UiProperty &p, bool isText);
    QPushButton *makeActionButton(const UiProperty &p, const CommitFn &commit);

protected:
    /** 点面板上那个 ⚠ 时，把浮动气泡再叫出来。 */
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    /** 带尾巴的浮动警告气泡（进程内共用一个，见 Property.cpp 的 CalloutTip）。 */
    class CalloutTip *tip() const;
    /**
     * 在 mark 上挂 ⚠ 并让气泡指着 anchor 弹出来；msg 为空表示没问题、清掉。
     * 【为什么正文不放面板里】属性栏两百来像素宽，整句话塞进去显示不全。
     */
    void setWarning(QLabel *mark, QWidget *anchor, const QString &msg);

    QLineEdit   *m_id = nullptr;
    QWidget     *m_dyn = nullptr;        ///< 基础设置页那半
    QFormLayout *m_dynForm = nullptr;
    QWidget     *m_dynRes = nullptr;     ///< 资源页那半
    QFormLayout *m_dynFormRes = nullptr;
    /* 气泡自动收了之后还要能再叫出来，所以把内容和锚点留着 */
    QString      m_tipText;
    QWidget     *m_tipAnchor = nullptr;
};

#endif // PROPERTY_H
