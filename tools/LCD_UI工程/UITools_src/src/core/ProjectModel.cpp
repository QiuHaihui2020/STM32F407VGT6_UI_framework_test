#include "ProjectModel.h"

#include <QSet>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

/* json 里承载子节点的四个键 */
static const char *const kChildKeys[] = { "layer", "layout", "widget", "listwidget" };

static bool isChildKey(const QString &k)
{
    for (const char *c : kChildKeys) {
        if (k == QLatin1String(c)) {
            return true;
        }
    }
    return false;
}

UiNode::~UiNode()
{
    for (auto &p : children) {
        delete p.second;
    }
    children.clear();
}

UiProperty *UiNode::findProp(const QString &propName)
{
    for (auto &p : props) {
        if (p.name == propName) {
            return &p;
        }
    }
    return nullptr;
}

void UiNode::forEach(const std::function<bool(UiNode *)> &f)
{
    if (!f(this)) {
        return;
    }
    for (auto &p : children) {
        p.second->forEach(f);
    }
}

void UiNode::markDirty()
{
    m_dirty = true;
}

int UiNode::cssStateCount() const
{
    for (const UiProperty &p : props) {
        if (p.name == QLatin1String("element_css")) {
            return p.raw.value(QStringLiteral("struct")).toArray().size();
        }
    }
    return 0;
}

QJsonArray UiNode::cssState(int state) const
{
    for (const UiProperty &p : props) {
        if (p.name == QLatin1String("element_css")) {
            const QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
            if (state >= 0 && state < st.size()) {
                return st.at(state).toArray();
            }
        }
    }
    return QJsonArray();
}

QRect UiNode::rectOf(int state) const
{
    const QJsonArray arr = cssState(state);
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("-name")).toString() == QLatin1String("rect")) {
            const QJsonObject r = o.value(QStringLiteral("rect")).toObject();
            return QRect(r.value(QStringLiteral("x")).toInt(),
                         r.value(QStringLiteral("y")).toInt(),
                         r.value(QStringLiteral("width")).toInt(),
                         r.value(QStringLiteral("height")).toInt());
        }
    }
    return QRect();
}

/* ---- CSS 状态的增删改 ---------------------------------------------------
 * 原厂属性面板上"清除 / 复制添加 / 复制插入 / 删除活动项"四个按钮动的就是
 * property[-name=="element_css"].struct 这个数组，一项 = 一个 CSS 状态，
 * 也就是页签上的 CSS属性_0 / _1 / …  改完必须把属性和节点一起标脏，否则
 * nodeToJson() 会直接吐原始 m_raw，改动全丢。 */
void UiNode::cssInsertState(int at, const QJsonArray &v)
{
    for (UiProperty &p : props) {
        if (p.name != QLatin1String("element_css")) {
            continue;
        }
        QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
        if (at < 0 || at > st.size()) {
            return;
        }
        st.insert(at, v);
        p.raw.insert(QStringLiteral("struct"), st);
        p.dirty = true;
        m_dirty = true;
        return;
    }
}

bool UiNode::cssRemoveState(int state)
{
    for (UiProperty &p : props) {
        if (p.name != QLatin1String("element_css")) {
            continue;
        }
        QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
        /* 留最后一个：状态数归零的控件在画布上没有 rect，在 .sty 里也没有
         * 可写的几何，等于把控件废了。 */
        if (state < 0 || state >= st.size() || st.size() <= 1) {
            return false;
        }
        st.removeAt(state);
        p.raw.insert(QStringLiteral("struct"), st);
        p.dirty = true;
        m_dirty = true;
        return true;
    }
    return false;
}

void UiNode::cssClearState(int state)
{
    for (UiProperty &p : props) {
        if (p.name != QLatin1String("element_css")) {
            continue;
        }
        QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
        if (state < 0 || state >= st.size()) {
            return;
        }
        st.replace(state, QJsonArray());
        p.raw.insert(QStringLiteral("struct"), st);
        p.dirty = true;
        m_dirty = true;
        return;
    }
}

