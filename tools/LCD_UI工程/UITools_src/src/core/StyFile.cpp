#include "StyFile.h"

#include <QFile>
#include <QSaveFile>
#include <QDataStream>
#include <QStringList>

static const int kHeadSize  = 24;
static const int kWHeadSize = 20;
static const int kCHeadSize = 16;

const char *StyCtrlType::name(int t)
{
    switch (t) {
    case Window:      return "Window";
    case Layout:      return "NewLayout";
    case Layer:       return "NewLayer";
    case ListOrGrid:  return "NewList/NewGrid";
    case Progress:    return "Progress";
    case ImageList:   return "ImageList";
    case Battery:     return "Battery";
    case TimeOrWatch: return "Time/Watch";
    case Text:        return "Text";
    case Number:      return "Number";
    default:          return "?";
    }
}

/* ---- 小工具：小端读写 ------------------------------------------------ */
static quint8  rd8(const QByteArray &b, int o)  { return quint8(b.at(o)); }
static quint16 rd16(const QByteArray &b, int o)
{
    return quint16(quint8(b.at(o)) | (quint8(b.at(o + 1)) << 8));
}
static quint32 rd32(const QByteArray &b, int o)
{
    return quint32(quint8(b.at(o))) | (quint32(quint8(b.at(o + 1))) << 8)
           | (quint32(quint8(b.at(o + 2))) << 16) | (quint32(quint8(b.at(o + 3))) << 24);
}
static void wr8(QByteArray &b, quint8 v)   { b.append(char(v)); }
static void wr16(QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); }
static void wr32(QByteArray &b, quint32 v)
{
    for (int i = 0; i < 4; ++i) {
        b.append(char((v >> (8 * i)) & 0xFF));
    }
}

/* 一条控件头是否自洽。三重校验：长度合法 / rev 三字节恒为 FF /
 * (id>>16)&0x3F 必须等于 head.type —— 最后这条是最强的约束，
 * 靠它才能在页块开头准确找到第一条控件记录。 */
static bool validCtrlHead(const QByteArray &raw, int p)
{
    if (p < 0 || p + kCHeadSize > raw.size()) {
        return false;
    }
    const quint8 t  = rd8(raw, p);
    const quint8 ln = rd8(raw, p + 3);
    if (ln < kCHeadSize || ln > 208 || p + ln > raw.size()) {
        return false;
    }
    if (t == 0 || t > 63) {
        return false;
    }
    if (rd8(raw, p + 5) != 0xFF || rd8(raw, p + 6) != 0xFF || rd8(raw, p + 7) != 0xFF) {
        return false;
    }
    const qint32 id = qint32(rd32(raw, p + 8));
    return id > 0 && ((id >> 16) & 0x3F) == int(t);
}

bool StyFile::load(const QString &path, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) {
            *err = QStringLiteral("打不开 %1: %2").arg(path, f.errorString());
        }
        return false;
    }
    const QByteArray raw = f.readAll();
    if (raw.size() < kHeadSize) {
        if (err) {
            *err = QStringLiteral("文件太小，不是 .sty");
        }
        return false;
    }

    m_head.uiVersion = rd32(raw, 0);
    m_head.magic2    = rd32(raw, 4);
    m_head.hdrPtr    = rd32(raw, 8);
    m_head.totalSize = rd32(raw, 12);
    m_head.type      = rd8(raw, 16);
    m_head.windowNum = rd8(raw, 17);
    m_head.propLen   = rd16(raw, 18);
    m_head.rotate    = rd8(raw, 20);
    for (int i = 0; i < 3; ++i) {
        m_head.rev[i] = rd8(raw, 21 + i);
    }

    if (m_head.magic2 != 0x6A978292u) {
        if (err) {
            *err = QStringLiteral("magic2=0x%1，不像原厂 .sty").arg(m_head.magic2, 8, 16, QLatin1Char('0'));
        }
        return false;
    }
    const int expect = kHeadSize + kWHeadSize * m_head.windowNum + int(m_head.totalSize);
    if (expect != raw.size()) {
        if (err) {
            *err = QStringLiteral("长度自洽性失败：头算出 %1，实际 %2").arg(expect).arg(raw.size());
        }
        return false;
    }

    m_windows.clear();
    for (int i = 0; i < m_head.windowNum; ++i) {
        const int o = kHeadSize + i * kWHeadSize;
        StyWindow w;
        w.head.offset    = rd32(raw, o);
        w.head.length    = rd32(raw, o + 4);
        w.head.tablePtr  = rd32(raw, o + 8);
        w.head.tableSize = rd16(raw, o + 12);
        for (int k = 0; k < 3; ++k) {
            w.head.crc[k] = rd16(raw, o + 14 + 2 * k);
        }
        m_windows.append(w);
    }

    for (int i = 0; i < m_windows.size(); ++i) {
        StyWindow &w = m_windows[i];
        const int base = int(w.head.offset);
        const int tbl  = int(w.head.tablePtr);

        /* 页块开头是窗口自身的记录，长度不固定（实测 28 B）：
         * 往后 4 字节一步扫，第一个自洽的控件头就是分界。 */
        int first = -1;
        for (int p = base; p < qMin(base + 64, raw.size()); p += 4) {
            if (validCtrlHead(raw, p)) {
                first = p;
                break;
            }
        }
        if (first < 0) {
            first = base;
        }
        w.windowRecord = raw.mid(base, first - base);

        int p = first;
        while (p < tbl && validCtrlHead(raw, p)) {
            StyControl c;
            c.fileOffset   = quint32(p);
            c.head.type    = rd8(raw, p);
            c.head.ctrlNum = rd8(raw, p + 1);
            c.head.cssNum  = rd8(raw, p + 2);
            c.head.len     = rd8(raw, p + 3);
            c.head.page    = rd8(raw, p + 4);
            for (int k = 0; k < 3; ++k) {
                c.head.rev[k] = rd8(raw, p + 5 + k);
            }
            c.head.id  = qint32(rd32(raw, p + 8));
            c.head.css = rd32(raw, p + 12);
            c.payload  = raw.mid(p + kCHeadSize, c.head.len - kCHeadSize);
            w.controls.append(c);
            p += c.head.len;
        }
        /* 控件区之后到索引表之前是 css / 字符串 / 图片列表等散数据。
         * 控件头里的 css 偏移指进这里，且是从高地址往低地址分配的。 */
        w.cssBlob    = raw.mid(p, tbl - p);
        w.indexTable = raw.mid(tbl, w.head.tableSize);
    }
    return true;
}

