#include "EditorOps.h"
#include "ControlLibrary.h"
#include "ProjectModel.h"

#include <QApplication>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QWidget>

namespace {

/** 进程内剪贴板。存 json 副本，源节点被删了也不影响。 */
QJsonObject g_clip;
bool        g_clipValid = false;
/** 剪贴板里那个节点原来挂在什么键下（widget / layout / listwidget）。 */
QString     g_clipKey;

QString g_customDir;

/** 当前工程模型，reassignEnames() 用它把去重范围扩到整个工程。 */
ProjectModel *g_model = nullptr;
/** control.json 模板库 —— 建节点的两条路要共用同一份模板。 */
const ControlLibrary *g_lib = nullptr;

bool    g_silent = false;
QString g_lastMsg;

} // namespace

namespace EditorOps {

bool isPage(const UiNode *n)
{
    return n && n->parent == nullptr;
}

bool isLayer(const UiNode *n)
{
    return n && n->cls == QLatin1String("NewLayer");
}

bool isLayout(const UiNode *n)
{
    return n && n->cls == QLatin1String("NewLayout");
}

bool isFrame(const UiNode *n)
{
    return n && n->cls == QLatin1String("NewFrame");
}

bool isList(const UiNode *n)
{
    return n && n->cls == QLatin1String("NewList");
}

bool isGrid(const UiNode *n)
{
    return n && n->cls == QLatin1String("NewGrid");
}

bool acceptsLayout(const UiNode *n)
{
    return isLayer(n) || isLayout(n) || isList(n) || isGrid(n);
}

bool acceptsWidget(const UiNode *n)
{
    /* 只有布局。列表的孩子在原厂工程里清一色是 NewLayout（165 个），
     * 塞个裸控件进去，生成出来是原厂从没产出过的形状。 */
    return isLayout(n);
}

bool acceptsChild(const UiNode *n)
{
    /* 粘贴仍然只认布局 —— 原厂那三条提示语写死了"请选择一个<布局>对像"。 */
    return isLayout(n);
}

UiNode *hostForNewControl(UiNode *sel)
{
    if (!sel || isLayer(sel)) {
        return nullptr;                      // 调用方去弹"请选择一个布局…"
    }
    if (isFrame(sel) || isList(sel) || isGrid(sel)) {
        return sel->parent;                  // 加到兄弟位置，不钻进去
    }
    return sel;                              // 布局：加到自己里
}

UiNode *hostForNewLayout(UiNode *sel, bool *needTip)
{
    if (needTip) {
        *needTip = false;
    }
    if (!sel) {
        if (needTip) {
            *needTip = true;                 // 只有这一种情况原厂会弹提示
        }
        return nullptr;
    }
    if (isLayout(sel) || isLayer(sel)) {
        return sel;
    }
    if (isList(sel) || isGrid(sel)) {
        /* ★ 这一条和原厂不一样：列表原厂给的是 sel->parent（布局落到列表
         * **旁边**），想加行得去列表的右键菜单「添加行」；表格原厂连判都不判，
         * 静默什么也不做，根本没有加项的入口。选中容器点「新建布局」却建到
         * 别处去（或者干脆没反应），实在不像话，本版两种都改成建进去 ——
         * 列表的行、表格的项本来就是布局，键还是 listwidget，
         * 产物形状和原厂那 165 行一致。 */
        return sel;
    }
    if (isFrame(sel)) {
        return sel->parent;
    }
    return nullptr;                          // 原厂在这儿是静默返回
}

QRect cellRectFor(const UiNode *container, int index)
{
    if (!container) {
        return QRect();
    }
    const QRect box = container->rect;
    const int i = qMax(0, index);
    if (isList(container)) {
        /* orientation 缺省按垂直 —— 和 Forms.cpp 的 listIsVertical() 同一条规则 */
        const bool vert = container->extraValue(QStringLiteral("orientation"))
                          .toString() != QLatin1String("Horizontal");
        const int size = qMax(1, container->extraValue(
                                     QStringLiteral("sizehw")).toInt(16));
        const int space = container->extraValue(QStringLiteral("space")).toInt(0);
        const int step = size + space;
        if (vert) {
            return QRect(0, i * step, qMax(1, box.width()), size);
        }
        return QRect(i * step, 0, size, qMax(1, box.height()));
    }
    if (isGrid(container)) {
        const int cw = qMax(1, container->extraValue(QStringLiteral("cell_w")).toInt(16));
        const int ch = qMax(1, container->extraValue(QStringLiteral("cell_h")).toInt(16));
        const int space = container->extraValue(QStringLiteral("space")).toInt(0);
        const int cols = qMax(1, container->extraValue(QStringLiteral("cols")).toInt(1));
        return QRect((i % cols) * (cw + space), (i / cols) * (ch + space), cw, ch);
    }
    return QRect();
}

QString childKeyFor(const UiNode *parent)
{
    if (isPage(parent)) {
        return QStringLiteral("layer");
    }
    if (parent && parent->cls == QLatin1String("NewList")) {
        return QStringLiteral("listwidget");
    }
    if (parent && parent->cls == QLatin1String("NewGrid")) {
        /* 表格的项也走 listwidget，不是 "GridWidget" —— 那个名字在
         * ui-tools.exe 里是个死字符串，没有任何代码读它（见 EditorOps.h）。 */
        return QStringLiteral("listwidget");
    }
    return QStringLiteral("layout");
}

void setSilent(bool on)
{
    g_silent = on;
}

bool silent()
{
    return g_silent;
}

QString lastMessage()
{
    return g_lastMsg;
}

void clearLastMessage()
{
    g_lastMsg.clear();
}

void tip(QWidget *parent, const QString &text)
{
    g_lastMsg = text;
    if (g_silent) {
        return;
    }
    QMessageBox::information(parent, QStringLiteral("提示"), text);
}

void warn(QWidget *parent, const QString &text)
{
    g_lastMsg = text;
    if (g_silent) {
        return;
    }
    QMessageBox::warning(parent, QStringLiteral("警告"), text);
}

bool confirmDelete(QWidget *parent, const QString &what)
{
    if (g_silent) {
        g_lastMsg = QStringLiteral("你真的要删除当前%1吗?").arg(what);
        return true;
    }
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("删除提示"));
    box.setText(QStringLiteral("你真的要删除当前%1吗?删除之后不可以撤消,请选择<删除>删除.")
                .arg(what));
    QAbstractButton *del = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    /* 默认焦点给"取消"：删除不可撤消，回车不该顺手把东西删了 */
    box.setDefaultButton(qobject_cast<QPushButton *>(box.buttons().last()));
    box.exec();
    return box.clickedButton() == del;
}