bool UiNode::setCssField(int state, const QString &propName, const QString &key,
                         const QJsonValue &value)
{
    for (UiProperty &p : props) {
        if (p.name != QLatin1String("element_css")) {
            continue;
        }
        QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
        if (state < 0 || state >= st.size()) {
            return false;
        }
        QJsonArray one = st.at(state).toArray();
        for (int i = 0; i < one.size(); ++i) {
            QJsonObject o = one.at(i).toObject();
            if (o.value(QStringLiteral("-name")).toString() != propName) {
                continue;
            }
            if (o.value(key) == value) {
                return true;                 // 没变就别标脏，免得整棵树重写
            }
            o.insert(key, value);
            one.replace(i, o);
            st.replace(state, one);
            p.raw.insert(QStringLiteral("struct"), st);
            p.dirty = true;
            m_dirty = true;
            return true;
        }
        return false;
    }
    return false;
}

QJsonValue UiNode::cssField(int state, const QString &propName, const QString &key) const
{
    const QJsonArray one = cssState(state);
    for (const QJsonValue &v : one) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("-name")).toString() == propName) {
            return o.value(key);
        }
    }
    return QJsonValue();
}

bool UiNode::isDefaultHidden(int state) const
{
    return cssField(state, QStringLiteral("invisible"), QStringLiteral("default"))
           .toString() == QLatin1String("true");
}

void UiNode::setRectOf(int state, const QRect &r)
{
    for (UiProperty &p : props) {
        if (p.name != QLatin1String("element_css")) {
            continue;
        }
        QJsonArray st = p.raw.value(QStringLiteral("struct")).toArray();
        if (state < 0 || state >= st.size()) {
            return;
        }
        QJsonArray one = st.at(state).toArray();
        for (int i = 0; i < one.size(); ++i) {
            QJsonObject o = one.at(i).toObject();
            if (o.value(QStringLiteral("-name")).toString() != QLatin1String("rect")) {
                continue;
            }
            QJsonObject rr = o.value(QStringLiteral("rect")).toObject();
            rr.insert(QStringLiteral("x"), r.x());
            rr.insert(QStringLiteral("y"), r.y());
            rr.insert(QStringLiteral("width"), r.width());
            rr.insert(QStringLiteral("height"), r.height());
            o.insert(QStringLiteral("rect"), rr);
            one.replace(i, o);
            st.replace(state, one);
            p.raw.insert(QStringLiteral("struct"), st);
            p.dirty = true;
            m_dirty = true;
            if (state == 0) {
                rect = r;
            }
            return;
        }
    }
}

ProjectModel::ProjectModel() = default;

ProjectModel::~ProjectModel()
{
    clear();
}

void ProjectModel::clear()
{
    qDeleteAll(m_pages);
    m_pages.clear();
    m_rootRaw = QJsonObject();
    m_path.clear();
    m_activePage = 0;
    m_dirty = false;
}

QSize ProjectModel::pageSize() const
{
    if (m_pages.isEmpty()) {
        return QSize();
    }
    return m_pages.first()->rect.size();
}

UiNode *ProjectModel::nodeFromJson(const QJsonObject &o, UiNode *parent)
{
    auto *n = new UiNode;
    n->m_raw   = o;                 // 全量留底，保真回写的根本
    n->parent  = parent;
    n->cls     = o.value(QStringLiteral("-class")).toString();
    n->type    = o.value(QStringLiteral("-type")).toString();
    n->name    = o.value(QStringLiteral("-name")).toString();
    n->caption = o.value(QStringLiteral("caption")).toString();
    n->icon    = o.value(QStringLiteral("icon")).toString();
    n->tip     = o.value(QStringLiteral("tip")).toString();
    n->version = o.value(QStringLiteral("version")).toString();

    const QJsonArray propArr = o.value(QStringLiteral("property")).toArray();
    for (const QJsonValue &pv : propArr) {
        const QJsonObject po = pv.toObject();
        UiProperty p;
        p.raw     = po;
        p.name    = po.value(QStringLiteral("-name")).toString();
        p.type    = po.value(QStringLiteral("-type")).toString();
        p.caption = po.value(QStringLiteral("caption")).toString();
        p.ename   = po.value(QStringLiteral("ename")).toString();
        p.id      = po.value(QStringLiteral("id")).toInt();
        /* 页节点的 property[0] 是 {"rect":{...}}，没有 -name，也要认 */
        if (po.contains(QStringLiteral("rect"))) {
            const QJsonObject r = po.value(QStringLiteral("rect")).toObject();
            n->rect = QRect(r.value(QStringLiteral("x")).toInt(),
                            r.value(QStringLiteral("y")).toInt(),
                            r.value(QStringLiteral("width")).toInt(),
                            r.value(QStringLiteral("height")).toInt());
        }
        n->props.append(p);
    }

    /* 子节点：记住原文件里出现过哪些键（含空数组），回写时一个不多一个不少 */
    /* 控件的几何在 element_css 里，上面那轮没取到的在这里补 */
    if (!n->rect.isValid()) {
        n->rect = n->rectOf(0);
    }

    for (const char *key : kChildKeys) {
        const QString k = QLatin1String(key);
        if (!o.contains(k)) {
            continue;
        }
        n->m_childKeysPresent.append(k);
        const QJsonArray arr = o.value(k).toArray();
        for (const QJsonValue &cv : arr) {
            n->children.append(qMakePair(k, nodeFromJson(cv.toObject(), n)));
        }
    }
    return n;
}

