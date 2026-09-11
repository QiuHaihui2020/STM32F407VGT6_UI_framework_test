#include "XlsReader.h"

#include <QFile>
#include <QtEndian>

namespace res {

namespace {

inline quint16 u16(const QByteArray &b, int o)
{
    return (o + 2 <= b.size())
           ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData()) + o)
           : quint16(0);
}
inline quint32 u32(const QByteArray &b, int o)
{
    return (o + 4 <= b.size())
           ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()) + o)
           : quint32(0);
}
inline qint32 i32(const QByteArray &b, int o) { return static_cast<qint32>(u32(b, o)); }

// ---------------------------------------------------------------- CFBF ----
class Cfbf
{
public:
    bool open(const QByteArray &raw);
    QByteArray stream(const QString &name) const;
    QString error() const { return m_error; }

private:
    int sectorOffset(qint32 s) const { return 512 + s * m_sectorSize; }
    QByteArray readChain(qint32 start, int size = -1) const;
    QByteArray readMiniChain(qint32 start, int size) const;

    QByteArray m_raw;
    QByteArray m_dir;
    QByteArray m_miniStream;
    QVector<qint32> m_fat;
    QVector<qint32> m_miniFat;
    int m_sectorSize = 512;
    int m_miniSectorSize = 64;
    quint32 m_miniCutoff = 4096;
    QString m_error;
};

bool Cfbf::open(const QByteArray &raw)
{
    static const char kSig[8] = {'\xd0', '\xcf', '\x11', '\xe0',
                                 '\xa1', '\xb1', '\x1a', '\xe1'};
    if (raw.size() < 512 || memcmp(raw.constData(), kSig, 8) != 0) {
        m_error = QStringLiteral("不是复合文档(.xls)");
        return false;
    }
    m_raw = raw;
    m_sectorSize = 1 << u16(raw, 30);
    m_miniSectorSize = 1 << u16(raw, 32);
    m_miniCutoff = u32(raw, 56);

    const quint32 nFat = u32(raw, 44);
    const qint32 dirStart = i32(raw, 48);
    const qint32 miniFatStart = i32(raw, 60);
    const qint32 difatStart = i32(raw, 68);
    const quint32 nDifat = u32(raw, 72);

    QVector<qint32> difat;
    for (int i = 0; i < 109; ++i) {
        difat.append(i32(raw, 76 + 4 * i));
    }
    qint32 sec = difatStart;
    const int perSector = m_sectorSize / 4 - 1;
    for (quint32 i = 0; i < nDifat && sec >= 0; ++i) {
        const int base = sectorOffset(sec);
        for (int k = 0; k < perSector; ++k) {
            difat.append(i32(raw, base + 4 * k));
        }
        sec = i32(raw, base + m_sectorSize - 4);
    }

    for (int i = 0; i < int(nFat) && i < difat.size(); ++i) {
        const qint32 s = difat.at(i);
        if (s < 0) {
            continue;
        }
        const int base = sectorOffset(s);
        for (int k = 0; k < m_sectorSize / 4; ++k) {
            m_fat.append(i32(raw, base + 4 * k));
        }
    }
    if (m_fat.isEmpty()) {
        m_error = QStringLiteral("FAT 为空");
        return false;
    }

    m_dir = readChain(dirStart);
    if (m_dir.isEmpty()) {
        m_error = QStringLiteral("目录流为空");
        return false;
    }
    if (miniFatStart >= 0) {
        const QByteArray mf = readChain(miniFatStart);
        for (int i = 0; i + 4 <= mf.size(); i += 4) {
            m_miniFat.append(i32(mf, i));
        }
    }
    // 根目录项（第 0 个）的 start/size 指向 MiniStream
    const qint32 rootStart = i32(m_dir, 116);
    if (rootStart >= 0) {
        m_miniStream = readChain(rootStart);
    }
    return true;
}

QByteArray Cfbf::readChain(qint32 sec, int size) const
{
    QByteArray out;
    int guard = 0;
    while (sec >= 0 && sec < m_fat.size() && guard++ < (1 << 22)) {
        const int off = sectorOffset(sec);
        if (off < 0 || off + m_sectorSize > m_raw.size()) {
            break;
        }
        out.append(m_raw.constData() + off, m_sectorSize);
        sec = m_fat.at(sec);
    }
    if (size >= 0 && out.size() > size) {
        out.truncate(size);
    }
    return out;
}