bool moveZ(UiNode *n, ZMove how)
{
    if (!n || !n->parent) {
        return false;
    }
    QVector<QPair<QString, UiNode *>> &sib = n->parent->children;
    int i = -1;
    for (int k = 0; k < sib.size(); ++k) {
        if (sib.at(k).second == n) {
            i = k;
            break;
        }
    }
    if (i < 0) {
        return false;
    }
    int to = i;
    switch (how) {
    case ZTop:    to = sib.size() - 1; break;
    case ZUp:     to = i + 1;          break;
    case ZDown:   to = i - 1;          break;
    case ZBottom: to = 0;              break;
    }
    if (to == i || to < 0 || to >= sib.size()) {
        return false;
    }
    sib.move(i, to);
    /* 顺序变了就得整棵重写：nodeToJson 只在节点标脏时才按 children 重建
     * 数组，父节点不标脏的话新次序根本落不到文件里。 */
    n->parent->markDirty();
    return true;
}

void copyToClip(const UiNode *n)
{
    if (!n) {
        return;
    }
    g_clip = ProjectModel::toJsonObject(n);
    g_clipKey = QStringLiteral("widget");
    if (n->parent) {
        for (const auto &c : n->parent->children) {
            if (c.second == n) {
                g_clipKey = c.first;
                break;
            }
        }
    }
    g_clipValid = true;
}

bool clipEmpty()
{
    return !g_clipValid;
}

QString uniqueName(const UiNode *parent, const QString &base)
{
    if (!parent) {
        return base;
    }
    QSet<QString> used;
    for (const auto &c : parent->children) {
        used.insert(c.second->name);
    }
    if (!used.contains(base)) {
        return base;
    }
    for (int i = 1; ; ++i) {
        const QString cand = QStringLiteral("%1_%2").arg(base).arg(i);
        if (!used.contains(cand)) {
            return cand;
        }
    }
}

void setLibrary(const ControlLibrary *lib)
{
    g_lib = lib;
}

QJsonObject templateFor(const QString &type)
{
    if (!g_lib) {
        return QJsonObject();
    }
    const ControlTemplate *t = g_lib->byType(type);
    return t ? t->raw : QJsonObject();
}