QJsonObject ProjectModel::nodeToJson(const UiNode *n)
{
    /* 起点永远是原始对象：没动过的键、没建模的键都原样带走。
     * QJsonObject 的键天然按字典序，与原厂 Qt 写出来的顺序一致。 */
    QJsonObject o = n->m_raw;

    bool childDirty = false;
    for (const auto &c : n->children) {
        if (c.second->m_dirty || c.second->m_raw.isEmpty()) {
            childDirty = true;
        }
    }

    if (n->m_dirty) {
        o.insert(QStringLiteral("-class"),  n->cls);
        o.insert(QStringLiteral("-type"),   n->type);
        o.insert(QStringLiteral("-name"),   n->name);
        /* caption / icon 只在原本就有、或确实有值时才写，避免凭空多出键 */
        if (o.contains(QStringLiteral("caption")) || !n->caption.isEmpty()) {
            o.insert(QStringLiteral("caption"), n->caption);
        }
        if (o.contains(QStringLiteral("icon")) || !n->icon.isEmpty()) {
            o.insert(QStringLiteral("icon"), n->icon);
        }
        if (o.contains(QStringLiteral("tip")) || !n->tip.isEmpty()) {
            o.insert(QStringLiteral("tip"), n->tip);
        }
        if (o.contains(QStringLiteral("version")) || !n->version.isEmpty()) {
            o.insert(QStringLiteral("version"), n->version);
        }

        QJsonArray propArr;
        for (const UiProperty &p : n->props) {
            QJsonObject po = p.raw;
            if (p.dirty) {
                po.insert(QStringLiteral("-name"),   p.name);
                po.insert(QStringLiteral("-type"),   p.type);
                if (po.contains(QStringLiteral("caption")) || !p.caption.isEmpty()) {
                    po.insert(QStringLiteral("caption"), p.caption);
                }
                if (po.contains(QStringLiteral("ename")) || !p.ename.isEmpty()) {
                    po.insert(QStringLiteral("ename"), p.ename);
                }
            }
            /* 页节点那种裸 rect 直接写回；控件的 rect 在 element_css 里，
             * 由 setRectOf() 负责，这里不碰。 */
            if (po.contains(QStringLiteral("rect")) && p.name.isEmpty()) {
                QJsonObject r = po.value(QStringLiteral("rect")).toObject();
                r.insert(QStringLiteral("x"),      n->rect.x());
                r.insert(QStringLiteral("y"),      n->rect.y());
                r.insert(QStringLiteral("width"),  n->rect.width());
                r.insert(QStringLiteral("height"), n->rect.height());
                po.insert(QStringLiteral("rect"), r);
            }
            propArr.append(po);
        }
        if (!propArr.isEmpty() || o.contains(QStringLiteral("property"))) {
            o.insert(QStringLiteral("property"), propArr);
        }
    }

    /* 子节点数组：只要有任何一个子树动过，就整组重建；
     * 键的集合取「原文件出现过的」∪「现在实际有的」。 */
    if (n->m_dirty || childDirty) {
        QVector<QString> keys = n->m_childKeysPresent;
        for (const auto &c : n->children) {
            if (!keys.contains(c.first)) {
                keys.append(c.first);
            }
        }
        for (const QString &k : keys) {
            QJsonArray arr;
            for (const auto &c : n->children) {
                if (c.first == k) {
                    arr.append(nodeToJson(c.second));
                }
            }
            o.insert(k, arr);
        }
    } else {
        /* 本节点没动，但子孙可能动了 —— 递归下去按需重建 */
        for (const QString &k : n->m_childKeysPresent) {
            bool need = false;
            for (const auto &c : n->children) {
                if (c.first == k) {
                    c.second->forEach([&need](UiNode *x) {
                        if (x->isDirty()) {
                            need = true;
                        }
                        return !need;
                    });
                }
            }
            if (!need) {
                continue;
            }
            QJsonArray arr;
            for (const auto &c : n->children) {
                if (c.first == k) {
                    arr.append(nodeToJson(c.second));
                }
            }
            o.insert(k, arr);
        }
    }
    return o;
}