QByteArray Cfbf::readMiniChain(qint32 sec, int size) const
{
    QByteArray out;
    int guard = 0;
    while (sec >= 0 && sec < m_miniFat.size() && guard++ < (1 << 22)) {
        const int off = sec * m_miniSectorSize;
        if (off + m_miniSectorSize > m_miniStream.size()) {
            break;
        }
        out.append(m_miniStream.constData() + off, m_miniSectorSize);
        sec = m_miniFat.at(sec);
    }
    if (out.size() > size) {
        out.truncate(size);
    }
    return out;
}

QByteArray Cfbf::stream(const QString &want) const
{
    for (int i = 0; (i + 1) * 128 <= m_dir.size(); ++i) {
        const int o = i * 128;
        const int nameLen = u16(m_dir, o + 64);
        if (nameLen < 2) {
            continue;
        }
        const QString name = QString::fromUtf16(
            reinterpret_cast<const ushort *>(m_dir.constData() + o), (nameLen - 2) / 2);
        if (name != want) {
            continue;
        }
        const qint32 start = i32(m_dir, o + 116);
        const quint32 size = u32(m_dir, o + 120);
        return (size < m_miniCutoff) ? readMiniChain(start, int(size))
                                     : readChain(start, int(size));
    }
    return QByteArray();
}

// ---------------------------------------------------------------- BIFF ----
struct Record {
    quint16 type = 0;
    QByteArray body;
    QVector<QByteArray> conts;      ///< 紧随其后的 CONTINUE 块
};

QVector<Record> readRecords(const QByteArray &s)
{
    QVector<Record> out;
    int o = 0;
    while (o + 4 <= s.size()) {
        Record r;
        r.type = u16(s, o);
        const int n = u16(s, o + 2);
        if (o + 4 + n > s.size()) {
            break;
        }
        r.body = s.mid(o + 4, n);
        o += 4 + n;
        while (o + 4 <= s.size() && u16(s, o) == 0x003C) {
            const int n2 = u16(s, o + 2);
            if (o + 4 + n2 > s.size()) {
                break;
            }
            r.conts.append(s.mid(o + 4, n2));
            o += 4 + n2;
        }
        out.append(r);
    }
    return out;
}

/// BIFF8 变长 Unicode 串。可能跨 CONTINUE，且每次续块开头会重发 1 字节 flags。
class UniCursor
{
public:
    UniCursor(const QByteArray &body, const QVector<QByteArray> &conts)
        : m_body(body), m_conts(conts) {}

    const QByteArray &cur() const { return m_ci < 0 ? m_body : m_conts.at(m_ci); }
    int pos() const { return m_pos; }
    void setPos(int p) { m_pos = p; }

    bool ensure(int need)
    {
        while (m_pos + need > cur().size()) {
            if (m_ci + 1 >= m_conts.size()) {
                return false;
            }
            ++m_ci;
            m_pos = 0;
        }
        return true;
    }

    QString readString(int cch)
    {
        if (!ensure(1)) {
            return QString();
        }
        const QByteArray *s = &cur();
        quint8 flags = quint8(s->at(m_pos++));
        bool high = flags & 0x01;
        const bool ext = flags & 0x04;
        const bool rt = flags & 0x08;
        int nrt = 0;
        quint32 cbExt = 0;
        if (rt) {
            nrt = u16(*s, m_pos);
            m_pos += 2;
        }
        if (ext) {
            cbExt = u32(*s, m_pos);
            m_pos += 4;
        }

        QString out;
        int left = cch;
        while (left > 0) {
            s = &cur();
            const int unit = high ? 2 : 1;
            int avail = (s->size() - m_pos) / unit;
            if (avail <= 0) {
                if (m_ci + 1 >= m_conts.size()) {
                    break;
                }
                ++m_ci;
                m_pos = 0;
                s = &cur();
                high = quint8(s->at(m_pos++)) & 0x01;
                continue;
            }
            const int take = qMin(left, avail);
            if (high) {
                out += QString::fromUtf16(
                    reinterpret_cast<const ushort *>(s->constData() + m_pos), take);
                m_pos += take * 2;
            } else {
                out += QString::fromLatin1(s->constData() + m_pos, take);
                m_pos += take;
            }
            left -= take;
        }
        // 富文本格式串 / 远东扩展块跳过（可能也跨 CONTINUE，这里按整块跳）
        int skip = nrt * 4 + int(cbExt);
        while (skip > 0) {
            const int room = cur().size() - m_pos;
            if (skip <= room) {
                m_pos += skip;
                break;
            }
            skip -= room;
            if (m_ci + 1 >= m_conts.size()) {
                m_pos = cur().size();
                break;
            }
            ++m_ci;
            m_pos = 0;
        }
        return out;
    }

private:
    const QByteArray &m_body;
    const QVector<QByteArray> &m_conts;
    int m_ci = -1;
    int m_pos = 0;
};

