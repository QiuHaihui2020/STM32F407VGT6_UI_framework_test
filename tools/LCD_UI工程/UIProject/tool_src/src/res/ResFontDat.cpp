#include "ResFontDat.h"

#include <QFile>

namespace res {

namespace {

const int    HEAD_SZ   = 4;             ///< "1.2\0"
const int    REC_SZ    = 132;
const int    LF_OFF    = 6;             ///< 记录内 LOGFONTW 的偏移
const int    LF_FACE   = 28;            ///< LOGFONTW 内 lfFaceName 的偏移
const int    LF_CHARS  = 32;            ///< lfFaceName 的 wchar 个数
const int    ROW_OFF   = 98;
const int    COL_OFF   = 102;
const int    MAX_COL   = 255;           ///< key() 只留了 8 位给列号

qint32 rd32(const uchar *p)
{
    return qint32(quint32(p[0]) | (quint32(p[1]) << 8)
                  | (quint32(p[2]) << 16) | (quint32(p[3]) << 24));
}

} // namespace

bool ResFontDat::load(const QString &path)
{
    m_cells.clear();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray raw = f.readAll();
    if (raw.size() < HEAD_SZ + REC_SZ || !raw.startsWith("1.2")) {
        return false;
    }

    const uchar *base = reinterpret_cast<const uchar *>(raw.constData());
    const int n = (raw.size() - HEAD_SZ) / REC_SZ;
    for (int i = 0; i < n; ++i) {
        const uchar *r = base + HEAD_SZ + i * REC_SZ;
        const qint32 row = rd32(r + ROW_OFF);
        const qint32 col = rd32(r + COL_OFF);
        if (row <= 0 || col <= 0 || col > MAX_COL) {
            /* 网格之外的记录：文件不是我们认识的那种，整份作废，
             * 免得半读半猜生成一份四不像的资源。 */
            m_cells.clear();
            return false;
        }

        const uchar *lf = r + LF_OFF;
        LogFontSpec s;
        s.height         = rd32(lf + 0);
        s.width          = rd32(lf + 4);
        s.escapement     = rd32(lf + 8);
        s.orientation    = rd32(lf + 12);
        s.weight         = rd32(lf + 16);
        s.italic         = lf[20];
        s.underline      = lf[21];
        s.strikeOut      = lf[22];
        s.charSet        = lf[23];
        s.outPrecision   = lf[24];
        s.clipPrecision  = lf[25];
        s.quality        = lf[26];
        s.pitchAndFamily = lf[27];

        QString face;
        for (int k = 0; k < LF_CHARS; ++k) {
            const ushort c = ushort(lf[LF_FACE + k * 2])
                             | ushort(lf[LF_FACE + k * 2 + 1] << 8);
            if (c == 0) {
                break;
            }
            face.append(QChar(c));
        }
        if (face.isEmpty()) {
            continue;                   // 没有字体名的格子按默认处理
        }
        s.faceName = face;

        m_cells.insert(key(row, col), s);
    }
    return !m_cells.isEmpty();
}

bool ResFontDat::fontAt(int row, int col, LogFontSpec *out) const
{
    const auto it = m_cells.constFind(key(row, col));
    if (it == m_cells.constEnd()) {
        return false;
    }
    if (out) {
        *out = it.value();
    }
    return true;
}

} // namespace res