bool ProjectModel::load(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) {
            *err = QStringLiteral("打不开 %1: %2").arg(path, f.errorString());
        }
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (err) {
            *err = QStringLiteral("json 解析失败 @%1: %2").arg(pe.offset).arg(pe.errorString());
        }
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("-type")).toString() != QLatin1String("project")) {
        if (err) {
            *err = QStringLiteral("不是 ui-tools 工程文件（顶层 -type 应为 project）");
        }
        return false;
    }

    clear();
    m_rootRaw    = root;
    m_path       = path;
    m_name       = root.value(QStringLiteral("-name")).toString();
    m_activePage = root.value(QStringLiteral("activePage")).toInt();
    m_langExcel  = root.value(QStringLiteral("lang_excel")).toString();

    const QJsonArray pageArr = root.value(QStringLiteral("pages")).toArray();
    for (const QJsonValue &pv : pageArr) {
        m_pages.append(nodeFromJson(pv.toObject(), nullptr));
    }
    m_dirty = false;
    return true;
}

QByteArray ProjectModel::toJsonBytes() const
{
    QJsonObject root = m_rootRaw;
    if (m_dirty) {
        root.insert(QStringLiteral("-name"),      m_name);
        root.insert(QStringLiteral("-type"),      QStringLiteral("project"));
        root.insert(QStringLiteral("activePage"), m_activePage);
        if (root.contains(QStringLiteral("lang_excel")) || !m_langExcel.isEmpty()) {
            root.insert(QStringLiteral("lang_excel"), m_langExcel);
        }
    } else {
        /* activePage 是纯视图状态，原厂也会随手更新，单独放行 */
        root.insert(QStringLiteral("activePage"), m_activePage);
    }

    bool pagesDirty = m_dirty;
    for (UiNode *p : m_pages) {
        p->forEach([&pagesDirty](UiNode *x) {
            if (x->isDirty()) {
                pagesDirty = true;
            }
            return !pagesDirty;
        });
    }
    if (pagesDirty || !root.contains(QStringLiteral("pages"))) {
        QJsonArray pageArr;
        for (const UiNode *p : m_pages) {
            pageArr.append(nodeToJson(p));
        }
        root.insert(QStringLiteral("pages"), pageArr);
    }

    /* 原厂就是 QJsonDocument::toJson(Indented)：4 空格、键字典序、
     * 空数组 "[\n<缩进>]"、UTF-8 不转义、末尾一个 \n。二进制写出即字节一致。 */
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool ProjectModel::save(const QString &path, QString *err) const
{
    /* QSaveFile：写一半崩掉不会毁掉原工程 —— 这类文件动辄几 MB，
     * 写崩一次就是一天的活。注意必须是二进制模式，原厂是纯 LF。 */
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) {
            *err = QStringLiteral("打不开 %1: %2").arg(path, f.errorString());
        }
        return false;
    }
    f.write(toJsonBytes());
    if (!f.commit()) {
        if (err) {
            *err = QStringLiteral("写入失败: %1").arg(f.errorString());
        }
        return false;
    }
    return true;
}

bool ProjectModel::saveAutosave(const QString &dir, QString *err) const
{
    return save(QDir(dir).filePath(QStringLiteral("autosave.json")), err);
}

