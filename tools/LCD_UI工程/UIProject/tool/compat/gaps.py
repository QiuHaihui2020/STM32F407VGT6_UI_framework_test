# -*- coding: utf-8 -*-
"""把 sty_blocks 没覆盖到的字节打出来看。"""
import struct
import sys
from sty_dump import parse
from sty_blocks import PTRS, CSS_SIZE, NONE, u16, u32

path = sys.argv[1]
raw = open(path, 'rb').read()
doc = parse(path)
w = doc['windows'][0]
base, lo, hi = w['offset'], w['scan_stopped_at'], w['table_ptr']
cov = bytearray(hi - lo)
owner = {}


def mark(off, size, kind):
    if off < lo or off + size > hi:
        return
    for i in range(off - lo, off - lo + size):
        cov[i] = 1
    owner[off] = (size, kind)


for c in w['controls']:
    pl = bytes.fromhex(c['payload'])
    rec = bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                 0xFF, 0xFF, 0xFF]) + struct.pack('<iI', c['id'], c['css_off']) + pl
    for name, off in PTRS.get(c['type'], [('css', 12)]):
        if off + 4 > len(rec):
            continue
        p = u32(rec, off)
        if p in NONE:
            continue
        a = base + (p & 0xFFFF)
        if name == 'css':
            mark(a, CSS_SIZE, 'css')
        elif name.startswith('img'):
            mark(a, 2 + 2 * u16(raw, a), 'image_list')
        elif name == 'strlist':
            mark(a, 2 + 2 * u16(raw, a), 'text_list')
        elif name == 'action':
            n = u16(raw, a)
            size = 2
            q = a + 2
            for _ in range(n):
                if q + 9 > hi:
                    break
                size += (9 + raw[q + 8] + 3) & ~3
                q = a + size
            mark(a, size, 'action')

# 找空洞并打印上下文
runs = []
i = 0
while i < len(cov):
    if not cov[i]:
        j = i
        while j < len(cov) and not cov[j]:
            j += 1
        runs.append((lo + i, j - i))
        i = j
    else:
        i += 1
print('空洞 %d 段' % len(runs))
from collections import Counter
print('长度分布:', Counter(r[1] for r in runs).most_common())
print()
for off, n in runs[:14]:
    # 找它前面紧邻的已知块
    prev = None
    for o, (s, k) in owner.items():
        if o + s == off:
            prev = (o, s, k)
    nxt = owner.get(off + n)
    print('  空洞 @0x%-6X %d B : %s' % (off, n, raw[off:off + n].hex()))
    print('        前一块: %s   后一块: %s'
          % (('%s@0x%X(%dB)' % (prev[2], prev[0], prev[1])) if prev else '?',
             ('%s@0x%X(%dB)' % (nxt[1], off + n, nxt[0])) if nxt else '?'))