void setCell(XlsSheet &sh, int r, int c, const QString &v)
{
    while (sh.rows.size() <= r) {
        sh.rows.append(QStringList());
    }
    QStringList &row = sh.rows[r];
    while (row.size() <= c) {
        row.append(QString());
    }
    row[c] = v;
}

} // namespace

bool XlsReader::load(const QString &path)
{
    m_sheets.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("打不开 %1").arg(path);
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    Cfbf cf;
    if (!cf.open(raw)) {
        m_error = cf.error();
        return false;
    }
    QByteArray wb = cf.stream(QStringLiteral("Workbook"));
    if (wb.isEmpty()) {
        wb = cf.stream(QStringLiteral("Book"));
    }
    if (wb.isEmpty()) {
        m_error = QStringLiteral("找不到 Workbook 流");
        return false;
    }

    const QVector<Record> recs = readRecords(wb);
    QStringList sheetNames;
    QStringList sst;
    int cur = -1;

    for (const Record &r : recs) {
        switch (r.type) {
        case 0x0085: {                       // BOUNDSHEET
            const int cch = quint8(r.body.at(6));
            const quint8 grbit = quint8(r.body.at(7));
            sheetNames.append(grbit & 1
                ? QString::fromUtf16(
                      reinterpret_cast<const ushort *>(r.body.constData() + 8), cch)
                : QString::fromLatin1(r.body.constData() + 8, cch));
            break;
        }
        case 0x00FC: {                       // SST
            const quint32 cnt = u32(r.body, 4);
            UniCursor cur2(r.body, r.conts);
            cur2.setPos(8);
            for (quint32 i = 0; i < cnt; ++i) {
                if (!cur2.ensure(2)) {
                    break;
                }
                const int cch = u16(cur2.cur(), cur2.pos());
                cur2.setPos(cur2.pos() + 2);
                sst.append(cur2.readString(cch));
            }
            break;
        }
        case 0x0809:                         // BOF
            if (u16(r.body, 2) == 0x0010) {  // 0x0010 = worksheet
                XlsSheet sh;
                sh.name = (m_sheets.size() < sheetNames.size())
                          ? sheetNames.at(m_sheets.size()) : QString();
                m_sheets.append(sh);
                cur = m_sheets.size() - 1;
            }
            break;
        case 0x00FD:                         // LABELSST
            if (cur >= 0) {
                const int ix = int(u32(r.body, 6));
                setCell(m_sheets[cur], u16(r.body, 0), u16(r.body, 2),
                        (ix >= 0 && ix < sst.size()) ? sst.at(ix) : QString());
            }
            break;
        case 0x0204: {                       // LABEL
            if (cur >= 0) {
                const int cch = u16(r.body, 6);
                UniCursor c2(r.body, r.conts);
                c2.setPos(8);
                setCell(m_sheets[cur], u16(r.body, 0), u16(r.body, 2), c2.readString(cch));
            }
            break;
        }
        case 0x0203: {                       // NUMBER
            if (cur >= 0 && r.body.size() >= 14) {
                double d = 0;
                memcpy(&d, r.body.constData() + 6, sizeof(double));
                setCell(m_sheets[cur], u16(r.body, 0), u16(r.body, 2),
                        QString::number(d, 'g', 15));
            }
            break;
        }
        default:
            break;
        }
    }

    if (m_sheets.isEmpty()) {
        m_error = QStringLiteral("没有工作表");
        return false;
    }
    return true;
}

} // namespace res
