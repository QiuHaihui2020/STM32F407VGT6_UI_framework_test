/*
 * StyFile.h —— JL.sty（即 project.bin）窗口/控件布局文件的读写
 *
 * 【格式来源】三方互证，已用 compat/sty_dump.py 在真实文件上跑通：
 *   ① 现成的 .sty 样本文件
 *   ② 与之配套的结构转储  .../project/debug.txt
 *   ③ ename.h 里的 ID 常量 —— 控件头 id 字段逐条对得上宏名
 *
 * 【已验证的事实】（tools/JL/JL.sty, 24498 B, 3 页）
 *   24 + 20*window_num + total_size == 文件大小
 *   每页  offset + length == table_ptr + table_size == 下一页 offset（末页为文件尾）
 *   控件头里 (id>>16)&0x3F == head.type，低 16 位是名字哈希，
 *   bit22/23 是页号（页0=0x00xxxx 页1=0x40xxxx 页2=0x80xxxx）
 *   控件负载长度与 control.h 的结构体逐字节吻合，例如 Text = 16+8+8+4+4+4+4 = 48
 *
 * 【尚未解决】控件头 id 低 16 位那个哈希的算法。
 *   不影响工具链自洽：ename.h 由本工具一并生成，固件包含的就是新表。
 */
#ifndef STYFILE_H
#define STYFILE_H

#include <QByteArray>
#include <QString>
#include <QVector>

/** 文件头，24 字节，对应固件 struct ui_file_head。 */
struct StyHead {
    quint32 uiVersion  = 0;           ///< 与 ename.h 的 UI_VERSION 宏一致
    /** 生成时间戳（Unix 秒）。**不是** magic —— 每生成一次就变一次。
     *  固件把前 16 字节当 res[16] 不透明块，只读前 4 字节的 UI_VERSION，
     *  这一格它根本不看。 */
    quint32 genTime    = 0;
    quint32 hdrPtr     = 16;          ///< 固定 16
    quint32 totalSize  = 0;           ///< 文件大小 - (24 + 20*windowNum)
    quint8  type       = 1;
    quint8  windowNum  = 0;
    quint16 propLen    = 0;
    quint8  rotate     = 0;           ///< 0/1/2/3 -> 0/90/180/270
    quint8  rev[3]     = { 0xFF, 0xFF, 0xFF };
};

/** 页表项，20 字节，对应固件 struct window_head。 */
struct StyWindowHead {
    quint32 offset    = 0;
    quint32 length    = 0;
    quint32 tablePtr  = 0;
    quint16 tableSize = 0;
    quint16 crc[3]    = { 0, 0, 0 };
};

/** 控件头，16 字节，对应固件 struct ui_ctrl_info_head。 */
struct StyCtrlHead {
    quint8  type     = 0;
    quint8  ctrlNum  = 0;
    quint8  cssNum   = 0;
    quint8  len      = 0;     ///< 整条记录长度（含本 16 字节头）
    quint8  page     = 0;
    quint8  rev[3]   = { 0xFF, 0xFF, 0xFF };
    qint32  id       = 0;
    quint32 css      = 0;     ///< 文件内偏移，不是内存指针
};

/** 一条控件记录 = 头 + 类型相关负载。 */
struct StyControl {
    StyCtrlHead head;
    QByteArray  payload;      ///< len - 16 字节
    quint32     fileOffset = 0;

    int  idType() const { return (head.id >> 16) & 0x3F; }
    int  idHash() const { return head.id & 0xFFFF; }
    int  idPage() const { return (head.id >> 22) & 0x3; }
};

/** 一页。 */
struct StyWindow {
    StyWindowHead      head;
    QByteArray         windowRecord;   ///< 页块开头、第一条控件头之前的那段（实测 28 B）
    QVector<StyControl> controls;
    QByteArray         cssBlob;        ///< 控件区末尾到 tablePtr 之间的 css/属性数据
    QByteArray         indexTable;     ///< [tablePtr, tablePtr+tableSize)
};

/** 控件类型码（= 控件头 type，也 = id 的 bit16..21）。
 *  取值由 ename.h 的 id 与工程 json 的 "-type" 对照得出。 */
namespace StyCtrlType {
enum {
    Window       = 2,
    Layout       = 3,
    Layer        = 4,
    ListOrGrid   = 5,
    Progress     = 7,
    ImageList    = 8,
    Battery      = 9,
    TimeOrWatch  = 10,
    Text         = 12,
    Number       = 15,
};
const char *name(int t);
}

class StyFile
{
public:
    /** 读入并完整解析。失败时 err 里是原因。 */
    bool load(const QString &path, QString *err = nullptr);

    /** 按当前内容重新计算所有偏移/长度后写出。
     *  对未改动的文件做 load->save 应得到字节相同的结果（自检见 verifyRoundTrip）。 */
    bool save(const QString &path, QString *err = nullptr) const;

    /** 读回来再写出去，和原文件逐字节比较。用来证明格式理解没有漏洞。 */
    static bool verifyRoundTrip(const QString &path, QString *report);

    const StyHead &head() const { return m_head; }
    StyHead &head() { return m_head; }
    const QVector<StyWindow> &windows() const { return m_windows; }
    QVector<StyWindow> &windows() { return m_windows; }

    /** 人类可读的结构摘要，等价于 debug.txt。 */
    QString describe() const;

private:
    QByteArray serialize() const;

    StyHead m_head;
    QVector<StyWindow> m_windows;
};

#endif // STYFILE_H
