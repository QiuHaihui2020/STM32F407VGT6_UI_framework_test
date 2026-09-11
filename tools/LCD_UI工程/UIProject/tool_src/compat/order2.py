# -*- coding: utf-8 -*-
"""从文件里直接统计：每类控件的数据块按地址升序是什么顺序，组是怎么排的。"""
import struct
import sys
from collections import defaultdict, Counter
from sty_dump import parse
from sty_blocks import PTRS, NONE

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]

raw = open(sys.argv[1], 'rb').read()
doc = parse(sys.argv[1])
w = doc['windows'][0]
base = w['offset']

seqs = defaultdict(Counter)
allblocks = []          # (rel, size, 控件序号, 字段名)
for ci, c in enumerate(w['controls']):
    pl = bytes.fromhex(c['payload'])
    rec = bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                 0xFF, 0xFF, 0xFF]) + struct.pack('<iI', c['id'], c['css_off']) + pl
    items = []
    for name, off in PTRS.get(c['type'], [('css', 12)]):
        if off + 4 > len(rec) or name in ('ctrl', 'layout', 'info'):
            continue
        pv = U32(rec, off)
        if pv in NONE:
            continue
        rel = pv & 0xFFFF
        if name == 'css':
            sz = 36
        else:
            sz = 2 + 2 * U16(raw, base + rel) if name != 'action' else None
        items.append((rel, name, sz))
        allblocks.append((rel, sz, ci, name))
    items.sort()
    seqs[c['type']][tuple(n for _, n, _ in items)] += 1

print('各类型控件的块「地址升序」字段顺序：')
for t in sorted(seqs):
    for seq, n in seqs[t].most_common():
        print('   type=%-3d ×%-3d  %s' % (t, n, ' < '.join(seq)))

allblocks.sort()
print('\n数据区里所有块按地址升序（前 24 个）：rel  控件#  字段')
for rel, sz, ci, name in allblocks[:24]:
    print('   0x%-5X  ctrl%-3d %s' % (rel, ci, name))
print('...')
print('后 12 个：')
for rel, sz, ci, name in allblocks[-12:]:
    print('   0x%-5X  ctrl%-3d %s' % (rel, ci, name))

print('\n控件序号随地址升序的走向（看是不是倒序排的）：')
order = [ci for _, _, ci, _ in allblocks]
print('   前 30:', order[:30])
