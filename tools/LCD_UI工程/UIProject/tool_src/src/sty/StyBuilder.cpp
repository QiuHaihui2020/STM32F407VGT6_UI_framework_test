#include "StyBuilder.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRect>
#include <functional>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QtEndian>

namespace sty {

// ------------------------------------------------------------ 小工具 ----
namespace {

const int HEAD_SZ = 24;         ///< 文件头
const int WHEAD_SZ = 20;        ///< 页表项
const int CHEAD_SZ = 16;        ///< 控件头
const int CSS_SZ = 36;          ///< element_css1
const int WINREC_SZ = 28;       ///< 页头的 window_info

quint16 crc16(const char *p, int n, quint16 crc = 0)
{
    for (int i = 0; i < n; ++i) {
        crc ^= quint16(quint8(p[i])) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
        }
    }
    return crc;
}
quint16 crc16(const QByteArray &b, quint16 c = 0) { return crc16(b.constData(), b.size(), c); }

quint32 crc32(const QByteArray &data)
{
    static quint32 t[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (int i = 0; i < data.size(); ++i) {
        c = t[(c ^ quint8(data.at(i))) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

inline int align4(int n) { return (n + 3) & ~3; }

void poke16(QByteArray &b, int off, quint16 v)
{
    qToLittleEndian(v, reinterpret_cast<uchar *>(b.data()) + off);
}
void poke32(QByteArray &b, int off, quint32 v)
{
    qToLittleEndian(v, reinterpret_cast<uchar *>(b.data()) + off);
}
void put16(QByteArray &b, quint16 v) { char t[2]; qToLittleEndian(v, reinterpret_cast<uchar *>(t)); b.append(t, 2); }
void put32(QByteArray &b, quint32 v) { char t[4]; qToLittleEndian(v, reinterpret_cast<uchar *>(t)); b.append(t, 4); }


/**
 * 数字感知的次序比较：把连续数字段当整数比，其余按字符比。
 *
 * 文字 ResID 编号用的是这个次序（m1 < m2 < m3 < m6 < m22 < m30），
 * 不是 QStringList::sort() 的字典序（那会得到 m1 < m22 < m3）。
 * 证据见 docs/FILE_FORMATS.md。
 *
 * @note 这套工程的 ResID 全是 m<数字> 形式，所以"按数字排"和"按 xls 行序排"
 *       在现有数据上结果相同，无法区分。选数字序是因为它不依赖 xls 能不能读到。
 */
static bool natLess(const QString &a, const QString &b)
{
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const QChar ca = a.at(i), cb = b.at(j);
        if (ca.isDigit() && cb.isDigit()) {
            int si = i, sj = j;
            while (i < a.size() && a.at(i).isDigit()) {
                ++i;
            }
            while (j < b.size() && b.at(j).isDigit()) {
                ++j;
            }
            const QString na = a.mid(si, i - si), nb = b.mid(sj, j - sj);
            const qlonglong va = na.toLongLong(), vb = nb.toLongLong();
            if (va != vb) {
                return va < vb;
            }
            continue;
        }
        if (ca != cb) {
            return ca < cb;
        }
        ++i;
        ++j;
    }
    return (a.size() - i) < (b.size() - j);
}

/**
 * 万分比换算：`(v * 10000 + ref - 1) / ref`，**正负都走这一条**，
 * 除法用 C 的向零截断。正数看起来像"向上取整"（95*10000/128=7421.875 -> 7422）。
 *
 * 【负数不要另开分支】以前这里给负数单独写了 -((-num)/d)，等于对负数做向零
 * 截断，对不上既有资源：那个 y=-11 的控件（父高 64），
 *     分正负写   -((110000)/64)      = -1718
 *     统一式子   (-110000 + 63) / 64 = -1717   （project.bin 0x54C4 实测）
 * 差 1。正确的做法是不分正负套同一个式子，负数时 "+ref-1" 把商往零推一格。
 * 这套工程里只有一个负坐标，所以一直没暴露。
 */
int perMyriad(int v, int ref)
{
    if (ref == 0) {
        return 0;
    }
    const qint64 num = qint64(v) * 10000 + ref - 1;
    return int(num / ref);          // C++ 整数除法向零截断
}

/// "#AARRGGBB" -> (RGB565, alpha 百分比)。空串是"未设置"，用 0xFFFFFF/100 当哨兵。
void argbTo565(const QString &s, quint32 *rgb, quint32 *alpha)
{
    if (s.isEmpty()) {
        *rgb = 0xFFFFFFu;
        *alpha = 100;
        return;
    }
    QString t = s;
    if (t.startsWith(QLatin1Char('#'))) {
        t.remove(0, 1);
    }
    int a = 255, r = 0, g = 0, b = 0;
    if (t.size() == 8) {
        a = t.mid(0, 2).toInt(nullptr, 16);
        r = t.mid(2, 2).toInt(nullptr, 16);
        g = t.mid(4, 2).toInt(nullptr, 16);
        b = t.mid(6, 2).toInt(nullptr, 16);
    } else if (t.size() == 6) {
        r = t.mid(0, 2).toInt(nullptr, 16);
        g = t.mid(2, 2).toInt(nullptr, 16);
        b = t.mid(4, 2).toInt(nullptr, 16);
    } else {
        *rgb = 0xFFFFFFu;
        *alpha = 100;
        return;
    }
    *rgb = quint32(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    *alpha = quint32(qRound(a * 100.0 / 255.0));
}

/// 22 种语言的键名与中文名。这份表配置文件里没有，只能写在程序里。
/// 顺序即语言编号 1..22，result.h 的宏就是按这个顺序生成的。
static const char *const kLangKeys[22] = {
    "Chinese_Simplified", "Chinese_Traditional", "Japanese", "Korean",
    "English", "French", "German", "Italian",
    "Dutch", "Portuguese", "Spanish", "Swedish",
    "Czech", "Danish", "Polish", "Russian",
    "Turkey", "Hebrew", "Thai", "Hungarian",
    "Romanian", "Arabic",
};
static const char *const kLangNames[22] = {
    "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87",     // 简体中文
    "\xe7\xb9\x81\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87",     // 繁体中文
    "\xe6\x97\xa5\xe8\xaf\xad",                             // 日语
    "\xe9\x9f\xa9\xe8\xaf\xad",                             // 韩语
    "\xe8\x8b\xb1\xe8\xaf\xad",                             // 英语
    "\xe6\xb3\x95\xe8\xaf\xad",                             // 法语
    "\xe5\xbe\xb7\xe8\xaf\xad",                             // 德语
    "\xe6\x84\x8f\xe5\xa4\xa7\xe5\x88\xa9\xe8\xaf\xad",     // 意大利语
    "\xe8\x8d\xb7\xe5\x85\xb0\xe8\xaf\xad",                 // 荷兰语
    "\xe8\x91\xa1\xe8\x90\x84\xe7\x89\x99\xe8\xaf\xad",     // 葡萄牙语
    "\xe8\xa5\xbf\xe7\x8f\xad\xe7\x89\x99\xe8\xaf\xad",     // 西班牙语
    "\xe7\x91\x9e\xe5\x85\xb8\xe8\xaf\xad",                 // 瑞典语
    "\xe6\x8d\xb7\xe5\x85\x8b\xe8\xaf\xad",                 // 捷克语
    "\xe4\xb8\xb9\xe9\xba\xa6\xe8\xaf\xad",                 // 丹麦语
    "\xe6\xb3\xa2\xe5\x85\xb0\xe8\xaf\xad",                 // 波兰语
    "\xe4\xbf\x84\xe5\x9b\xbd\xe8\xaf\xad",                 // 俄国语
    "\xe5\x9c\x9f\xe8\x80\xb3\xe5\x85\xb6\xe8\xaf\xad",     // 土耳其语
    "\xe5\xb8\x8c\xe4\xbc\xaf\xe6\x9d\xa5\xe8\xaf\xad",     // 希伯来语
    "\xe6\xb3\xb0\xe8\xaf\xad",                             // 泰语
    "\xe5\x8c\x88\xe7\x89\x99\xe5\x88\xa9\xe8\xaf\xad",     // 匈牙利语
    "\xe7\xbd\x97\xe9\xa9\xac\xe5\xb0\xbc\xe4\xba\x9a\xe8\xaf\xad", // 罗马尼亚语
    "\xe9\x98\xbf\xe6\x8b\x89\xe4\xbc\xaf\xe8\xaf\xad",     // 阿拉伯语
};

/// 文件名 -> 宏名。不做字符净化，理由见 ResBuilderCore.cpp 的同名函数。
QString symbolOf(const QString &path)
{
    return QFileInfo(path).completeBaseName().toUpper();
}

} // namespace

// -------------------------------------------------------------- 节点 ----

struct Builder::Node {
    QJsonObject obj;
    Node *parent = nullptr;
    QVector<Node *> kids;
    QString ename;
    QString typeName;
    QString caption;
    int     typeCode = 0;
    int     page = 0;
    qint32  id = 0;

    int     recOff = 0;                 ///< 页内偏移
    int     len = 0;                    ///< 记录总长
    QByteArray rec;                     ///< 完整记录（头+负载）

    /// 需要指向数据区的字段：(字段偏移, 块内容, 是否回填指针)
    QVector<QPair<int, QByteArray> > dataFields;
    QVector<bool>                    dataPatch;
    QVector<int>                     ctrlPtrFields;   ///< 指向第一个孩子的字段偏移

    ~Node() { qDeleteAll(kids); }
};

// -------------------------------------------------------------- 读入 ----

bool Builder::loadProject(const QString &jsonPath, QString *error)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("打不开 %1").arg(jsonPath);
        }
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("json 解析失败 @%1: %2").arg(pe.offset).arg(pe.errorString());
        }
        return false;
    }
    m_doc = d.object();
    m_jsonPath = jsonPath;
    return true;
}

