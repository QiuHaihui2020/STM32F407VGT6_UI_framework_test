# -*- coding: utf-8 -*-
"""换个输入再试：用工程 json 里的中文 -name / caption / 类型串做哈希输入。"""
import json
import re
import sys

ename_h, proj = sys.argv[1], sys.argv[2]
txt = open(ename_h, encoding='utf-8', errors='replace').read()
IDS = {n: int(v, 16) for n, v in
       re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt) if n != 'UI_VERSION'}

doc = json.load(open(proj, encoding='utf-8'))
rows = []


def walk(node, path):
    if isinstance(node, dict):
        ename = None
        for p in node.get('property', []) or []:
            if isinstance(p, dict) and p.get('ename'):
                ename = p['ename']
        if ename:
            rows.append({'ename': ename, 'name': node.get('-name', ''),
                         'type': node.get('-type', ''), 'cls': node.get('-class', ''),
                         'caption': node.get('caption', ''), 'path': path})
        for k, v in node.items():
            walk(v, path + '/' + str(k))
    elif isinstance(node, list):
        for i, v in enumerate(node):
            walk(v, path)


walk(doc, '')
rows = [r for r in rows if r['ename'] in IDS]
print('json 中带 ename 且能对上 ename.h 的节点: %d' % len(rows))
for r in rows[:8]:
    v = IDS[r['ename']]
    print('  %-22s id=0x%06X  type=%-14s cls=%-10s name=%r' %
          (r['ename'], v, r['type'], r['cls'], r['name']))

M = 0xFFFFFFFF


def djb2(b):
    h = 5381
    for c in b:
        h = (h * 33 + c) & M
    return h


def bkdr(b, s=131):
    h = 0
    for c in b:
        h = (h * s + c) & M
    return h


def sdbm(b):
    h = 0
    for c in b:
        h = (c + (h << 6) + (h << 16) - h) & M
    return h


import zlib
FN = {'djb2': djb2, 'bkdr131': bkdr, 'bkdr31': lambda b: bkdr(b, 31),
      'sdbm': sdbm, 'crc32': lambda b: zlib.crc32(b) & M}
SRC = {
    'name-utf8': lambda r: r['name'].encode('utf-8'),
    'name-gbk': lambda r: r['name'].encode('gbk', 'ignore'),
    'ename': lambda r: r['ename'].encode(),
    'ename-lower': lambda r: r['ename'].lower().encode(),
    'caption-utf8': lambda r: r['caption'].encode('utf-8'),
    'type': lambda r: r['type'].encode(),
}
for sn, sf in SRC.items():
    for fn, f in FN.items():
        hit = sum(1 for r in rows if (f(sf(r)) & 0xFFFF) == (IDS[r['ename']] & 0xFFFF))
        hit24 = sum(1 for r in rows if (f(sf(r)) & 0xFFFFFF) == IDS[r['ename']])
        if hit or hit24:
            print('  %-14s %-8s low16 命中 %d/%d, 全 24 位命中 %d'
                  % (sn, fn, hit, len(rows), hit24))
print('（无输出即全部 0 命中）')
