#include "EditorOps.h"
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

bool acceptsChild(const UiNode *n)
{
    return isLayout(n);
}

QString childKeyFor(const UiNode *parent)
{
    if (isPage(parent)) {
        return QStringLiteral("layer");
    }
    if (parent && parent->cls == QLatin1String("NewList")) {
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