bool Builder::loadOptionIni(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("打不开 %1").arg(path);
        }
        return false;
    }
    // option.ini 里有中文键（窗口=1 图层=4 …）。这份文件是 UTF-8 带 BOM，
    // 但别的机器上可能是本地代码页，两种都收。
    QByteArray raw = f.readAll();
    if (raw.startsWith("\xEF\xBB\xBF")) {
        raw.remove(0, 3);
    }
    QString text = QString::fromUtf8(raw);
    if (text.contains(QChar(0xFFFD))) {
        text = QString::fromLocal8Bit(raw);
    }
    bool inControl = false;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inControl = (line.compare(QLatin1String("[Control]"), Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inControl || line.startsWith(QLatin1Char(';')) || !line.contains(QLatin1Char('='))) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        m_typeCode.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed().toInt());
    }
    if (m_typeCode.isEmpty() && error) {
        *error = QStringLiteral("%1 里没有 [Control] 段").arg(path);
        return false;
    }
    return true;
}

// ------------------------------------------------------- 构建的辅助 ----
namespace {

QJsonObject propOf(const QJsonObject &n, const QString &name, int nth = 0)
{
    const QJsonArray arr = n.value(QStringLiteral("property")).toArray();
    int seen = 0;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("-name")).toString() == name) {
            if (seen == nth) {
                return o;
            }
            ++seen;
        }
    }
    return QJsonObject();
}

/// element_css.struct[state] 摊成 名字 -> 对象
QMap<QString, QJsonObject> cssState(const QJsonObject &n, int state)
{
    QMap<QString, QJsonObject> out;
    const QJsonObject p = propOf(n, QStringLiteral("element_css"));
    const QJsonArray st = p.value(QStringLiteral("struct")).toArray();
    if (state < 0 || state >= st.size()) {
        return out;
    }
    for (const QJsonValue &v : st.at(state).toArray()) {
        const QJsonObject o = v.toObject();
        out.insert(o.value(QStringLiteral("-name")).toString(), o);
    }
    return out;
}

int cssStateCount(const QJsonObject &n)
{
    const QJsonObject p = propOf(n, QStringLiteral("element_css"));
    if (p.isEmpty()) {
        return 0;
    }
    return qMax(1, p.value(QStringLiteral("struct")).toArray().size());
}

/// 控件矩形：普通控件在 element_css.struct[0].rect，页节点是裸 property[0].rect
bool rectOf(const QJsonObject &n, int state, QRect *out)
{
    const QMap<QString, QJsonObject> cs = cssState(n, state);
    if (cs.contains(QStringLiteral("rect"))) {
        const QJsonObject r = cs.value(QStringLiteral("rect"))
                              .value(QStringLiteral("rect")).toObject();
        *out = QRect(r.value(QStringLiteral("x")).toInt(),
                     r.value(QStringLiteral("y")).toInt(),
                     r.value(QStringLiteral("width")).toInt(),
                     r.value(QStringLiteral("height")).toInt());
        return true;
    }
    for (const QJsonValue &v : n.value(QStringLiteral("property")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("rect")) && !o.contains(QStringLiteral("-name"))) {
            const QJsonObject r = o.value(QStringLiteral("rect")).toObject();
            *out = QRect(r.value(QStringLiteral("x")).toInt(),
                         r.value(QStringLiteral("y")).toInt(),
                         r.value(QStringLiteral("width")).toInt(),
                         r.value(QStringLiteral("height")).toInt());
            return true;
        }
    }
    return false;
}

int enumValue(const QJsonObject &prop, int def = 0)
{
    const QString d = prop.value(QStringLiteral("default")).toString();
    for (const QJsonValue &v : prop.value(QStringLiteral("enum")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.contains(d)) {
            return o.value(d).toInt();
        }
    }
    const QJsonValue dv = prop.value(QStringLiteral("default"));
    return dv.isDouble() ? dv.toInt() : def;
}

void putCStr(QByteArray &rec, int off, const QString &s, int size)
{
    const QByteArray b = s.toLatin1();
    for (int i = 0; i < size; ++i) {
        rec[off + i] = char(i < b.size() ? b.at(i) : '\0');
    }
}

} // namespace

// -------------------------------------------------------------- 构建 ----