bool ProjectModel::verifyRoundTrip(const QString &path, QString *report)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (report) {
            *report = QStringLiteral("打不开 %1").arg(path);
        }
        return false;
    }
    const QByteArray orig = f.readAll();

    ProjectModel m;
    QString err;
    if (!m.load(path, &err)) {
        if (report) {
            *report = QStringLiteral("解析失败: %1").arg(err);
        }
        return false;
    }
    const QByteArray again = m.toJsonBytes();
    if (again == orig) {
        if (report) {
            *report = QStringLiteral("工程 json 往返一致：%1 字节逐字节相同").arg(orig.size());
        }
        return true;
    }
    int diff = -1;
    const int n = qMin(orig.size(), again.size());
    for (int i = 0; i < n; ++i) {
        if (orig.at(i) != again.at(i)) {
            diff = i;
            break;
        }
    }
    if (report) {
        QString ctx;
        if (diff >= 0) {
            const int a = qMax(0, diff - 60);
            ctx = QStringLiteral("\n  原 : ...%1...\n  新 : ...%2...")
                  .arg(QString::fromUtf8(orig.mid(a, 120)),
                       QString::fromUtf8(again.mid(a, 120)));
        }
        *report = QStringLiteral("工程 json 往返不一致：原 %1 B，重写 %2 B，首个差异 @%3%4")
                  .arg(orig.size()).arg(again.size()).arg(diff).arg(ctx);
    }
    return false;
}

void ProjectModel::createDefault(const QString &projName, const QSize &pageSize)
{
    clear();
    m_name = projName;
    m_dirty = true;

    auto mkRect = [](const QRect &r) {
        QJsonObject rp;
        rp.insert(QStringLiteral("-name"),   QStringLiteral("rect"));
        rp.insert(QStringLiteral("-type"),   QStringLiteral("rect"));
        rp.insert(QStringLiteral("caption"), QStringLiteral("坐标"));
        QJsonObject rr;
        rr.insert(QStringLiteral("x"), r.x());
        rr.insert(QStringLiteral("y"), r.y());
        rr.insert(QStringLiteral("width"), r.width());
        rr.insert(QStringLiteral("height"), r.height());
        rp.insert(QStringLiteral("rect"), rr);
        return rp;
    };

    auto *page = new UiNode;
    page->markDirty();
    page->cls = page->name = QStringLiteral("ScenesScreen");
    page->type = QStringLiteral("page");
    page->caption = QStringLiteral("页面_0");
    page->rect = QRect(1, 1, pageSize.width(), pageSize.height());
    UiProperty rp;
    rp.name = rp.type = QStringLiteral("rect");
    rp.caption = QStringLiteral("坐标");
    rp.raw = mkRect(page->rect);
    page->props.append(rp);
    page->m_childKeysPresent.append(QStringLiteral("layer"));

    auto *layer = new UiNode;
    layer->markDirty();
    layer->parent = page;
    layer->cls = layer->type = QStringLiteral("NewLayer");
    layer->name = QStringLiteral("图层_0");
    layer->caption = QStringLiteral("图层");
    layer->icon = QStringLiteral("config/images/layer.ico");
    layer->rect = QRect(0, 0, pageSize.width(), pageSize.height());
    UiProperty lrp = rp;
    lrp.raw = mkRect(layer->rect);
    layer->props.append(lrp);
    layer->m_childKeysPresent.append(QStringLiteral("layout"));
    layer->m_childKeysPresent.append(QStringLiteral("widget"));
    page->children.append(qMakePair(QStringLiteral("layer"), layer));

    auto *layout = new UiNode;
    layout->markDirty();
    layout->parent = layer;
    layout->cls = layout->type = QStringLiteral("NewLayout");
    layout->name = QStringLiteral("布局_1");
    layout->caption = QStringLiteral("布局");
    layout->icon = QStringLiteral("config/images/layout.ico");
    layout->rect = QRect(0, 0, pageSize.width(), pageSize.height());
    UiProperty orp = rp;
    orp.raw = mkRect(layout->rect);
    layout->props.append(orp);
    layout->m_childKeysPresent.append(QStringLiteral("layout"));
    layout->m_childKeysPresent.append(QStringLiteral("widget"));
    layer->children.append(qMakePair(QStringLiteral("layout"), layout));

    m_pages.append(page);
}

/* ===================== 节点级搬运原语 ===================== */

QJsonObject ProjectModel::toJsonObject(const UiNode *n)
{
    return nodeToJson(n);
}

