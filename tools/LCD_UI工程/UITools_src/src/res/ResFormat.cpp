#include "ResFormat.h"

namespace res {

quint16 crc16(const void *data, int len, quint16 init)
{
    const quint8 *p = static_cast<const quint8 *>(data);
    quint16 crc = init;
    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<quint16>(p[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<quint16>((crc << 1) ^ 0x1021)
                                 : static_cast<quint16>(crc << 1);
        }
    }
    return crc;
}

} // namespace res
