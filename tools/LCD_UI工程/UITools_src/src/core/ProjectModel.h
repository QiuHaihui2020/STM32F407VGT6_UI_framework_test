/*
 * ProjectModel.h —— ui-tools 工程文件(*.json)的内存模型
 *
 * 【定位】ui-tools.exe 是一个**纯布局编辑器**：它只读写 <工程>.json 和
 * autosave.json，不产 .sty、不产 ename.h、不调下游工具（在它的二进制里搜
 * 不到 uitoolbin / QtToolBin / ResBuilder / ename.h / project.bin 任何一个
 * 字符串，而这些全在 QtToolBin.exe 里）。所以"能替换原厂 exe"的门槛就是：
 * 工程文件读写要与它完全一致。
 *
 * 【格式】原厂 SmallColorTFT.json 的特征（re/json_fmt.py 实测）：
 *     - 就是 Qt 的 QJsonDocument::toJson(Indented)：4 空格缩进、
 *       对象键按字典序（QJsonObject 天然如此）、空数组写成 "[\n<缩进>]"
 *     - 纯 LF 换行、无 BOM、UTF-8 直出不转义、结尾 "\n}\n"
 *   所以用 Qt 写出来天生同格式，不需要自己拼字符串。
 *
 * 【保真往返】这是能不能顶替原厂工具的硬指标：打开→不改→保存必须与原文件
 * 逐字节相同。做法是每个节点都留着原始 QJsonObject(m_raw)，保存时以它为底，
 * 只覆盖被改过的字段；没建模的键、原本就存在的空数组、字段有无，一律照旧。
 *   验证：UITools --json-roundtrip <in.json> <out.json>
 *
 * 【层次】project -> pages[] -> layer[] -> layout[] -> {layout[] | widget[] | listwidget[]}
 *   "-class" 是编辑器里的 Qt 类，"-type" 是业务类型(Battery / Text / ImageList ...)
 *
 * 【注意】工程 json 里的坐标是**绝对像素**。下游 uitoolbin.bin 里会被换算成
 * 万分比（x 112/128 -> 8750/10000），那是 QTToolJson 的事，本模型不掺和。
 */
#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include <QString>
#include <QVector>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <functional>

/** 控件的一条属性。对应 json 里 property[] 的一项。 */
struct UiProperty {
    QString name;        ///< "-name"，如 id / rect / color
    QString type;        ///< "-type"，属性编辑器据此选控件
    QString caption;
    QString ename;       ///< 英文名 —— QtToolBin 生成 ename.h 时的宏名
    int     id = 0;
    QJsonObject raw;     ///< 原始 json；保存以它为底
    bool    dirty = false;
};

/** 工程树上的一个节点：页面 / 图层 / 布局 / 控件，结构相同只是层级不同。 */
class UiNode
{
public:
    ~UiNode();

    QString cls;         ///< "-class"
    QString type;        ///< "-type"
    QString name;        ///< "-name"
    QString caption;
    QString icon;
    QString tip;
    QString version;
    QRect   rect;        ///< 由 property 里 -name=="rect" 的项提取，画布直接用

    QVector<UiProperty> props;

    /** 直接读写未建模的键。写入会置脏，回写时原样带出去。 */
    QJsonValue extraValue(const QString &k) const { return m_raw.value(k); }
    void setExtra(const QString &k, const QJsonValue &v)
    {
        m_raw.insert(k, v);
        m_dirty = true;
    }

    /// 子节点。first 是它在 json 里所属的键("layer"/"layout"/"widget"/"listwidget")
    QVector<QPair<QString, UiNode *>> children;

    UiNode *parent = nullptr;

    UiProperty *findProp(const QString &propName);
    void forEach(const std::function<bool(UiNode *)> &f);

    /* ---- element_css ----------------------------------------------------
     * 工程 json 里控件**没有** rect 属性，几何和样式全在
     *     property[-name=="element_css"].struct[状态][ ... ]
     * 里。struct 是"每个 CSS 状态一项"的数组 —— 原厂属性面板上那个
     * "CSS属性_0" 的下标就是它的索引。状态内的属性有
     *     align / invisible / z_order / rect / background_color /
     *     background_image / border ...
     * 页节点是个例外：它的 property[0] 是个只有 "rect" 键、连 -name 都没有的
     * 裸对象，取矩形时要单独兜住。
     * -------------------------------------------------------------------- */
    int        cssStateCount() const;
    QJsonArray cssState(int state) const;
    /** 在 at 处插入一个状态（at == 状态数 就是追加）。 */
    void       cssInsertState(int at, const QJsonArray &v);
    /** 删掉一个状态。最后一个不让删 —— 没有状态的控件既没几何也没样式。 */
    bool       cssRemoveState(int state);
    /** 把一个状态清空（保留这一项，只是内容没了）。 */
    void       cssClearState(int state);
    QRect      rectOf(int state = 0) const;
    void       setRectOf(int state, const QRect &r);

