/* EventActionDialog.h ——
 *
 * 用途：编辑控件的事件动作（json 里 -type == "action" 的属性）。
 * 该属性的结构是"模板 + 实例"：
 *     property[-name=="action"].action = [
 *         { -name:"event",  -type:"enum",     enum:[{KEY_OK:13}, …] },   <- 模板
 *         { -name:"action", -type:"enum",     enum:[{SHOW:0},{HIDE:1}] },
 *         { -name:"object", -type:"text-str", … },
 *         { -name:"args",   -type:"text-str", … },
 *         { values: [ … ] }                                             <- 实例
 *     ]
 * 本对话框只改最后那个 values 数组，模板原样留着。
 *
 * 落到 .sty 里就是 struct element_event_action：
 *     u16 num; struct event_action { u16 event; u16 action; int id; u8 argc;
 *                                    char argv[]; } [num];   // 每条 4 字节对齐
 * 注意：本工程 277 个控件的 action 全是 num=0，**没有真实样本可比对**，
 * 只能保证结构对。
 */
#ifndef ACTIONLIST_H
#define ACTIONLIST_H

#include "BaseDialog.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QPoint>
#include <QStringList>

class QTableWidget;

class EventActionDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit EventActionDialog(QWidget *parent = nullptr);
    ~EventActionDialog() override;

    /// 属性里的整个 action 数组（模板 + values）
    void       setActionProperty(const QJsonArray &arr);
    /** 带类型校验的版本。不是数组就返回 false，error 是"格式不对：这里要的是数组"。 */
    bool       setActionPropertyChecked(const QJsonValue &v, QString *error);
    /// 改过之后的 action 数组，直接写回 property
    QJsonArray actionProperty() const;

    /// "对象"列的候选：本工程所有控件的 ename
    void setObjectNames(const QStringList &names) { m_objects = names; }

public slots:
    void onCustomContextMenu(QPoint pos);

private:
    void addRow(const QJsonObject &v);
    void fillRow(int row, const QJsonObject &v);

    QTableWidget *m_table = nullptr;
    QJsonArray    m_template;      ///< 除 values 外的模板项，原样带回
    QJsonArray    m_eventEnum;
    QJsonArray    m_actionEnum;
    QStringList   m_objects;
};

#endif // ACTIONLIST_H
