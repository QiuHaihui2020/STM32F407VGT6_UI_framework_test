# -*- coding: utf-8 -*-
"""ID 低 16 位哈希全面搜索：哈希函数 x 输入编码 x 输出折叠。"""
import re
import sys
import zlib

txt = open(sys.argv[1], encoding='utf-8', errors='replace').read()
PAIRS = [(n, int(v, 16)) for n, v in
         re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt) if n != 'UI_VERSION']
TARGET = [(n, v & 0xFFFF) for n, v in PAIRS]
print('样本 %d 条' % len(TARGET))

M32 = 0xFFFFFFFF
M64 = 0xFFFFFFFFFFFFFFFF

# ---------- 输入编码 ----------
ENC = {
    'ascii':      lambda s: s.encode('latin1', 'ignore'),
    'ascii+NUL':  lambda s: s.encode('latin1', 'ignore') + b'\0',
    'lower':      lambda s: s.lower().encode('latin1', 'ignore'),
    'utf16le':    lambda s: s.encode('utf-16-le'),
    'utf16le+NUL': lambda s: s.encode('utf-16-le') + b'\0\0',
    'utf16be':    lambda s: s.encode('utf-16-be'),
    'lower16le':  lambda s: s.lower().encode('utf-16-le'),
}


# ---------- 哈希函数 ----------
def mul_hash(mul, init=0):
    def f(b):
        h = init
        for c in b:
            h = (h * mul + c) & M32
        return h
    return f


def mul_hash64(mul, init=0):
    def f(b):
        h = init
        for c in b:
            h = (h * mul + c) & M64
        return h
    return f


def sdbm(b):
    h = 0
    for c in b:
        h = (c + (h << 6) + (h << 16) - h) & M32
    return h


def fnv1a32(b):
    h = 0x811c9dc5
    for c in b:
        h = ((h ^ c) * 0x01000193) & M32
    return h


def fnv1_32(b):
    h = 0x811c9dc5
    for c in b:
        h = ((h * 0x01000193) & M32) ^ c
    return h


def elf(b):
    h = 0
    for c in b:
        h = ((h << 4) + c) & M32
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & M32
    return h


def murmur2(b, seed=0):
    m = 0x5bd1e995
    r = 24
    ln = len(b)
    h = (seed ^ ln) & M32
    i = 0
    while ln >= 4:
        k = int.from_bytes(b[i:i + 4], 'little')
        k = (k * m) & M32
        k ^= k >> r
        k = (k * m) & M32
        h = (h * m) & M32
        h ^= k
        i += 4
        ln -= 4
    if ln == 3:
        h ^= b[i + 2] << 16
    if ln >= 2:
        h ^= b[i + 1] << 8
    if ln >= 1:
        h ^= b[i]
        h = (h * m) & M32
    h ^= h >> 13
    h = (h * m) & M32
    h ^= h >> 15
    return h


FUNCS = {
    'crc32': lambda b: zlib.crc32(b) & M32,
    'adler32': lambda b: zlib.adler32(b) & M32,
    'sdbm': sdbm,
    'fnv1a32': fnv1a32,
    'fnv1_32': fnv1_32,
    'elf': elf,
    'murmur2': murmur2,
}
# Qt5 的 qHash 内部就是 h = 31*h + byte（作用在 UTF-16 字节上），seed 参与
for mul in (31, 33, 131, 65599, 37, 17, 5381, 1000003):
    for init in (0, 1, 5381, 0x811c9dc5):
        FUNCS['mul%d_i%d' % (mul, init)] = mul_hash(mul, init)
FUNCS['mul31_64'] = mul_hash64(31)
FUNCS['mul131_64'] = mul_hash64(131)

# ---------- 输出折叠 ----------
FOLD = {
    'lo16':      lambda h: h & 0xFFFF,
    'hi16':      lambda h: (h >> 16) & 0xFFFF,
    'xorfold':   lambda h: (h ^ (h >> 16)) & 0xFFFF,
    'sh8':       lambda h: (h >> 8) & 0xFFFF,
    'mod65521':  lambda h: h % 65521,
    'mod65535':  lambda h: h % 65535,
    'mod0xFFFF+1': lambda h: h % 0x10000,
    'lo16^hi16^': lambda h: ((h & 0xFFFF) ^ ((h >> 16) & 0xFFFF)) & 0xFFFF,
}

best = []
for en, ef in ENC.items():
    enc = [(ef(n), t) for n, t in TARGET]
    for fn, f in FUNCS.items():
        try:
            hs = [(f(b), t) for b, t in enc]
        except Exception:
            continue
        for dn, d in FOLD.items():
            hit = sum(1 for h, t in hs if d(h) == t)
            if hit:
                best.append((hit, en, fn, dn))
best.sort(reverse=True)
if best:
    for b in best[:15]:
        print('  命中 %d/%d   编码=%s  函数=%s  折叠=%s' % (b[0], len(TARGET), b[1], b[2], b[3]))
else:
    print('  全部 0 命中')
    n, t = TARGET[0]
    print('  参考：%s -> 目标 0x%04X' % (n, t))
    for en, ef in list(ENC.items())[:3]:
        b = ef(n)
        print('    %-10s bytes=%s' % (en, b[:16].hex()))
        for fn in ('crc32', 'mul31_i0', 'mul131_i0', 'sdbm', 'fnv1a32'):
            print('        %-12s = 0x%08X' % (fn, FUNCS[fn](b)))