    /**
     * 改 element_css.struct[state] 里某一条属性的某个键。
     *
     * 属性面板上除了"位置坐标"，其余每一行（对齐方式 / 默认隐藏 / 标志 /
     * 背景颜色 / 背景图片 / 内边框线 / 滚动方式 …）都是 struct 里的一项，
     * 值存在哪个键上由那一项的 -type 决定（rect 存 "rect"、背景色存
     * "background-color"、枚举和数值存 "default"…）。所以这里按
     * (属性名, 键名) 定位，不去猜类型。
     *
     * @param propName struct 里那一项的 "-name"
     * @param key      要改的键，通常是 "default" 或与 -type 同名的那个
     * @return 找到并改了返回 true
     */
    bool setCssField(int state, const QString &propName, const QString &key,
                     const QJsonValue &value);
    /** 读回来（面板初值用）。 */
    QJsonValue cssField(int state, const QString &propName, const QString &key) const;

    /**
     * 属性面板上那个"默认隐藏"。
     *
     * json 里是个**枚举**，值是字符串 "true"/"false"，不是 json bool
     * （`{"-name":"invisible","-type":"enum","default":"true",
     *    "enum":[{"true":1},{"false":0}]}`），别拿 toBool() 去读，
     * 那样永远得到 false。
     */
    bool isDefaultHidden(int state = 0) const;

    /** 标记本节点被编辑过；只有脏节点才会重建 json，其余原样回写。 */
    void markDirty();
    bool isDirty() const { return m_dirty; }

    /** 原始 json（含所有未建模的键）。保存时以它为底。 */
    const QJsonObject &raw() const { return m_raw; }

    friend class ProjectModel;

private:
    QJsonObject m_raw;
    bool m_dirty = false;
    /// 原文件里出现过的子节点键，按出现顺序；空数组也要记住，回写时不能漏
    QVector<QString> m_childKeysPresent;
};

/** 一个完整工程。 */
class ProjectModel
{
public:
    ProjectModel();
    ~ProjectModel();

    bool load(const QString &path, QString *err = nullptr);
    bool save(const QString &path, QString *err = nullptr) const;

    /** 序列化成与原厂完全一致的字节（Qt Indented + LF）。 */
    QByteArray toJsonBytes() const;

    /** 保真往返自检：读进来再写出去，与原文件逐字节比较。 */
    static bool verifyRoundTrip(const QString &path, QString *report);

    void clear();
    void createDefault(const QString &projName, const QSize &pageSize);

    QString  name() const { return m_name; }
    void     setName(const QString &n) { m_name = n; m_dirty = true; }
    int      activePage() const { return m_activePage; }
    void     setActivePage(int i) { m_activePage = i; }
    QString  langExcel() const { return m_langExcel; }
    void     setLangExcel(const QString &p) { m_langExcel = p; m_dirty = true; }
    /** 启用的语言位掩码，与 Resbuilder.xml 的 <language> 同一套编码
     *  （bit i 置位 = 启用第 i+1 号语言）。原厂默认 0x13 = 简中+繁中+英文。 */
    quint32  languageMask() const { return m_languageMask; }
    void     setLanguageMask(quint32 m) { m_languageMask = m; m_dirty = true; }
    QSize    pageSize() const;

    const QVector<UiNode *> &pages() const { return m_pages; }
    QVector<UiNode *> &pages() { return m_pages; }

    QString  filePath() const { return m_path; }
    bool     dirty() const { return m_dirty; }
    void     setDirty(bool d) { m_dirty = d; }

    /** 原厂还会写一份 autosave.json（这个名字在 ui-tools.exe 里能搜到）。 */
    bool saveAutosave(const QString &dir, QString *err = nullptr) const;

    /* ---- 节点编号与显示名 -------------------------------------------
     * 原厂给新建节点起名是 "<caption>_<序号>"，这个序号是**跨页全局递增、
     * 且不计页节点**的。证据在 SmallColorTFT.json 里：页0 有 49 个非页
     * 节点(图层_0 … 文字_48)，页1 的第一个节点就叫 图层_49。
     * 以前这里按页各数各的，第二页新建出来的东西会和第一页重名。 */

    /** 全工程的非页节点总数 = 下一个新节点该拿的序号。 */
    int  nextNodeSeq() const;
    /** n 在全工程里的序号（非页节点，前序遍历）。找不到返回 -1。 */
    int  nodeSeq(const UiNode *n) const;
    /** 对象树第一列显示的名字。 */
    QString displayName(const UiNode *n) const;

    /* ---- 节点级的搬运原语（复制/粘贴、保存成控件、删除都要用）----------
     * 一律走 json 中转，而不是逐字段拷贝：节点里有一大堆没建模的键
     * （m_raw），手抄必漏，转一圈 json 才能保证克隆出来的东西存盘后
     * 和原件等价。 */
    static QJsonObject toJsonObject(const UiNode *n);
    static UiNode     *fromJsonObject(const QJsonObject &o, UiNode *parent);
    /** 深克隆一棵子树，整棵标脏（新节点没有可信的 m_raw 底稿）。 */
    static UiNode     *cloneNode(const UiNode *src, UiNode *parent);
    /** 从父节点摘下并销毁。返回它原来在 children 里的下标，没找到返回 -1。 */
    static int         detachAndDelete(UiNode *n);

private:
    static UiNode *nodeFromJson(const QJsonObject &o, UiNode *parent);
    static QJsonObject nodeToJson(const UiNode *n);

    QString m_path;
    QString m_name = QStringLiteral("untitled");
    QString m_langExcel;
    quint32 m_languageMask = 0x13;
    int     m_activePage = 0;
    bool    m_dirty = false;
    QVector<UiNode *> m_pages;
    QJsonObject m_rootRaw;      ///< 顶层原始 json，保真回写用
};

#endif // PROJECTMODEL_H