QByteArray StyFile::serialize() const
{
    QByteArray out;
    out.reserve(int(m_head.totalSize) + kHeadSize + kWHeadSize * m_windows.size());

    wr32(out, m_head.uiVersion);
    wr32(out, m_head.magic2);
    wr32(out, m_head.hdrPtr);
    wr32(out, m_head.totalSize);
    wr8(out, m_head.type);
    wr8(out, quint8(m_windows.size()));
    wr16(out, m_head.propLen);
    wr8(out, m_head.rotate);
    for (int i = 0; i < 3; ++i) {
        wr8(out, m_head.rev[i]);
    }
    for (const StyWindow &w : m_windows) {
        wr32(out, w.head.offset);
        wr32(out, w.head.length);
        wr32(out, w.head.tablePtr);
        wr16(out, w.head.tableSize);
        for (int k = 0; k < 3; ++k) {
            wr16(out, w.head.crc[k]);
        }
    }
    for (const StyWindow &w : m_windows) {
        out.append(w.windowRecord);
        for (const StyControl &c : w.controls) {
            wr8(out, c.head.type);
            wr8(out, c.head.ctrlNum);
            wr8(out, c.head.cssNum);
            wr8(out, c.head.len);
            wr8(out, c.head.page);
            for (int k = 0; k < 3; ++k) {
                wr8(out, c.head.rev[k]);
            }
            wr32(out, quint32(c.head.id));
            wr32(out, c.head.css);
            out.append(c.payload);
        }
        out.append(w.cssBlob);
        out.append(w.indexTable);
    }
    return out;
}

bool StyFile::save(const QString &path, QString *err) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err) {
            *err = QStringLiteral("打不开 %1: %2").arg(path, f.errorString());
        }
        return false;
    }
    f.write(serialize());
    if (!f.commit()) {
        if (err) {
            *err = QStringLiteral("写入失败: %1").arg(f.errorString());
        }
        return false;
    }
    return true;
}

bool StyFile::verifyRoundTrip(const QString &path, QString *report)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (report) {
            *report = QStringLiteral("打不开 %1").arg(path);
        }
        return false;
    }
    const QByteArray orig = f.readAll();

    StyFile s;
    QString err;
    if (!s.load(path, &err)) {
        if (report) {
            *report = QStringLiteral("解析失败: %1").arg(err);
        }
        return false;
    }
    const QByteArray again = s.serialize();
    if (again == orig) {
        if (report) {
            *report = QStringLiteral("往返一致：%1 字节逐字节相同").arg(orig.size());
        }
        return true;
    }
    int diff = -1;
    for (int i = 0; i < qMin(orig.size(), again.size()); ++i) {
        if (orig.at(i) != again.at(i)) {
            diff = i;
            break;
        }
    }
    if (report) {
        *report = QStringLiteral("往返不一致：原 %1 B，重写 %2 B，首个差异 @0x%3")
                  .arg(orig.size()).arg(again.size()).arg(diff, 0, 16);
    }
    return false;
}

QString StyFile::describe() const
{
    QStringList L;
    L << QStringLiteral("UI_VERSION = 0x%1  magic2 = 0x%2")
      .arg(m_head.uiVersion, 8, 16, QLatin1Char('0'))
      .arg(m_head.magic2, 8, 16, QLatin1Char('0'));
    L << QStringLiteral("type=%1 window_num=%2 prop_len=%3 rotate=%4 total_size=%5")
      .arg(m_head.type).arg(m_head.windowNum).arg(m_head.propLen)
      .arg(m_head.rotate).arg(m_head.totalSize);

    for (int i = 0; i < m_windows.size(); ++i) {
        const StyWindow &w = m_windows.at(i);
        L << QStringLiteral("页%1 @0x%2 len=0x%3 table=0x%4+0x%5 控件 %6 条 css %7 B")
          .arg(i).arg(w.head.offset, 0, 16).arg(w.head.length, 0, 16)
          .arg(w.head.tablePtr, 0, 16).arg(w.head.tableSize, 0, 16)
          .arg(w.controls.size()).arg(w.cssBlob.size());
        for (const StyControl &c : w.controls) {
            L << QStringLiteral("   0x%1 t=%2 %3 len=%4 id=0x%5 hash=0x%6 css=0x%7")
              .arg(c.fileOffset, 6, 16, QLatin1Char('0'))
              .arg(c.head.type, 2)
              .arg(QString::fromLatin1(StyCtrlType::name(c.head.type)), -16)
              .arg(c.head.len, 3)
              .arg(c.head.id & 0xFFFFFF, 6, 16, QLatin1Char('0'))
              .arg(c.idHash(), 4, 16, QLatin1Char('0'))
              .arg(c.head.css, 0, 16);
        }
    }
    return L.join(QLatin1Char('\n'));
}
