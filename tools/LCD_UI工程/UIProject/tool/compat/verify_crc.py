# -*- coding: utf-8 -*-
"""验证 .sty 页表里三个 CRC 的定义。

字段名直接来自固件 ui_resources_manager.c：

    struct window_head {
        u32 offset;            // +0
        u32 len;               // +4
        u32 ptr_table_offset;  // +8
        u16 ptr_table_len;     // +12
        u16 crc_data;          // +14
        u16 crc_table;         // +16
        u16 crc_head;          // +18
    };

固件的校验逻辑：
    CRC16(&window, offsetof(window_head, crc_data)) == window.crc_head   // 头 14 字节
    CRC16(页数据, window.len)                      == window.crc_data
    CRC16(指针表, window.ptr_table_len)            == window.crc_table
CRC16() 就是 crc16_xmodem(ptr, len, 0)（见 liba/common/jl_crc.c）。

固件对 crc_head / crc_data 是**真校验**，对不上直接返回 NULL —— 所以生成器必须算对。
"""
import struct
import sys

HEAD_SZ, WHEAD_SZ = 24, 20


def crc16_xmodem(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def main():
    raw = open(sys.argv[1], 'rb').read()
    nwin = raw[17]
    ok = bad = 0
    print('页数 %d' % nwin)
    for i in range(nwin):
        o = HEAD_SZ + i * WHEAD_SZ
        wh = raw[o:o + WHEAD_SZ]
        offset, ln, tptr = struct.unpack_from('<III', wh, 0)
        tlen, c_data, c_table, c_head = struct.unpack_from('<HHHH', wh, 12)

        got_head = crc16_xmodem(wh[:14])
        got_data = crc16_xmodem(raw[offset:offset + ln])
        got_table = crc16_xmodem(raw[tptr:tptr + tlen])

        for name, want, got in (('crc_head', c_head, got_head),
                                ('crc_data', c_data, got_data),
                                ('crc_table', c_table, got_table)):
            flag = 'OK ' if want == got else 'BAD'
            if want == got:
                ok += 1
            else:
                bad += 1
            print('  页%d %-10s 文件里 0x%04X  算出来 0x%04X  %s'
                  % (i, name, want, got, flag))
    print('\n一致 %d / 不一致 %d' % (ok, bad))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
