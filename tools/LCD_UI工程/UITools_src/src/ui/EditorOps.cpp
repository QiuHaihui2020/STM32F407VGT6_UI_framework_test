#include "EditorOps.h"
#include "ProjectModel.h"

#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>

namespace {

/** 进程内剪贴板。存 json 副本，源节点被删了也不影响。 */
QJsonObject g_clip;
bool        g_clipValid = false;
/** 剪贴板里那个节点原来挂在什么键下（widget / layout / listwidget）。 */
QString     g_clipKey;

QString g_customDir;

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
    parent->children.append(qMakePair(childKeyFor(parent), n));
    parent->markDirty();
    return n;
}

} // namespace EditorOps
