# -*- coding: utf-8 -*-
import sys
sys.path.insert(0, r'D:\MyFile\zh-jieli\sdk_demo\JIELI_STM32_UI\STM32F407VGT6_Template\tools\LCD_UI工程\UITools_src\re')
from sty_dump import parse

PATH = r'D:\MyFile\zh-jieli\sdk_demo\JIELI_STM32_UI\STM32F407VGT6_Template\tools\JL\JL.sty'


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
    'XMODEM': (0x1021, 0x0000, False, False, 0x0000),
    'CCITT-FALSE': (0x1021, 0xFFFF, False, False, 0x0000),
    'MODBUS': (0xA001, 0xFFFF, True, True, 0x0000),
    'IBM': (0xA001, 0x0000, True, True, 0x0000),
    'KERMIT': (0x8408, 0x0000, True, True, 0x0000),
    'X25': (0x8408, 0xFFFF, True, True, 0xFFFF),
    'MAXIM': (0xA001, 0x0000, True, True, 0xFFFF),
    'USB': (0xA001, 0xFFFF, True, True, 0xFFFF),
}

raw = open(PATH, 'rb').read()
doc = parse(PATH)
pages = doc['windows']
target = [w['crc'][2] for w in pages]
print('要找的第3个 CRC:', ['0x%04X' % t for t in target])

bnds = []
for w in pages:
    bnds.append({'o': w['offset'], 'first': w['controls'][0]['off'],
                 'ce': w['scan_stopped_at'], 'tp': w['table_ptr'],
                 'end': w['table_ptr'] + w['table_size'],
                 'o24': w['offset'] + 24, 'o28': w['offset'] + 28})
keys = list(bnds[0].keys())
hits = []
for vn, (poly, init, ri, ro, xo) in VAR.items():
    for ka in keys:
        for kb in keys:
            if all(bnds[i][kb] > bnds[i][ka] for i in range(len(pages))):
                vals = [crc16(raw[bnds[i][ka]:bnds[i][kb]], poly, init, ri, ro, xo)
                        for i in range(len(pages))]
                if all(vals[i] == target[i] for i in range(len(pages))):
                    hits.append((vn, ka, kb))
if hits:
    for h in hits:
        print('  命中: %s  区段 [%s, %s)' % h)
    sys.exit(0)

print('  区段组合未命中，改试「整页 + 任意 init」')
for vn, (poly, init, ri, ro, xo) in VAR.items():
    for seed in range(0x10000):
        if crc16(raw[bnds[0]['o']:bnds[0]['end']], poly, seed, ri, ro, xo) != target[0]:
            continue
        if all(crc16(raw[bnds[i]['o']:bnds[i]['end']], poly, seed, ri, ro, xo) == target[i]
               for i in range(len(pages))):
            print('  命中: %s 整页 init=0x%04X' % (vn, seed))
            sys.exit(0)
print('  没找到 —— 第3个 CRC 的算法/输入待定')
