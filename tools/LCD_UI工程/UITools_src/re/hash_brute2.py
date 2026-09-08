# -*- coding: utf-8 -*-
"""最后一轮：多种输入变体 x CRC16 全多项式。"""
import re
import sys

txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
pairs = [(n, int(v, 16) & 0xFFFF) for n, v in
         re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt) if n != 'UI_VERSION']

VARIANTS = {
    'ename': lambda n: n.encode(),
    'ename+NUL': lambda n: n.encode() + b'\x00',
    'lower': lambda n: n.lower().encode(),
    'lower+NUL': lambda n: n.lower().encode() + b'\x00',
    'utf16le': lambda n: n.encode('utf-16-le'),
    'reversed': lambda n: n.encode()[::-1],
}


def crc_msb(d, poly, init):
    c = init
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def crc_lsb(d, poly, init):
    c = init
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ poly if c & 1 else c >> 1
    return c


n0, t0 = pairs[0]
n1, t1 = pairs[1]
print('探针 %s->0x%04X, %s->0x%04X' % (n0, t0, n1, t1))
found = False
for vn, vf in VARIANTS.items():
    b0, b1 = vf(n0), vf(n1)
    for init in (0x0000, 0xFFFF, 0x1D0F):
        for poly in range(0x10000):
            for kind, f in (('msb', crc_msb), ('lsb', crc_lsb)):
                if f(b0, poly, init) == t0 and f(b1, poly, init) == t1:
                    hit = sum(1 for n, t in pairs if f(vf(n), poly, init) == t)
                    print('  ★ %s %s poly=0x%04X init=0x%04X 命中 %d/%d'
                          % (vn, kind, poly, init, hit, len(pairs)))
                    found = True
    print('  %-10s 扫完' % vn)
if not found:
    print('全部未命中 —— 低 16 位不是这些输入的 CRC16')
