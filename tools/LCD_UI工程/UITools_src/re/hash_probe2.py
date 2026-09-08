# -*- coding: utf-8 -*-
"""ename.h 的 ID 疑似 [高8位标记][低16位 CRC]，逐一试 CRC16 变体。"""
import re
import sys
from collections import Counter

txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
pairs = [(n, int(v, 16)) for n, v in
         re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt) if n != 'UI_VERSION']

print('高 8 位分布:', Counter(v >> 16 for _, v in pairs).most_common(12))
print('低 16 位是否唯一:', len({v & 0xFFFF for _, v in pairs}), '/', len(pairs))
print('整值是否唯一:', len({v for _, v in pairs}), '/', len(pairs))


def crc16(data, poly, init, refin, refout, xorout):
    if refin:
        crc = init
        for b in data:
            crc ^= b
            for _ in range(8):
                crc = (crc >> 1) ^ poly if crc & 1 else crc >> 1
    else:
        crc = init
        for b in data:
            crc ^= b << 8
            for _ in range(8):
                crc = ((crc << 1) ^ poly) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    if refout != refin:
        crc = int('{:016b}'.format(crc)[::-1], 2)
    return crc ^ xorout


VARIANTS = {
    'XMODEM':      (0x1021, 0x0000, False, False, 0x0000),
    'CCITT-FALSE': (0x1021, 0xFFFF, False, False, 0x0000),
    'MODBUS':      (0xA001, 0xFFFF, True,  True,  0x0000),
    'IBM/ARC':     (0xA001, 0x0000, True,  True,  0x0000),
    'MAXIM':       (0xA001, 0x0000, True,  True,  0xFFFF),
    'USB':         (0xA001, 0xFFFF, True,  True,  0xFFFF),
    'KERMIT':      (0x8408, 0x0000, True,  True,  0x0000),
    'X25':         (0x8408, 0xFFFF, True,  True,  0xFFFF),
    'DNP':         (0xA6BC, 0x0000, True,  True,  0xFFFF),
}
CASES = {'as-is': lambda s: s, 'lower': lambda s: s.lower()}

hits = []
for vn, (poly, init, ri, ro, xo) in VARIANTS.items():
    for cn, cf in CASES.items():
        n16 = sum(1 for n, v in pairs
                  if crc16(cf(n).encode(), poly, init, ri, ro, xo) == (v & 0xFFFF))
        if n16:
            hits.append((n16, vn, cn))
hits.sort(reverse=True)
for h in hits[:8]:
    print('低16位命中 %d/%d  %s  %s' % (h[0], len(pairs), h[1], h[2]))
if not hits:
    print('CRC16 变体全部 0 命中')
    for n, v in pairs[:5]:
        print('  %-22s 0x%06X  low16=0x%04X  xmodem=0x%04X kermit=0x%04X'
              % (n, v, v & 0xFFFF,
                 crc16(n.encode(), 0x1021, 0, False, False, 0),
                 crc16(n.encode(), 0x8408, 0, True, True, 0)))
