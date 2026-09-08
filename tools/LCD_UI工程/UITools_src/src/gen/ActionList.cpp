#include "BaseDialog.h"
#include "ActionList.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

/// enum 数组形如 [{"KEY_OK":13}, …]，取出全部键名
QStringList enumKeys(const QJsonArray &en)
{
    QStringList out;
    for (const QJsonValue &v : en) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            out.append(it.key());
        }
    }
    return out;
}

} // namespace

ActionList::ActionList(QWidget *parent)
    : BaseDialog(parent)
{
    setWindowTitle(QStringLiteral("事件动作"));
    resize(680, 380);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        QStringList{ tr("事件"), tr("动作"), tr("对象"), tr("参数") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(24);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    localizeButtonBox(box);
    auto *root = new QVBoxLayout(this);
    root->addWidget(m_table, 1);
    root->addWidget(box);

    connect(m_table, &QWidget::customContextMenuRequested,
            this, &ActionList::onCustomContextMenu);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ActionList::~ActionList() = default;

bool ActionList::setActionPropertyChecked(const QJsonValue &v, QString *error)
{
    /* ★ 原厂的类型校验（"格式错误" + "格式错误,必须是数组类型"）。
     * 事件属性在工程 json 里必须是数组；写成对象或标量说明这个控件模板坏了，
     * 硬着头皮往下读只会得到一张空表，用户还以为是自己没配过事件。 */
    if (!v.isArray()) {
        if (error) {
            *error = QStringLiteral("格式错误,必须是数组类型");
        }
        return false;
    }
    setActionProperty(v.toArray());
    return true;
}

void ActionList::setActionProperty(const QJsonArray &arr)
{

    m_template = QJsonArray();
    m_eventEnum = QJsonArray();
    m_actionEnum = QJsonArray();
    QJsonArray values;

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("values"))) {
            values = o.value(QStringLiteral("values")).toArray();
            continue;                      // values 单独拿出来，其余是模板
        }
        m_template.append(o);
        const QString n = o.value(QStringLiteral("-name")).toString();
        if (n == QLatin1String("event")) {
            m_eventEnum = o.value(QStringLiteral("enum")).toArray();
        } else if (n == QLatin1String("action")) {
            m_actionEnum = o.value(QStringLiteral("enum")).toArray();
        }
    }

    m_table->setRowCount(0);
    for (const QJsonValue &v : values) {
        addRow(v.toObject());
    }
}

void ActionList::addRow(const QJsonObject &v)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    fillRow(r, v);
}

void ActionList::fillRow(int row, const QJsonObject &v)
{
    auto *ev = new QComboBox(m_table);
    ev->addItems(enumKeys(m_eventEnum));
    const int ei = ev->findText(v.value(QStringLiteral("event_name")).toString());
    if (ei >= 0) {
        ev->setCurrentIndex(ei);
    }
    m_table->setCellWidget(row, 0, ev);

    auto *ac = new QComboBox(m_table);
    ac->addItems(enumKeys(m_actionEnum));
    const int ai = ac->findText(v.value(QStringLiteral("action_name")).toString());
    if (ai >= 0) {
        ac->setCurrentIndex(ai);
    }
    m_table->setCellWidget(row, 1, ac);

    auto *ob = new QComboBox(m_table);
    ob->setEditable(true);
    ob->addItem(QString());
    ob->addItems(m_objects);
    ob->setCurrentText(v.value(QStringLiteral("object")).toString());
    m_table->setCellWidget(row, 2, ob);

    auto *ar = new QLineEdit(v.value(QStringLiteral("args")).toString(), m_table);
    m_table->setCellWidget(row, 3, ar);
}

