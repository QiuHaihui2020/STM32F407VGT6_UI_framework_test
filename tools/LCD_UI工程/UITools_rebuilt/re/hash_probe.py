# -*- coding: utf-8 -*-
"""反推 ename.h 里控件 ID 的哈希算法。"""
import re
import sys

txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
pairs = re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt)
pairs = [(n, int(v, 16)) for n, v in pairs if n != 'UI_VERSION']
print('样本 %d 条, 值域 0x%x..0x%x' % (len(pairs), min(v for _, v in pairs), max(v for _, v in pairs)))

M32 = 0xFFFFFFFF


def djb2(s, init=5381, mul=33, xor=False):
    h = init
    for c in s.encode('utf-8'):
        h = ((h * mul) & M32) ^ c if xor else ((h * mul) + c) & M32
    return h & M32


def sdbm(s):
    h = 0
    for c in s.encode('utf-8'):
        h = (c + (h << 6) + (h << 16) - h) & M32
    return h


def bkdr(s, seed=131):
    h = 0
    for c in s.encode('utf-8'):
        h = (h * seed + c) & M32
    return h


def fnv1a(s):
    h = 0x811c9dc5
    for c in s.encode('utf-8'):
        h = ((h ^ c) * 0x01000193) & M32
    return h


def ap(s):
    h = 0
    for i, c in enumerate(s.encode('utf-8')):
        if i & 1:
            h ^= (~((h << 11) ^ c ^ (h >> 5))) & M32
        else:
            h ^= ((h << 7) ^ c ^ (h >> 3)) & M32
    return h & M32


FUNCS = {
    'djb2': djb2,
    'djb2x': lambda s: djb2(s, xor=True),
    'sdbm': sdbm,
    'bkdr131': bkdr,
    'bkdr31': lambda s: bkdr(s, 31),
    'bkdr1313': lambda s: bkdr(s, 1313),
    'fnv1a': fnv1a,
    'ap': ap,
}
CASES = {
    'as-is': lambda n: n,
    'lower': lambda n: n.lower(),
}
MASKS = [0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF, 0xFFFFFFFF]

best = []
for fn, f in FUNCS.items():
    for cn, cf in CASES.items():
        for m in MASKS:
            hit = sum(1 for n, v in pairs if (f(cf(n)) & m) == v)
            if hit:
                best.append((hit, fn, cn, hex(m)))
best.sort(reverse=True)
for b in best[:10]:
    print('命中 %d/%d  %s  %s  mask=%s' % (b[0], len(pairs), b[1], b[2], b[3]))
if not best:
    print('以上算法全部 0 命中')
    for n, v in pairs[:6]:
        print('  %-24s 0x%06X   djb2=0x%08x sdbm=0x%08x bkdr=0x%08x'
              % (n, v, djb2(n), sdbm(n), bkdr(n)))
