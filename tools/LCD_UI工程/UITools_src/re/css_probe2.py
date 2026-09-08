# -*- coding: utf-8 -*-
import struct
import sys
from collections import Counter
sys.path.insert(0, r'D:\MyFile\zh-jieli\sdk_demo\JIELI_STM32_UI\STM32F407VGT6_Template\tools\LCD_UI工程\UITools_src\re')
from sty_dump import parse

path = sys.argv[1]
raw = open(path, 'rb').read()
doc = parse(path)
w = doc['windows'][0]
ctrls = w['controls']
base = w['offset']
tbl = raw[w['table_ptr']:w['table_ptr'] + w['table_size']]
vals = list(struct.unpack('<%dH' % (len(tbl) // 2), tbl))

print('页0 base=0x%X  控件区 0x%X..0x%X  css区 0x%X..0x%X  表 0x%X(%dB, %d 项 u16)'
      % (base, ctrls[0]['off'], w['scan_stopped_at'],
         w['scan_stopped_at'], w['table_ptr'], w['table_ptr'], len(tbl), len(vals)))

coffs = [c['off'] for c in ctrls]
print('\n控件偏移(%d): %s ... %s' % (len(coffs),
      ' '.join('%X' % o for o in coffs[:8]), ' '.join('%X' % o for o in coffs[-6:])))
cssoffs = sorted({c['css_off'] for c in ctrls})
print('css 偏移(%d): %s ... %s' % (len(cssoffs),
      ' '.join('%X' % o for o in cssoffs[:6]), ' '.join('%X' % o for o in cssoffs[-6:])))

print('\n表值范围 0x%X..0x%X' % (min(vals), max(vals)))
setc = set(coffs)
setcss = set(cssoffs)
print('表值命中控件偏移: %d/%d' % (sum(1 for v in vals if v in setc), len(vals)))
print('表值命中 css 偏移 : %d/%d' % (sum(1 for v in vals if v in setcss), len(vals)))
for delta_name, delta in (('+base', base), ('-base', -base)):
    print('表值%s 命中控件偏移: %d, 命中css: %d' % (
        delta_name,
        sum(1 for v in vals if v + delta in setc),
        sum(1 for v in vals if v + delta in setcss)))

print('\n表全值:')
for i in range(0, len(vals), 12):
    print('  %3d: %s' % (i, ' '.join('%04X' % v for v in vals[i:i + 12])))

print('\n--- css 区起始 0x%X 处 64B ---' % w['scan_stopped_at'])
o = w['scan_stopped_at']
for i in range(0, 64, 16):
    print('  %04X  %s' % (o + i, ' '.join('%02X' % b for b in raw[o + i:o + i + 16])))

print('\n--- 各 css_off 处 40B（前 4 条控件）---')
for c in ctrls[:4]:
    o = c['css_off']
    print('  %-12s css=0x%X:' % (c['type_name'], o))
    for i in range(0, 40, 16):
        print('      %04X  %s' % (o + i, ' '.join('%02X' % b for b in raw[o + i:o + i + 16])))

print('\n--- 控件负载里的指针字段是否落在 css 区 ---')
lo, hi = w['scan_stopped_at'], w['table_ptr']
for c in ctrls[:6]:
    pl = bytes.fromhex(c['payload'])
    ptrs = [struct.unpack_from('<I', pl, i)[0] for i in range(0, len(pl) - 3, 4)]
    tag = ['%X%s' % (p, '*' if lo <= p < hi else '') for p in ptrs]
    print('  %-12s len=%d css=0x%-5X payload ptrs: %s'
          % (c['type_name'], c['len'], c['css_off'], ' '.join(tag)))