UiNode *makeFromTemplate(UiNode *parent, const QString &type,
                         const QString &caption)
{
    const QJsonObject tpl = templateFor(type);
    UiNode *n = nullptr;
    if (!tpl.isEmpty()) {
        n = ProjectModel::fromJsonObject(tpl, parent);
    } else {
        /* 模板库没加载（--ops-test 之外基本不会发生）。退到空壳，
         * 但至少把类型填对，别让调用方拿到 nullptr。 */
        QJsonObject o;
        o.insert(QStringLiteral("-class"), QStringLiteral("NewLayout"));
        o.insert(QStringLiteral("-type"), type);
        o.insert(QStringLiteral("-name"), type);
        n = ProjectModel::fromJsonObject(o, parent);
    }
    n->forEach([](UiNode *x) { x->markDirty(); return true; });
    if (!caption.isEmpty()) {
        n->caption = caption;
    }
    n->name = uniqueName(parent, defaultNodeName(n->caption));

    /* 唯一 ID 号：空着的话生成资源时这个控件拿不到 ename.h 里的宏 */
    if (UiProperty *idp = n->findProp(QStringLiteral("id"))) {
        if (idp->ename.isEmpty() && g_model) {
            idp->ename = g_model->uniqueEname();
            idp->dirty = true;
            n->markDirty();
        }
    }

    /* 进列表/表格的，尺寸就是那一格 */
    const QRect cell = cellRectFor(parent, parent ? parent->children.size() : 0);
    if (cell.isValid()) {
        n->rect = cell;
        n->setRectOf(0, cell);
    }
    return n;
}

QString defaultNodeName(const QString &caption)
{
    const QString base = caption.isEmpty() ? QStringLiteral("控件") : caption;
    if (!g_model) {
        return base;
    }
    return QStringLiteral("%1_%2").arg(base).arg(g_model->nextNodeSeq());
}

void setCustomWidgetDir(const QString &dir)
{
    g_customDir = dir;
}

QString customWidgetDir()
{
    return g_customDir;
}

UiNode *pasteInto(UiNode *parent)
{
    if (!g_clipValid || !acceptsChild(parent)) {
        return nullptr;
    }
    UiNode *n = ProjectModel::fromJsonObject(g_clip, parent);
    n->forEach([](UiNode *x) { x->markDirty(); return true; });
    n->name = uniqueName(parent, n->name);
    /* 键按**目标**父节点定，不是按源节点原来挂在哪儿 —— 从列表的
     * listwidget 里复制一个布局，贴到普通布局下面时键要变成 layout。 */
    Q_UNUSED(g_clipKey)
    /* 【ID 号必须重分配】剪贴板里连 ename 一起带过来了，直接贴上去
     * ename.h 会出现两个同名宏。 */
    reassignEnames(n, parent);
    parent->children.append(qMakePair(childKeyFor(parent), n));
    parent->markDirty();
    return n;
}

void setModel(ProjectModel *m)
{
    g_model = m;
}

void reassignEnames(UiNode *sub, UiNode *parent)
{
    if (!sub) {
        return;
    }
    /* 收集**整个工程**已经用掉的 ename。
     * 【不能只顺着 parent 往上爬】爬到页节点就到头了（页的 parent 是 nullptr），
     * 跨页的名字扫不到 —— 在页 0 的列表里加行，分到的 BaseForm 会和页 1、
     * 页 3 里已有的撞车。有模型就用模型，没有（单元测试之类）再退回爬树。 */
    QSet<QString> used;
    auto collect = [&used, sub](UiNode *x) {
        /* sub 这棵子树待会儿要重分配，它现在的名字不算"已用" */
        for (UiNode *a = x; a; a = a->parent) {
            if (a == sub) {
                return true;
            }
        }
        for (const UiProperty &p : x->props) {
            if (p.name == QLatin1String("id") && !p.ename.isEmpty()) {
                used.insert(p.ename.toUpper());
            }
        }
        return true;
    };
    if (g_model) {
        for (UiNode *pg : g_model->pages()) {
            pg->forEach(collect);
        }
    } else {
        UiNode *root = parent ? parent : sub;
        while (root->parent) {
            root = root->parent;
        }
        root->forEach(collect);
    }

    sub->forEach([&used](UiNode *x) {
        for (UiProperty &p : x->props) {
            if (p.name != QLatin1String("id")) {
                continue;
            }
            const QString base = QStringLiteral("BaseForm");
            QString cand = base;
            for (int i = 1; used.contains(cand.toUpper()); ++i) {
                cand = QStringLiteral("%1_%2").arg(base).arg(i);
            }
            p.ename = cand;
            p.dirty = true;
            used.insert(cand.toUpper());
            x->markDirty();
            break;
        }
        return true;
    });
}

void commitPendingEdit()
{
    /* clearFocus() 会让控件收到 FocusOut：QLineEdit 借此发 editingFinished，
     * QSpinBox / 可编辑的 QComboBox 也在这时 interpretText()。
     * 一句话覆盖所有编辑器，不用挨个去认类型。 */
    if (QWidget *w = QApplication::focusWidget()) {
        w->clearFocus();
    }
}

} // namespace EditorOps