QJsonArray ActionList::actionProperty() const
{
    QJsonArray values;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const auto *ev = qobject_cast<QComboBox *>(m_table->cellWidget(r, 0));
        const auto *ac = qobject_cast<QComboBox *>(m_table->cellWidget(r, 1));
        const auto *ob = qobject_cast<QComboBox *>(m_table->cellWidget(r, 2));
        const auto *ar = qobject_cast<QLineEdit *>(m_table->cellWidget(r, 3));
        if (!ev || !ac || !ob || !ar) {
            continue;
        }
        QJsonObject o;
        // 名字和数值都存：名字给界面看，数值是 .sty 里真正落盘的
        o.insert(QStringLiteral("event_name"), ev->currentText());
        o.insert(QStringLiteral("action_name"), ac->currentText());
        int evVal = 0, acVal = 0;
        for (const QJsonValue &x : m_eventEnum) {
            const QJsonObject eo = x.toObject();
            if (eo.contains(ev->currentText())) {
                evVal = eo.value(ev->currentText()).toInt();
            }
        }
        for (const QJsonValue &x : m_actionEnum) {
            const QJsonObject ao = x.toObject();
            if (ao.contains(ac->currentText())) {
                acVal = ao.value(ac->currentText()).toInt();
            }
        }
        o.insert(QStringLiteral("event"), evVal);
        o.insert(QStringLiteral("action"), acVal);
        o.insert(QStringLiteral("object"), ob->currentText());
        o.insert(QStringLiteral("args"), ar->text());
        values.append(o);
    }
    QJsonArray out = m_template;
    QJsonObject holder;
    holder.insert(QStringLiteral("values"), values);
    out.append(holder);
    return out;
}

void ActionList::onCustomContextMenu(QPoint pos)
{
    /* 六项逐字来自 ui-tools.exe：插入行 / 删除当前行 / 上移一行 / 移到顶部 /
     * 下移一行 / 移到底部。之前是我自己起的"添加/删除/上移/下移"。 */
    QMenu menu(this);
    QAction *add = menu.addAction(QIcon(QStringLiteral(":/icon/icons/act_add.png")),
                                  QStringLiteral("插入行"));
    QAction *del = menu.addAction(QIcon(QStringLiteral(":/icon/icons/act_del.png")),
                                  QStringLiteral("删除当前行"));
    menu.addSeparator();
    QAction *up = menu.addAction(QIcon(QStringLiteral(":/icon/icons/act_up.png")),
                                 QStringLiteral("上移一行"));
    QAction *top = menu.addAction(QStringLiteral("移到顶部"));
    QAction *down = menu.addAction(QIcon(QStringLiteral(":/icon/icons/act_down.png")),
                                   QStringLiteral("下移一行"));
    QAction *bottom = menu.addAction(QStringLiteral("移到底部"));

    const int row = m_table->rowAt(pos.y());
    del->setEnabled(row >= 0);
    up->setEnabled(row > 0);
    top->setEnabled(row > 0);
    down->setEnabled(row >= 0 && row + 1 < m_table->rowCount());
    bottom->setEnabled(row >= 0 && row + 1 < m_table->rowCount());

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }
    if (chosen == add) {
        addRow(QJsonObject());
    } else if (chosen == del) {
        m_table->removeRow(row);
    } else if (chosen == up || chosen == down || chosen == top || chosen == bottom) {
        // QTableWidget 不支持整行带 cellWidget 的搬移，只能取值重建两行
        int other = row;
        if (chosen == up) {
            other = row - 1;
        } else if (chosen == down) {
            other = row + 1;
        } else if (chosen == top) {
            other = 0;
        } else {
            other = m_table->rowCount() - 1;
        }
        const QJsonArray all = actionProperty();
        QJsonArray values;
        for (const QJsonValue &v : all) {
            const QJsonObject o = v.toObject();
            if (o.contains(QStringLiteral("values"))) {
                values = o.value(QStringLiteral("values")).toArray();
            }
        }
        if (row >= 0 && other >= 0 && row < values.size() && other < values.size()) {
            /* 上下移是**交换**，移到顶/底是**搬移**（把这一行抽出来插到端点，
             * 中间那些行整体顺移）—— 两种语义不一样，别混用。 */
            if (chosen == up || chosen == down) {
                const QJsonValue a = values.at(row);
                values.replace(row, values.at(other));
                values.replace(other, a);
            } else {
                const QJsonValue a = values.at(row);
                values.removeAt(row);
                values.insert(other, a);
            }
        }
        m_table->setRowCount(0);
        for (const QJsonValue &v : values) {
            addRow(v.toObject());
        }
        m_table->setCurrentCell(other, 0);
    }
}
