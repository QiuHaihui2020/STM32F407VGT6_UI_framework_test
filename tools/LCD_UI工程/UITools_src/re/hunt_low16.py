# -*- coding: utf-8 -*-
"""低 16 位到底从哪来：在所有中间产物里找这些数值。"""
import re
import os
import sys
import struct

PDIR = sys.argv[1]
en = open(os.path.join(PDIR, 'ename.h'), encoding='utf-8', errors='replace').read()
PAIRS = [(n, int(v, 16)) for n, v in
         re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', en) if n != 'UI_VERSION']
low = [(n, v & 0xFFFF) for n, v in PAIRS]
print('样本 %d 条，例：%s' % (len(low), low[:4]))

files = ['Resbuilder.xml', 'result.xml', 'result.csv', 'result.h', 'result_pic_index.h',
         'result_str_index.h', 'imagelist.txt', 'qtread.csv', 'debug.txt',
         'SmallColorTFT.json', 'uitoolbin.bin', 'Resbuilder.dat', 'version.txt']

print('\n[1] 低16位是否以十进制/十六进制文本出现在各文件里（抽 20 个样本）')
sample = low[:20]
for f in files:
    p = os.path.join(PDIR, f)
    if not os.path.exists(p):
        continue
    try:
        txt = open(p, encoding='utf-8', errors='replace').read()
    except Exception:
        txt = ''
    dec = sum(1 for _, v in sample if re.search(r'\b%d\b' % v, txt))
    hexa = sum(1 for _, v in sample if re.search(r'\b%04x\b' % v, txt, re.I))
    print('   %-22s 十进制命中 %2d/20   十六进制命中 %2d/20' % (f, dec, hexa))

print('\n[2] 低16位是否以小端 u16 出现在二进制里')
for f in ('project.bin', 'result.bin', 'result.str', 'Resbuilder.dat', 'uitoolbin.bin'):
    p = os.path.join(PDIR, f)
    if not os.path.exists(p):
        continue
    b = open(p, 'rb').read()
    hit = sum(1 for _, v in sample if struct.pack('<H', v) in b)
    print('   %-22s %2d/20' % (f, hit))

print('\n[3] uitoolbin.bin 里 id 字段是否非零')
try:
    import json
    d = json.load(open(os.path.join(PDIR, 'uitoolbin.bin'), encoding='utf-8'))
    ids = []

    def walk(n):
        if isinstance(n, dict):
            for p in n.get('property', []) or []:
                if isinstance(p, dict) and p.get('-name') == 'id':
                    ids.append((p.get('ename'), p.get('id')))
            for v in n.values():
                walk(v)
        elif isinstance(n, list):
            for v in n:
                walk(v)
    walk(d)
    nz = [x for x in ids if x[1]]
    print('   id 项 %d 个，非零 %d 个；前几个: %s' % (len(ids), len(nz), ids[:6]))
except Exception as e:
    print('   解析失败:', e)

print('\n[4] ename.h 里 id 的排列有无规律（按出现顺序看低16位）')
for n, v in PAIRS[:12]:
    print('   %-24s 0x%06X  page=%d type=%2d low=0x%04X (%5d)'
          % (n, v, (v >> 22) & 3, (v >> 16) & 0x3F, v & 0xFFFF, v & 0xFFFF))
