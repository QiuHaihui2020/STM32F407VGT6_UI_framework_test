# -*- coding: utf-8 -*-
"""页表里那 3 个 u16 CRC 是对哪段数据算的、用什么算法。"""
import sys
sys.path.insert(0, r'D:\MyFile\zh-jieli\sdk_demo\JIELI_STM32_UI\STM32F407VGT6_Template\tools\LCD_UI工程\UITools_src\re')
from sty_dump import parse

raw = open(sys.argv[1], 'rb').read()
doc = parse(sys.argv[1])


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


VAR = {
    'XMODEM':      (0x1021, 0x0000, False, False, 0x0000),
    'CCITT-FALSE': (0x1021, 0xFFFF, False, False, 0x0000),
    'MODBUS':      (0xA001, 0xFFFF, True,  True,  0x0000),
    'IBM':         (0xA001, 0x0000, True,  True,  0x0000),
    'KERMIT':      (0x8408, 0x0000, True,  True,  0x0000),
    'X25':         (0x8408, 0xFFFF, True,  True,  0xFFFF),
    'MAXIM':       (0xA001, 0x0000, True,  True,  0xFFFF),
    'USB':         (0xA001, 0xFFFF, True,  True,  0xFFFF),
}

pages = doc['windows']
print('目标 CRC:')
for w in pages:
    print('   页@0x%X  %s' % (w['offset'], ['0x%04X' % c for c in w['crc']]))

regions = {}
for i, w in enumerate(pages):
    o, ln, tp, ts = w['offset'], w['length'], w['table_ptr'], w['table_size']
    first = w['controls'][0]['off'] if w['controls'] else o
    ce = w['scan_stopped_at']
    regions[i] = {
        '整页': raw[o:o + ln],
        '窗口记录': raw[o:first],
        '控件区': raw[first:ce],
        '窗口+控件': raw[o:ce],
        '数据区': raw[ce:tp],
        '索引表': raw[tp:tp + ts],
        '控件+数据': raw[first:tp],
        '页去掉表': raw[o:tp],
    }

hits = []
for vn, (poly, init, ri, ro, xo) in VAR.items():
    for rn in regions[0]:
        vals = [crc16(regions[i][rn], poly, init, ri, ro, xo) for i in range(len(pages))]
        for slot in range(3):
            if all(vals[i] == pages[i]['crc'][slot] for i in range(len(pages))):
                hits.append((vn, rn, slot, '全部 %d 页' % len(pages)))
            elif vals[0] == pages[0]['crc'][slot]:
                hits.append((vn, rn, slot, '仅页0'))
if hits:
    for h in hits:
        print('  命中: %s  区段=%s  第%d个CRC  (%s)' % h)
else:
    print('  8 种 CRC16 x 8 个区段 x 3 个槽位，全部未命中')
    print('  参考：页0 各区段长度 %s' % {k: len(v) for k, v in regions[0].items()})