UiNode *ProjectModel::fromJsonObject(const QJsonObject &o, UiNode *parent)
{
    return nodeFromJson(o, parent);
}

UiNode *ProjectModel::cloneNode(const UiNode *src, UiNode *parent)
{
    if (!src) {
        return nullptr;
    }
    UiNode *n = nodeFromJson(nodeToJson(src), parent);
    /* 克隆件必须整棵标脏。它的 m_raw 是从源节点抄来的，位置、名字马上就要
     * 被改（粘贴到别处、重命名避免重名），底稿已经不可信了；不标脏的话
     * nodeToJson() 会直接吐原始 m_raw，改动全丢。 */
    n->forEach([](UiNode *x) { x->markDirty(); return true; });
    return n;
}

int ProjectModel::detachAndDelete(UiNode *n)
{
    if (!n || !n->parent) {
        return -1;
    }
    UiNode *p = n->parent;
    for (int i = 0; i < p->children.size(); ++i) {
        if (p->children.at(i).second == n) {
            p->children.removeAt(i);
            p->markDirty();
            delete n;
            return i;
        }
    }
    return -1;
}

/* ===================== 节点编号与显示名 ===================== */

/** 前序遍历全部页的非页节点；f 返回 false 就停。 */
static void forEachNonPage(const QVector<UiNode *> &pages,
                           const std::function<bool(UiNode *, int)> &f)
{
    int seq = 0;
    bool stop = false;
    std::function<void(UiNode *)> rec = [&](UiNode *n) {
        if (stop) {
            return;
        }
        if (n->parent) {                 // 页节点不占号
            if (!f(n, seq)) {
                stop = true;
                return;
            }
            ++seq;
        }
        for (const auto &c : n->children) {
            rec(c.second);
            if (stop) {
                return;
            }
        }
    };
    for (UiNode *p : pages) {
        rec(p);
        if (stop) {
            break;
        }
    }
}

int ProjectModel::nextNodeSeq() const
{
    int n = 0;
    forEachNonPage(m_pages, [&n](UiNode *, int) { ++n; return true; });
    return n;
}

QString ProjectModel::uniqueEname(const QString &base) const
{
    QSet<QString> used;
    forEachNonPage(m_pages, [&used](UiNode *x, int) {
        for (const UiProperty &p : x->props) {
            if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
                used.insert(p.ename.toUpper());
            }
        }
        return true;
    });
    if (!used.contains(base.toUpper())) {
        return base;
    }
    for (int i = 1; ; ++i) {
        const QString cand = QStringLiteral("%1_%2").arg(base).arg(i);
        if (!used.contains(cand.toUpper())) {
            return cand;
        }
    }
}

int ProjectModel::nodeSeq(const UiNode *node) const
{
    int found = -1;
    forEachNonPage(m_pages, [&](UiNode *x, int i) {
        if (x == node) {
            found = i;
            return false;
        }
        return true;
    });
    return found;
}

/**
 * 对象树第一列显示什么。
 *
 * 【为什么不能直接显示 -name】原厂工程里绝大多数节点的 -name 就是
 * "<caption>_<序号>"（图层_0 / 电池电量_2 …），直接显示没问题；但
 * VerticalList 这类节点的 -name 是**裸的类型名** "VerticalList"，
 * 而原厂界面上显示的是 "垂直列表_16" —— 也就是说原厂在 -name 没被正经
 * 命名过的时候，是拿 caption + 序号 现算的。之前我照搬 -name，树上就
 * 冒出一个英文 "VerticalList"，和原厂对不上。
 *
 * 【说清楚不确定的地方】能区分这两种规则的样本只有这一种（-name == -type），
 * 所以"什么算没正经命名"我取的是最保守的判据：空、等于 -type、等于 -class。
 * 用户自己起的名字一律原样显示，不会被覆盖。
 */
QString ProjectModel::displayName(const UiNode *n) const
{
    if (!n) {
        return QString();
    }
    const bool unnamed = n->name.isEmpty()
                         || n->name == n->type
                         || n->name == n->cls;
    if (!unnamed) {
        return n->name;
    }
    const int i = nodeSeq(n);
    const QString base = n->caption.isEmpty() ? n->type : n->caption;
    return i < 0 ? base : QStringLiteral("%1_%2").arg(base).arg(i);
}
