# -*- coding: utf-8 -*-
"""往返验证：按 sty_dump.py 的切分把 .sty 拆开再拼回去，和原文件逐字节比较。

拼回去若与原文件完全相同，说明切分没有遗漏任何字节 —— 这是"格式理解完整"
最直接的证据，也是 src/core/StyFile.cpp 里 verifyRoundTrip() 的 Python 对照实现。
"""
import sys
import struct
from sty_dump import parse, HEAD_SZ, WHEAD_SZ, CHEAD_SZ


def rebuild(path):
    raw = open(path, 'rb').read()
    doc = parse(path)
    h, ws = doc['head'], doc['windows']

    out = bytearray()
    out += struct.pack('<IIII', h['ui_version'], h['magic2'], h['hdr_ptr'], h['total_size'])
    out += struct.pack('<BBHB', h['type'], h['window_num'], h['prop_len'], h['rotate'])
    out += bytes.fromhex(h['rev'])
    for w in ws:
        out += struct.pack('<III', w['offset'], w['length'], w['table_ptr'])
        out += struct.pack('<HHHH', w['table_size'], *w['crc'])

    for w in ws:
        first = w['offset'] + len(w['window_record']) // 2
        out += bytes.fromhex(w['window_record'])
        for c in w['controls']:
            out += bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page']])
            out += b'\xff\xff\xff'
            out += struct.pack('<iI', c['id'] if c['id'] < 0x80000000 else c['id'] - (1 << 32),
                               c['css_off'])
            out += bytes.fromhex(c['payload'])
        # 控件区结束位置
        p = w['scan_stopped_at']
        out += raw[p:w['table_ptr']]                      # css / 散数据
        out += raw[w['table_ptr']:w['table_ptr'] + w['table_size']]   # 索引表
    return raw, bytes(out)


def main():
    path = sys.argv[1]
    raw, again = rebuild(path)
    if raw == again:
        print('往返一致：%d 字节逐字节相同 —— 切分无遗漏' % len(raw))
        return 0
    print('往返不一致：原 %d B，重建 %d B' % (len(raw), len(again)))
    n = min(len(raw), len(again))
    for i in range(n):
        if raw[i] != again[i]:
            print('  首个差异 @0x%X: 原 %02X 重建 %02X' % (i, raw[i], again[i]))
            print('  原   :', raw[max(0, i - 8):i + 24].hex())
            print('  重建 :', again[max(0, i - 8):i + 24].hex())
            break
    return 1


if __name__ == '__main__':
    sys.exit(main())