Output Builder::build(const Options &opt)
{
    Output out;
    const QJsonArray pages = m_doc.value(QStringLiteral("pages")).toArray();
    if (pages.isEmpty()) {
        out.error = QStringLiteral("工程里没有页面");
        return out;
    }

    // ---- 1. 建树，按"孩子块序"排好每页的记录顺序 ----
    QVector<Node *> roots;
    QVector<QVector<Node *> > order(pages.size());

    std::function<void(Node *, int)> attach = [&](Node *n, int page) {
        static const char *kKeys[] = { "layer", "layout", "widget", "listwidget" };
        for (const char *k : kKeys) {
            for (const QJsonValue &v : n->obj.value(QLatin1String(k)).toArray()) {
                Node *c = new Node;
                c->obj = v.toObject();
                c->parent = n;
                c->page = page;
                n->kids.append(c);
                attach(c, page);
            }
        }
    };
    std::function<void(Node *, QVector<Node *> &)> childBlock =
        [&](Node *n, QVector<Node *> &acc) {
            for (Node *c : n->kids) {
                acc.append(c);
            }
            for (Node *c : n->kids) {
                childBlock(c, acc);
            }
        };

    for (int pi = 0; pi < pages.size(); ++pi) {
        Node *root = new Node;
        root->obj = pages.at(pi).toObject();
        root->page = pi;
        attach(root, pi);
        roots.append(root);
        childBlock(root, order[pi]);
    }

    // ---- 2. 名字 / 类型码 ----
    for (int pi = 0; pi < order.size(); ++pi) {
        for (Node *n : order[pi]) {
            n->ename = propOf(n->obj, QStringLiteral("id"))
                       .value(QStringLiteral("ename")).toString();
            n->typeName = n->obj.value(QStringLiteral("-type")).toString();
            n->caption = n->obj.value(QStringLiteral("caption")).toString();
            // 查表键：**caption 优先，再退回 -type**。扩展控件（slider/vslider 及零件）
            // 的 -type 都是 NewLayout/ImageList/Text，真正决定类型码的是 caption。
            if (m_typeCode.contains(n->caption)) {
                n->typeCode = m_typeCode.value(n->caption);
            } else if (m_typeCode.contains(n->typeName)) {
                n->typeCode = m_typeCode.value(n->typeName);
            } else {
                out.warnings.append(QStringLiteral("认不出控件类型: %1 (caption=%2 -type=%3)")
                                    .arg(n->ename, n->caption, n->typeName));
            }
        }
    }

    // ---- 3. 资源号：必须和 ResBuilder 的分配规则一致 ----
    //      图片：每页把引用到的图片按宏名排序，1..N
    //      文字：所有页的 cell 去重后按名字排序，1..N（全局）
    QVector<QStringList> pagePicRefs(order.size());     // 页内引用顺序（给 Resbuilder.xml）
    QVector<QStringList> pageCellRefs(order.size());
    QVector<QStringList> pageColors(order.size());
    QVector<QMap<QString, int> > picId(order.size());
    QMap<QString, int> cellId;

    auto addUnique = [](QStringList &lst, const QString &v) {
        if (!v.isEmpty() && !lst.contains(v)) {
            lst.append(v);
        }
    };

    QStringList allCells;
    for (int pi = 0; pi < order.size(); ++pi) {
        for (Node *n : order[pi]) {
            for (const QJsonValue &v : n->obj.value(QStringLiteral("property")).toArray()) {
                const QJsonObject p = v.toObject();
                const QString ptype = p.value(QStringLiteral("-type")).toString();
                if (ptype == QLatin1String("piclist") || ptype == QLatin1String("arrlist")) {
                    for (const QJsonValue &x : p.value(QStringLiteral("list")).toArray()) {
                        addUnique(pagePicRefs[pi], x.toString());
                    }
                } else if (ptype == QLatin1String("text-pic")) {
                    for (const QJsonValue &x : p.value(QStringLiteral("list")).toArray()) {
                        const QString c = x.toString();
                        if (c.isEmpty()) {
                            continue;
                        }
                        addUnique(pageCellRefs[pi], c);
                        if (!allCells.contains(c)) {
                            allCells.append(c);
                        }
                    }
                } else if (ptype == QLatin1String("background-color")
                           || ptype == QLatin1String("color")) {
                    const QString c = p.value(QStringLiteral("background-color")).toString()
                                      + p.value(QStringLiteral("color")).toString();
                    Q_UNUSED(c)
                }
            }
            const int ns = cssStateCount(n->obj);
            for (int st = 0; st < ns; ++st) {
                const QMap<QString, QJsonObject> cs = cssState(n->obj, st);
                const QString bi = cs.value(QStringLiteral("background_image"))
                                   .value(QStringLiteral("background-image")).toString();
                addUnique(pagePicRefs[pi], bi);
                const QString bc = cs.value(QStringLiteral("background_color"))
                                   .value(QStringLiteral("background-color")).toString();
                const QString brc = cs.value(QStringLiteral("border"))
                                    .value(QStringLiteral("color")).toString();
                for (const QString &c : QStringList{ bc, brc }) {
                    if (c.size() >= 6) {
                        addUnique(pageColors[pi], c.right(6).toUpper());
                    }
                }
            }
            for (const QJsonValue &v : n->obj.value(QStringLiteral("property")).toArray()) {
                const QJsonObject p = v.toObject();
                const QString ptype = p.value(QStringLiteral("-type")).toString();
                if (ptype == QLatin1String("background-color") || ptype == QLatin1String("color")) {
                    QString c = p.value(QStringLiteral("background-color")).toString();
                    if (c.isEmpty()) {
                        c = p.value(QStringLiteral("color")).toString();
                    }
                    if (c.size() >= 6) {
                        addUnique(pageColors[pi], c.right(6).toUpper());
                    }
                }
            }
        }
        // 每页的 ColorList 里一定要有黑色，页 1 的 JSON 里一处 000000 都没有，
        // 输出里却排在最后 —— 就是补上去的。
        if (!pageColors[pi].contains(QStringLiteral("000000"))) {
            pageColors[pi].append(QStringLiteral("000000"));
        }
        QStringList syms;
        for (const QString &p : pagePicRefs[pi]) {
            syms.append(symbolOf(p));
        }
        /* 【自然序】必须和 ResBuilderCore::collectPictures() 一致，
         * 否则 project.bin 里引用的图片号和 result_pic_index.h 对不上。 */
        std::sort(syms.begin(), syms.end(), natLess);
        for (int i = 0; i < syms.size(); ++i) {
            picId[pi].insert(syms.at(i), i + 1);
        }
    }
    /* 【按数字排，不是字典序】和 ResBuilderCore::collectStrings() 必须一致，
     * 否则 project.bin 里写的字符串号和 result_str_index.h 对不上。 */
    std::sort(allCells.begin(), allCells.end(), natLess);
    for (int i = 0; i < allCells.size(); ++i) {
        cellId.insert(allCells.at(i), i + 1);
    }

    // ---- 4. id 分配 ----
    QMap<QString, quint32> enameIds;        // 宏名 -> 完整 id
    QMap<QString, quint32> imported;
    if (!opt.enameIn.isEmpty()) {
        QFile f(opt.enameIn);
        if (f.open(QIODevice::ReadOnly)) {
            const QString t = QString::fromUtf8(f.readAll());
            QRegularExpression re(QStringLiteral("#define\\s+(\\S+)\\s+0X([0-9A-Fa-f]+)"));
            QRegularExpressionMatchIterator it = re.globalMatch(t);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                imported.insert(m.captured(1), m.captured(2).toUInt(nullptr, 16));
            }
        }
    }

    QSet<quint32> usedLow;
    auto allocLow = [&](const QString &macro) -> quint32 {
        quint32 h = crc16(macro.toUtf8());
        while (usedLow.contains(h)) {              // 撞了就线性探测
            h = (h + 1) & 0xFFFF;
        }
        usedLow.insert(h);
        return h;
    };

    int noEname = 0;                    // 没有 ID 号的节点，页内递增序号
    for (int pi = 0; pi < order.size(); ++pi) {
        for (Node *n : order[pi]) {
            const QString macro = n->ename.toUpper();
            /* 【空宏名一个字节都不能进 ename.h】没有"唯一ID号"的控件
             * （旧版建出来的空壳，连 id 这条属性都没有）macro 是空串，
             * 直接插进去就写出 `#define  0XC30002` —— 编译器一看就炸，
             * 而且这一行还会顶掉后面同为空名的那些。
             * 节点自己照样要分 id（.sty 里的记录得有），只是不登记宏名，
             * 并且当场报出来，让用户知道是哪个控件。 */
            if (macro.isEmpty()) {
                out.warnings.append(
                    QStringLiteral("页%1 的 %2（%3）没有唯一ID号，"
                                   "不会写进 ename.h，业务代码引用不到它")
                    .arg(pi).arg(n->caption.isEmpty() ? QStringLiteral("(无名)")
                                                      : n->caption,
                                 n->typeName));
                /* 【种子不能用指针】原来这里拿 quintptr(n) 当哈希种子 ——
                 * 同一个模型连跑两遍，节点地址不一样，.sty 就差字节，
                 * 18t 那条"生成两遍要稳定"会随机红。改成页号 + 页内序号。 */
                n->id = qint32((quint32(opt.pjId) << 29) | (quint32(pi) << 22)
                               | (quint32(n->typeCode & 0x3F) << 16)
                               | allocLow(QStringLiteral("__noename_%1_%2")
                                          .arg(pi).arg(noEname++)));
                continue;
            }
            if (imported.contains(macro)) {
                n->id = qint32(imported.value(macro));
                enameIds.insert(macro, quint32(n->id));
                continue;
            }
            const quint32 low = allocLow(macro);
            n->id = qint32((quint32(opt.pjId) << 29) | (quint32(pi) << 22)
                           | (quint32(n->typeCode & 0x3F) << 16) | low);
            enameIds.insert(macro, quint32(n->id));
        }
        const QString pageMacro = QStringLiteral("PAGE_%1").arg(pi);
        const quint32 pageId = imported.contains(pageMacro)
            ? imported.value(pageMacro)
            : ((quint32(opt.pjId) << 29) | (quint32(pi) << 22) | (2u << 16) | quint32(pi));
        enameIds.insert(pageMacro, pageId);
    }
    enameIds.insert(QStringLiteral("UI_ROTATE"),
                    imported.contains(QStringLiteral("UI_ROTATE"))
                    ? imported.value(QStringLiteral("UI_ROTATE")) : quint32(opt.rotate));

    // ---- 5. 每条记录的字节 ----
    // 落盘的指针是**纯页内偏移**：固件读的时候用 window_head.offset 当基址，
    // 高位（页号/工程号）只在控件 id 里有，指针字段实测全 0。
    for (int pi = 0; pi < order.size(); ++pi) {
        // 先定长度和页内偏移
        int cur = WINREC_SZ;
        for (Node *n : order[pi]) {
            static const QMap<int, int> kLen = {
                { 3, 24 }, { 4, 28 }, { 5, 28 }, { 7, 32 }, { 8, 36 }, { 9, 28 },
                { 10, 96 }, { 12, 48 }, { 13, 32 }, { 15, 96 },
                { 20, 24 }, { 21, 24 }, { 22, 24 }, { 23, 24 },
                { 24, 24 }, { 25, 16 }, { 26, 16 }, { 27, 16 },
                { 28, 24 }, { 29, 16 }, { 30, 16 }, { 31, 16 }, { 32, 16 },
                { 33, 24 }, { 34, 16 }, { 35, 16 }, { 36, 16 }, { 37, 16 },
            };
            n->len = kLen.value(n->typeCode, 16);
            n->recOff = cur;
            cur += n->len;
        }

        for (Node *n : order[pi]) {
            QByteArray rec(n->len, '\0');
            rec[0] = char(n->typeCode);
            rec[1] = char(n->kids.size());
            rec[2] = char(cssStateCount(n->obj));
            rec[3] = char(n->len);
            rec[4] = char(pi);
            rec[5] = char(0xFF);
            rec[6] = char(0xFF);
            rec[7] = char(0xFF);
            poke32(rec, 8, quint32(n->id));

            // ---- css 块 ----
            const int nState = cssStateCount(n->obj);
            if (nState > 0) {
                QByteArray css(nState * CSS_SZ, '\0');
                for (int st = 0; st < nState; ++st) {
                    const QMap<QString, QJsonObject> cs = cssState(n->obj, st);
                    const int o = st * CSS_SZ;
                    css[o + 0] = char(enumValue(cs.value(QStringLiteral("align"))));
                    css[o + 1] = char(enumValue(cs.value(QStringLiteral("invisible"))));
                    css[o + 2] = char(cs.value(QStringLiteral("z_order"))
                                      .value(QStringLiteral("default")).toInt());
                    css[o + 3] = char(0xFF);
                    QRect r, pr;
                    const bool haveOwn = rectOf(n->obj, st, &r);
                    bool haveParent =
                        rectOf(n->parent ? n->parent->obj : roots[pi]->obj, 0, &pr)
                        && pr.width() > 0 && pr.height() > 0;
                    /* 【父矩形取不到时绝不能留 0】css 里的左/上/宽/高是**万分比**，
                     * 全 0 就是"零尺寸"，固件照着画等于什么都不画 —— 整屏黑，
                     * 而且哪儿都不报错。实测就踩过：旧版「新建页面」没给页节点
                     * 建 rect 属性，那一页所有控件的几何全成了 0。
                     * 这里退到页面尺寸，并把这件事记进 debug 输出。 */
                    if (haveOwn && !haveParent) {
                        if (rectOf(roots[pi]->obj, 0, &pr)
                            && pr.width() > 0 && pr.height() > 0) {
                            haveParent = true;
                        } else {
                            pr = QRect(0, 0, 128, 64);
                            haveParent = true;
                        }
                        out.warnings.append(
                            QStringLiteral("页%1 的 %2 取不到父级尺寸，按 %3x%4 折算"
                                           "（工程里页面可能缺 rect 属性）")
                            .arg(pi).arg(n->ename).arg(pr.width()).arg(pr.height()));
                    }
                    if (haveOwn && haveParent) {
                        poke32(css, o + 4, quint32(perMyriad(r.x(), pr.width())));
                        poke32(css, o + 8, quint32(perMyriad(r.y(), pr.height())));
                        poke32(css, o + 12, quint32(perMyriad(r.width(), pr.width())));
                        poke32(css, o + 16, quint32(perMyriad(r.height(), pr.height())));
                    }
                    quint32 rgb = 0, alpha = 0;
                    argbTo565(cs.value(QStringLiteral("background_color"))
                              .value(QStringLiteral("background-color")).toString(), &rgb, &alpha);
                    poke32(css, o + 20, (alpha << 24) | rgb);

                    const QString img = cs.value(QStringLiteral("background_image"))
                                        .value(QStringLiteral("background-image")).toString();
                    poke32(css, o + 24, img.isEmpty() ? 0xFFFFFFFFu
                           : quint32(picId[pi].value(symbolOf(img), 0xFFFF)));

                    const QJsonObject bd = cs.value(QStringLiteral("border"));
                    const QJsonObject bw = bd.value(QStringLiteral("border")).toObject();
                    css[o + 28] = char(bw.value(QStringLiteral("left")).toInt());
                    css[o + 29] = char(bw.value(QStringLiteral("top")).toInt());
                    css[o + 30] = char(bw.value(QStringLiteral("right")).toInt());
                    css[o + 31] = char(bw.value(QStringLiteral("bottom")).toInt());
                    argbTo565(bd.value(QStringLiteral("color")).toString(), &rgb, &alpha);
                    poke32(css, o + 32, (alpha << 24) | rgb);
                }
                n->dataFields.append(qMakePair(12, css));
                n->dataPatch.append(true);
            }

            // ---- 各类型负载 ----
            auto imgList = [&](const QString &propName) {
                QByteArray blk;
                const QJsonArray lst = propOf(n->obj, propName)
                                       .value(QStringLiteral("list")).toArray();
                put16(blk, quint16(lst.size()));
                for (const QJsonValue &v : lst) {
                    blk.append(char(0));
                    blk.append(char(0));
                    poke16(blk, blk.size() - 2,
                           quint16(picId[pi].value(symbolOf(v.toString()), 0xFFFF)));
                }
                return blk;
            };
            auto textList = [&]() {
                QByteArray blk;
                const QJsonArray lst = propOf(n->obj, QStringLiteral("str"))
                                       .value(QStringLiteral("list")).toArray();
                put16(blk, quint16(lst.size()));
                for (const QJsonValue &v : lst) {
                    blk.append(char(0));
                    blk.append(char(0));
                    poke16(blk, blk.size() - 2,
                           quint16(cellId.value(v.toString(), 0xFFFF)));
                }
                return blk;
            };
            // 空列表照样要写一个 0x0000 占位块。区别在指针：
            //   图片列表（type 8/9）为空时指针留 null；
            //   文字列表（type 12 strlist）为空时**照样指过去**（num=0）。
            // 这不是猜的，是把真实文件里 273 个控件全统计了一遍的结果。
            auto addList = [&](int fieldOff, const QByteArray &blk, bool alwaysPatch = false) {
                n->dataFields.append(qMakePair(fieldOff, blk));
                n->dataPatch.append(alwaysPatch || blk.size() > 2);
            };
            auto arrU16 = [&](QByteArray &r, int off, const QString &propName, int count) {
                const QJsonArray lst = propOf(n->obj, propName)
                                       .value(QStringLiteral("list")).toArray();
                for (int i = 0; i < count; ++i) {
                    const quint16 v = i < lst.size()
                        ? quint16(picId[pi].value(symbolOf(lst.at(i).toString()), 0xFFFF))
                        : quint16(0xFFFF);
                    poke16(r, off + i * 2, v);
                }
            };
            auto colorAt = [&](int nth) {
                quint32 rgb = 0, a = 0;
                const QJsonObject p = propOf(n->obj, QStringLiteral("color"), nth);
                QString s = p.value(QStringLiteral("background-color")).toString();
                if (s.isEmpty()) {
                    s = p.value(QStringLiteral("color")).toString();
                }
                argbTo565(s, &rgb, &a);
                return (a << 24) | rgb;
            };

            switch (n->typeCode) {
            case 3:                                    // NewLayout / layout_info
                // 布局记录只有 action(@16) + ctrl(@20)，没有 step 字段
                // （PTRS 是从真实文件反推的，控件区 100% 字节覆盖都对得上）
                n->ctrlPtrFields.append(20);
                break;
            case 4:                                    // NewLayer / layer_info
                rec[16] = char(enumValue(propOf(n->obj, QStringLiteral("color_format"))));
                rec[17] = char(0xFF);
                rec[18] = char(0xFF);
                rec[19] = char(0xFF);
                n->ctrlPtrFields.append(24);
                break;
            case 5: {                                  // List / Grid
                // 这个字段在不同控件模板里名字不一样：表格叫 page_mode，
                // 垂直/水平列表叫 scroll_mode（枚举都是 SCROLL=0 / PAGE=1）
                QJsonObject pm = propOf(n->obj, QStringLiteral("page_mode"));
                if (pm.isEmpty()) {
                    pm = propOf(n->obj, QStringLiteral("scroll_mode"));
                }
                rec[16] = char(enumValue(pm));
                rec[17] = char(propOf(n->obj, QStringLiteral("highlight_index"))
                               .value(QStringLiteral("default")).toInt(-1));
                rec[18] = char(0xFF);       // 结构体对齐空洞，填 0xFF
                rec[19] = char(0xFF);
                n->ctrlPtrFields.append(24);
                break;
            }
            case 7:                                    // Button
                putCStr(rec, 16, propOf(n->obj, QStringLiteral("source"))
                        .value(QStringLiteral("default")).toString(), 8);
                n->ctrlPtrFields.append(28);
                break;
            case 8:                                    // ImageList / 图片
                // 结构体是 u8 highlight; u16 cent_x; u16 cent_y; 三个指针，
                // 自然对齐后 17 和 22..23 是空洞，填 0xFF
                rec[16] = char(propOf(n->obj, QStringLiteral("highlight"))
                               .value(QStringLiteral("default")).toInt());
                rec[17] = char(0xFF);
                poke16(rec, 18, quint16(propOf(n->obj, QStringLiteral("cent_x"))
                                        .value(QStringLiteral("default")).toInt()));
                poke16(rec, 20, quint16(propOf(n->obj, QStringLiteral("cent_y"))
                                        .value(QStringLiteral("default")).toInt()));
                rec[22] = char(0xFF);
                rec[23] = char(0xFF);
                addList(24, imgList(QStringLiteral("normal_image")));
                addList(28, imgList(QStringLiteral("highlight_image")));
                break;
            case 9:                                    // Battery
                addList(16, imgList(QStringLiteral("image")));
                addList(20, imgList(QStringLiteral("charge_image")));
                break;
            case 10:                                   // Time
                putCStr(rec, 16, propOf(n->obj, QStringLiteral("source"))
                        .value(QStringLiteral("default")).toString(), 8);
                rec[24] = char(enumValue(propOf(n->obj, QStringLiteral("auto_cnt"))));
                rec[25] = char(0xFF);
                rec[26] = char(0xFF);
                rec[27] = char(0xFF);
                putCStr(rec, 28, propOf(n->obj, QStringLiteral("format"))
                        .value(QStringLiteral("default")).toString(), 16);
                poke32(rec, 44, colorAt(0));
                poke32(rec, 48, colorAt(1));
                arrU16(rec, 52, QStringLiteral("number"), 10);
                arrU16(rec, 72, QStringLiteral("delimiter"), 10);
                break;
            case 12:                                   // Text
                putCStr(rec, 16, propOf(n->obj, QStringLiteral("source"))
                        .value(QStringLiteral("default")).toString(), 8);
                putCStr(rec, 24, propOf(n->obj, QStringLiteral("code"))
                        .value(QStringLiteral("default")).toString(), 8);
                poke32(rec, 32, colorAt(0));
                poke32(rec, 36, colorAt(1));
                addList(40, textList(), true);
                break;
            case 15:                                   // number
                putCStr(rec, 16, propOf(n->obj, QStringLiteral("source"))
                        .value(QStringLiteral("default")).toString(), 8);
                putCStr(rec, 24, propOf(n->obj, QStringLiteral("format"))
                        .value(QStringLiteral("default")).toString(), 16);
                poke32(rec, 40, colorAt(0));
                poke32(rec, 44, colorAt(1));
                arrU16(rec, 48, QStringLiteral("number"), 10);
                arrU16(rec, 68, QStringLiteral("delimiter"), 10);
                arrU16(rec, 88, QStringLiteral("space"), 2);
                break;
            /* 【slider 和 vslider 记录布局完全一样】以前只写了 33(vslider)，
             * 28(slider) 掉进 default 什么都不做 —— step 不写、子元素指针留空、
             * 重定位表里也少一项。固件按空指针找不到滑块子元素，水平 slider
             * 在设备上就是画不出滑块。
             * 对照既有的 project.bin（那份 oled 工程）：
             *   slider  +16 = 01 ff ff ff   +20 = a4 01 00 00
             *   本版原来 +16 = 00 00 00 00   +20 = 00 00 00 00
             * vslider 两边本来就一致，正好说明是这个 case 漏了。 */
            case 28:                                   // slider（水平）
            case 33:                                   // vslider（垂直）
                rec[16] = char(propOf(n->obj, QStringLiteral("step"))
                               .value(QStringLiteral("default")).toInt());
                rec[17] = char(0xFF);       // u8 step 后面的对齐空洞
                rec[18] = char(0xFF);
                rec[19] = char(0xFF);
                n->ctrlPtrFields.append(20);
                break;
            default:
                break;
            }

            // ---- action 块 ----
            // 每个带 action 字段的控件**都**有一个块，哪怕一条事件都没配
            // （那就是个 u16 num=0，对齐到 4 字节）。三页 49/96/132 个控件
            // 各占 4 字节、重定位表各多 49/96/132 项，就是这么来的。
            static const QMap<int, int> kActionOff = {
                { 3, 16 }, { 4, 20 }, { 5, 20 }, { 7, 24 }, { 8, 32 }, { 9, 24 },
                { 10, 92 }, { 12, 44 }, { 13, 28 }, { 15, 92 },
            };
            if (kActionOff.contains(n->typeCode)) {
                QByteArray blk;
                QJsonArray values;
                for (const QJsonValue &v : propOf(n->obj, QStringLiteral("action"))
                                           .value(QStringLiteral("action")).toArray()) {
                    const QJsonObject o = v.toObject();
                    if (o.contains(QStringLiteral("values"))) {
                        values = o.value(QStringLiteral("values")).toArray();
                    }
                }
                put16(blk, quint16(values.size()));
                for (const QJsonValue &v : values) {
                    const QJsonObject o = v.toObject();
                    const QByteArray argv = o.value(QStringLiteral("args"))
                                            .toString().toLatin1();
                    QByteArray e;
                    put16(e, quint16(o.value(QStringLiteral("event")).toInt()));
                    put16(e, quint16(o.value(QStringLiteral("action")).toInt()));
                    const QString obj = o.value(QStringLiteral("object")).toString();
                    put32(e, enameIds.value(obj.toUpper(), 0xFFFFFFFFu));
                    e.append(char(argv.size()));
                    e += argv;
                    while (e.size() % 4) {
                        e.append(char(0));
                    }
                    blk += e;
                }
                n->dataFields.append(qMakePair(kActionOff.value(n->typeCode), blk));
                n->dataPatch.append(true);
            }
            n->rec = rec;
        }
    }

    // ---- 6. 每页：控件区 + 数据区 + 重定位表 ----
    QVector<QByteArray> pageBytes;
    QVector<QByteArray> pageTables;
    for (int pi = 0; pi < order.size(); ++pi) {
        // 窗口记录 28 字节
        QByteArray win(WINREC_SZ, '\0');
        win[0] = char(2);                              // page/ScenesScreen
        win[1] = char(roots[pi]->kids.size());
        win[2] = char(0);
        win[3] = char(0);          // 这里写 0，不是记录长度
        win[4] = char(0);
        win[5] = char(0xFF);
        win[6] = char(0xFF);
        win[7] = char(0xFF);
        poke32(win, 8, 0);
        poke32(win, 12, 0);
        poke32(win, 16, 10000);
        poke32(win, 20, 10000);
        poke32(win, 24, quint32(order[pi].isEmpty() ? 0 : order[pi].first()->recOff));

        QByteArray body = win;
        for (Node *n : order[pi]) {
            body += n->rec;
        }
        const int dataStart = body.size();

        // 数据区：组间倒序（控件 0 在最高地址），组内按字段偏移升序，4 字节对齐 0xFF 填充
        int total = 0;
        QVector<int> groupSize(order[pi].size(), 0);
        for (int i = 0; i < order[pi].size(); ++i) {
            Node *n = order[pi].at(i);
            int gs = 0;
            for (const QPair<int, QByteArray> &f : n->dataFields) {
                gs += align4(f.second.size());
            }
            groupSize[i] = gs;
            total += gs;
        }
        QByteArray data(total, char(0xFF));
        int pos = total;
        for (int i = 0; i < order[pi].size(); ++i) {
            Node *n = order[pi].at(i);
            pos -= groupSize[i];
            int p = pos;
            // 组内按字段偏移升序
            QVector<int> idx;
            for (int k = 0; k < n->dataFields.size(); ++k) {
                idx.append(k);
            }
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                return n->dataFields.at(a).first < n->dataFields.at(b).first;
            });
            for (int k : idx) {
                const QByteArray &blk = n->dataFields.at(k).second;
                data.replace(p, blk.size(), blk);
                if (n->dataPatch.at(k)) {
                    poke32(body, n->recOff + n->dataFields.at(k).first,
                           quint32(dataStart + p));
                }
                p += align4(blk.size());
            }
        }
        // 指向孩子的指针
        for (Node *n : order[pi]) {
            for (int off : n->ctrlPtrFields) {
                if (!n->kids.isEmpty()) {
                    poke32(body, n->recOff + off, quint32(n->kids.first()->recOff));
                }
            }
        }

        // 重定位表：控件**倒着**遍历，每个控件内部按字段偏移升序，最后补窗口记录里的指针
        QVector<quint16> reloc;
        for (int i = order[pi].size() - 1; i >= 0; --i) {
            Node *n = order[pi].at(i);
            QVector<int> offs;
            for (const QPair<int, QByteArray> &f : n->dataFields) {
                offs.append(f.first);
            }
            for (int o : n->ctrlPtrFields) {
                offs.append(o);
            }
            std::sort(offs.begin(), offs.end());
            for (int o : offs) {
                reloc.append(quint16(n->recOff + o));
            }
        }
        reloc.append(quint16(WINREC_SZ - 4));
        QByteArray table;
        for (quint16 v : reloc) {
            put16(table, v);
        }

        pageBytes.append(body + data);
        pageTables.append(table);
    }

    // ---- 7. 组装 ----
    const int npg = order.size();
    int cur = HEAD_SZ + WHEAD_SZ * npg;
    QVector<int> offsets(npg), lengths(npg), tptrs(npg);
    for (int i = 0; i < npg; ++i) {
        offsets[i] = cur;
        lengths[i] = pageBytes.at(i).size() + pageTables.at(i).size();
        tptrs[i] = cur + pageBytes.at(i).size();
        cur += lengths[i];
    }

    quint32 uiVersion = opt.uiVersion;
    if (uiVersion == 0) {
        QByteArray seed;
        for (QMap<QString, quint32>::const_iterator it = enameIds.constBegin();
             it != enameIds.constEnd(); ++it) {
            if (it.key() == QLatin1String("UI_VERSION")) {
                continue;
            }
            seed += it.key().toUtf8();
            put32(seed, it.value());
        }
        uiVersion = crc32(seed);
    }
    if (imported.contains(QStringLiteral("UI_VERSION"))) {
        uiVersion = imported.value(QStringLiteral("UI_VERSION"));
    }
    enameIds.insert(QStringLiteral("UI_VERSION"), uiVersion);

    QByteArray sty;
    put32(sty, uiVersion);
    /* 【这不是魔数，是生成时间戳】以前当常量抄了一个样本值。既有的两份
     * 产物这里分别是 0x6AA11723 / 0x6A85455D，换算成 Unix 时间正好等于各自
     * project.bin 的文件时间（精确到秒）。固件 struct ui_file_head 把前 16
     * 字节当 res[16] 不透明块，只读前 4 字节的 UI_VERSION，这一格不参与任何
     * 判断 —— 和 resver 一样属于"天然不可复现"的字段。 */
    put32(sty, quint32(QDateTime::currentSecsSinceEpoch()));
    put32(sty, 16);
    put32(sty, quint32(cur - HEAD_SZ - WHEAD_SZ * npg));
    sty.append(char(1));
    sty.append(char(npg));
    put16(sty, quint16(HEAD_SZ + WHEAD_SZ * npg - 16));
    sty.append(char(opt.rotate));
    sty.append(char(0xFF));
    sty.append(char(0xFF));
    sty.append(char(0xFF));
    for (int i = 0; i < npg; ++i) {
        QByteArray h14;
        put32(h14, quint32(offsets[i]));
        put32(h14, quint32(lengths[i]));
        put32(h14, quint32(tptrs[i]));
        put16(h14, quint16(pageTables.at(i).size()));
        sty += h14;
        put16(sty, crc16(pageBytes.at(i) + pageTables.at(i)));   // crc_data
        put16(sty, crc16(pageTables.at(i)));                     // crc_table
        put16(sty, crc16(h14));                                  // crc_head
    }
    for (int i = 0; i < npg; ++i) {
        sty += pageBytes.at(i);
        sty += pageTables.at(i);
    }
    out.sty = sty;

    // ---- 8. ename.h ----
    {
        QString s;
        s += QLatin1String("#ifndef UI_TOOL_ENAME\n#define UI_TOOL_ENAME\n\n");
        for (QMap<QString, quint32>::const_iterator it = enameIds.constBegin();
             it != enameIds.constEnd(); ++it) {
            s += QStringLiteral("#define %1 0X%2\n")
                 .arg(it.key()).arg(it.value(), 0, 16).toUpper()
                 .replace(QLatin1String("#DEFINE"), QLatin1String("#define"));
        }
        s += QLatin1String("\n#endif //UI_TOOL_ENAME_H\n");
        out.enameH = s.toUtf8();
    }

    // ---- 9. Resbuilder.xml ----
    {
        QString s;
        s += QLatin1String("<?xml version='2.0' encoding='UTF-8'?>\r\n<Resbuilder>\r\n");
        s += QLatin1String("\t<Items>\r\n");
        // 语言表和字体表配置文件里没有，只能写在程序里。ResBuilder 靠语言表
        // 生成 result.h、靠字体表挑字符串位图的字体，必须原样写出来。
        s += QLatin1String("\t\t<LanguageList>\r\n");
        for (int i = 0; i < 22; ++i) {
            /* 语言名在「功能设置」里可改；没给就用内置表 */
            s += QStringLiteral("\t\t\t<language_name LANG=\"%1\">%2</language_name>\r\n")
                 .arg(opt.langKeys.value(i, QString::fromUtf8(kLangKeys[i])),
                      opt.langNames.value(i, QString::fromUtf8(kLangNames[i])));
        }
        s += QLatin1String("\t\t</LanguageList>\r\n\t\t<Fonts>\r\n");
        for (int i = 0; i < 22; ++i) {
            /* 每种语言一套 LOGFONT ——「功能设置」里那张表就是它。
             * 没配就退回老规矩：宋体 -16、常规、不斜体不下划线。 */
            s += QStringLiteral(
                     "\t\t\t<font%1 lfQuality=\"0\" lfPitchAndFamily=\"2\" lfOrientation=\"0\""
                     " lfHeight=\"%2\" lfWeight=\"%3\" lfUnderline=\"%4\" lfOutPrecision=\"0\""
                     " lfItalic=\"%5\" lfClipPrecision=\"0\" lfFaceName=\"%6\" lfCharSet=\"134\""
                     " lfEscapement=\"0\" lfStrikeOut=\"%7\" lfWidth=\"0\"/>\r\n")
                 .arg(i, 2, 10, QLatin1Char('0'))
                 .arg(opt.fontHeights.value(i, -16))
                 .arg(opt.fontWeights.value(i, 400))
                 .arg(opt.fontUnderlines.value(i, 0))
                 .arg(opt.fontItalics.value(i, 0))
                 .arg(opt.fontFaces.value(
                          i, QString::fromUtf8("\xe5\xae\x8b\xe4\xbd\x93")))   // 宋体
                 .arg(opt.fontStrikeOuts.value(i, 0));
        }
        s += QLatin1String("\t\t</Fonts>\r\n");
        /* 【次序要紧】这四项在 <PageList> **之前**，其余设置项在之后。
         * Resbuilder.xml 里 <Items> 的子元素次序是：
         *   LanguageList / Fonts / endian / paneltype / picture_path /
         *   excel_path / PageList / language / bmp_transparent_color / ...
         * 顺序不一样，文件就不是逐字节相同的了。 */
        {
            const QString pp = opt.picturePath.isEmpty() ? QStringLiteral("NULL")
                                                         : opt.picturePath;
            s += QStringLiteral("\t\t<endian>%1</endian>\r\n").arg(opt.endian);
            s += QStringLiteral("\t\t<paneltype>%1</paneltype>\r\n").arg(opt.panelType);
            s += QStringLiteral("\t\t<picture_path>%1</picture_path>\r\n").arg(pp);
            /* 【excel_path 写相对工程目录的路径】既有那份写的是
             * ../tool/assets/i18n_128_64.xls，不是绝对路径 —— 工程整个
             * 挪个位置或者换台机器还能用。命令行 --excel 给的一般是绝对路径，
             * 这里折算回去。给的本来就是相对路径就原样保留。 */
            const QString xmlDir = opt.outDir.isEmpty() ? opt.projectDir : opt.outDir;
            QString xls = opt.excelPath;
            if (!xls.isEmpty() && QFileInfo(xls).isAbsolute() && !xmlDir.isEmpty()) {
                xls = QDir::fromNativeSeparators(QDir(xmlDir).relativeFilePath(xls));
            }
            s += QStringLiteral("\t\t<excel_path>%1</excel_path>\r\n").arg(xls);
        }
        s += QLatin1String("\t\t<PageList>\r\n");
        for (int pi = 0; pi < npg; ++pi) {
            s += QStringLiteral("\t\t\t<Page id=\"%1\">\r\n").arg(pi);
            s += QLatin1String("\t\t\t\t<ColorList>\r\n");
            for (const QString &c : pageColors[pi]) {
                s += QStringLiteral("\t\t\t\t\t<Color>%1</Color>\r\n").arg(c);
            }
            s += QLatin1String("\t\t\t\t</ColorList>\r\n\t\t\t\t<PictureList>\r\n");
            for (const QString &p : pagePicRefs[pi]) {
                QString abs = p;
                if (!QFileInfo(abs).isAbsolute() && !opt.projectDir.isEmpty()) {
                    abs = QDir(opt.projectDir).absoluteFilePath(p);
                }
                s += QStringLiteral("\t\t\t\t\t<Picture fmt=\"OSD1\">%1</Picture>\r\n")
                     .arg(QDir::toNativeSeparators(abs));
            }
            s += QLatin1String("\t\t\t\t</PictureList>\r\n\t\t\t\t<CellList>\r\n");
            for (const QString &c : pageCellRefs[pi]) {
                s += QStringLiteral("\t\t\t\t\t<Cell>%1</Cell>\r\n").arg(c);
            }
            s += QLatin1String("\t\t\t\t</CellList>\r\n\t\t\t</Page>\r\n");
        }
        s += QLatin1String("\t\t</PageList>\r\n");
        /* 【一项一行拼，不要挤在一个 QStringLiteral 里】占位符超过 9 个 .arg 就
         * 接不上了，而且 <percent>100%</percent> 里那个 % 混在带占位符的串里
         * 很容易被看成占位符。 */
        const QString resName = opt.res.isEmpty() ? QStringLiteral("result") : opt.res;
        // 写成大写十六进制（0x00FFFFFF）
        auto hex8 = [](quint32 v) {
            return QStringLiteral("0x%1").arg(
                QString::number(v, 16).toUpper().rightJustified(8, QLatin1Char('0')));
        };
        s += QStringLiteral("\t\t<language>0x%1</language>\r\n")
             .arg(opt.language, 8, 16, QLatin1Char('0'));
        s += QStringLiteral("\t\t<bmp_transparent_color>%1</bmp_transparent_color>\r\n")
             .arg(hex8(opt.bmpTransparentColor));
        s += QStringLiteral("\t\t<png_background_color>%1</png_background_color>\r\n")
             .arg(hex8(opt.pngBackgroundColor));
        s += QLatin1String("\t\t<spec_color_list>NULL</spec_color_list>\r\n");
        s += QLatin1String("\t\t<png2jpg_list>NULL</png2jpg_list>\r\n");
        s += QStringLiteral("\t\t<res>%1</res>\r\n").arg(resName);
        s += QStringLiteral("\t\t<resfilename>%1.bin</resfilename>\r\n").arg(resName);
        s += QStringLiteral("\t\t<headerfilename>%1.h</headerfilename>\r\n").arg(resName);
        s += QStringLiteral("\t\t<image_compress_method>%1</image_compress_method>\r\n")
             .arg(opt.imageCompress);
        s += QStringLiteral("\t\t<string_compress_method>%1</string_compress_method>\r\n")
             .arg(opt.stringCompress);
        s += QStringLiteral("\t\t<palette_type>%1</palette_type>\r\n").arg(opt.paletteType);
        s += QLatin1String("\t\t<percent>100%</percent>\r\n");
        s += QLatin1String("\t\t<excel_crc/>\r\n\t\t<excel_row/>\r\n");
        s += QStringLiteral("\t\t<rotate>%1</rotate>\r\n\t</Items>\r\n</Resbuilder>\r\n")
             .arg(opt.rotate);
        /* 【缩进是每级 8 个空格，不是 Tab】上面为了好读一直写的 \t，这里统一
         * 换成 8 空格（<Items> 前 8 个 0x20、
         * <LanguageList> 16 个、<language_name> 24 个，见工程目录里那份的原始
         * 字节）。Tab 和空格差一个字节都算产物不一致。
         * 内容里不会出现 Tab（都是路径、语言名、数字），整串替换是安全的。 */
        s.replace(QLatin1Char('\t'), QLatin1String("        "));

        /* 【UTF-8，不是本地代码页】以前这里写的是 toLocal8Bit()，那是错的。
         *
         * 现成的 Resbuilder.xml 里，excel_path 的"多"是 E5 A4 9A、lfFaceName
         * 的"宋体"是 E5 AE 8B E4 BD 93，都是 **UTF-8**；而本机 ANSI 代码页是
         * 936(GBK)，toLocal8Bit() 在这儿会写成 GBK。XML 头自己声明的也是
         * encoding='UTF-8'。
         *
         * 后果不是纸面问题：工程路径/字体名里有中文时，写 GBK 的话下游按
         * UTF-8 解就是乱码，找不到 xls 也挑不对字体。 */
        out.resbuilderXml = s.toUtf8();
    }

    // ---- 10. debug.txt ----
    {
        QString s;
        s += QLatin1String("Debug Information\r\n\r\n");
        s += QLatin1String("/===================>Head<===========================\\\r\n");
        s += QStringLiteral("|| version: %1\r\n").arg(uiVersion);
        s += QStringLiteral("|| date: %1\r\n")
             .arg(QDateTime::currentDateTime().toString(QStringLiteral("ddd M'月' d HH:mm:ss yyyy")));
        s += QLatin1String("|| ptr: 16\r\n");
        s += QStringLiteral("|| total size: %1\r\n").arg(cur - HEAD_SZ - WHEAD_SZ * npg);
        s += QLatin1String("\\====================================================/\r\n\r\n\r\n");
        s += QLatin1String("/===================>Project<===========================\\\r\n");
        s += QLatin1String("|| type: project\r\n");
        s += QStringLiteral("|| sub-widget: %1\r\n").arg(npg);
        s += QStringLiteral("|| prop size: %1\r\n").arg(HEAD_SZ + WHEAD_SZ * npg - 16);
        s += QStringLiteral("|| project rotate: %1  [(0-0),(1-90),(2-180),(3-270)]\r\n").arg(opt.rotate);
        s += QLatin1String("|| offset prop_len  t_ptr  t_size   crc       crc       crc\r\n");
        for (int i = 0; i < npg; ++i) {
            const QByteArray page = pageBytes.at(i) + pageTables.at(i);
            QByteArray h14;
            put32(h14, quint32(offsets[i]));
            put32(h14, quint32(lengths[i]));
            put32(h14, quint32(tptrs[i]));
            put16(h14, quint16(pageTables.at(i).size()));
            s += QStringLiteral("||   %1      %2      %3      %4      %5      %6      %7\r\n")
                 .arg(offsets[i], 0, 16).arg(lengths[i], 0, 16).arg(tptrs[i], 0, 16)
                 .arg(pageTables.at(i).size(), 0, 16)
                 .arg(crc16(page), 0, 16).arg(crc16(pageTables.at(i)), 0, 16)
                 .arg(crc16(h14), 0, 16);
        }
        s += QLatin1String("\\====================================================/\r\n\r\n\r\n");
        // 后面是整个 .sty 的十六进制转储，每行 16 字节
        for (int i = 0; i < sty.size(); i += 16) {
            QString line;
            for (int k = 0; k < 16 && i + k < sty.size(); ++k) {
                line += QStringLiteral("%1  ").arg(quint8(sty.at(i + k)), 2, 16,
                                                   QLatin1Char('0')).toUpper();
            }
            s += line + QLatin1String("\r\n");
        }
        out.debugTxt = s.toLocal8Bit();
    }

    qDeleteAll(roots);
    out.ok = true;
    return out;
}

} // namespace sty
