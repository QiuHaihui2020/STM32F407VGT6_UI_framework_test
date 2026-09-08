# -*- coding: utf-8 -*-
"""暴力搜低 16 位哈希：CRC16 全多项式 + 乘法哈希全乘子。"""
import re
import sys

txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
pairs = [(n, int(v, 16)) for n, v in
         re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt) if n != 'UI_VERSION']
probe = [(n, v & 0xFFFF) for n, v in pairs]
n0, t0 = probe[0]
b0 = n0.encode()
print('探针: %s -> 0x%04X (共 %d 条样本)' % (n0, t0, len(probe)))


def crc_msb(data, poly, init):
    c = init
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def crc_lsb(data, poly, init):
    c = init
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ poly if c & 1 else c >> 1
    return c


found = []
for init in (0x0000, 0xFFFF):
    for poly in range(0x10000):
        if crc_msb(b0, poly, init) == t0:
            found.append(('msb', poly, init))
        if crc_lsb(b0, poly, init) == t0:
            found.append(('lsb', poly, init))
print('CRC16 一级候选:', len(found))
ok = []
for kind, poly, init in found:
    f = crc_msb if kind == 'msb' else crc_lsb
    hit = sum(1 for n, t in probe if f(n.encode(), poly, init) == t)
    if hit > len(probe) * 0.5:
        ok.append((hit, kind, hex(poly), hex(init)))
for o in sorted(ok, reverse=True)[:5]:
    print('  CRC16 命中 %d/%d  %s poly=%s init=%s' % (o[0], len(probe), o[1], o[2], o[3]))

# 乘法哈希: h = h*mul + c  (或 ^c)，取低 16
best = []
for mul in range(2, 4096):
    for init in (0, 1, 5381, 0xFFFF, 0x1505):
        h = init
        for c in b0:
            h = (h * mul + c) & 0xFFFFFFFF
        if (h & 0xFFFF) == t0:
            hit = 0
            for n, t in probe:
                hh = init
                for c in n.encode():
                    hh = (hh * mul + c) & 0xFFFFFFFF
                if (hh & 0xFFFF) == t:
                    hit += 1
            best.append((hit, mul, init))
best.sort(reverse=True)
for b in best[:5]:
    print('  乘法哈希 命中 %d/%d  mul=%d init=%d' % (b[0], len(probe), b[1], b[2]))
if not ok and not best:
    print('两类都没搜到 —— 哈希输入很可能不是这个符号名本身')
