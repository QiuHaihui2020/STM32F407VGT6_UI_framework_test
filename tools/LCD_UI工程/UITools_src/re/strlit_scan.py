# -*- coding: utf-8 -*-
"""扫描 Qt5 QStringLiteral 静态数据。

QStringData = QArrayData { int ref; int size; uint alloc:31,cr:1; qptrdiff offset; }
后接 size 个 UTF-16 码元 + 一个 0 终止符。offset 相对于该头自身地址。
"""
import struct
import sys
import json
from pe import Img

HDR = 16


def scan(img):
    out = []
    for secname in ('.rdata', '.data'):
        s = img.sec(secname)
        if not s:
            continue
        _, vstart, vsize, poff, psize = s
        raw = img.raw
        for i in range(poff, poff + psize - HDR, 4):
            ref, size, alloc, off = struct.unpack_from('<iIIi', raw, i)
            if ref != -1 or alloc != 0 or not (0 < size < 8192):
                continue
            va = vstart + (i - poff)
            to = img.va2off(va + off)
            if to is None:
                continue
            nbytes = size * 2
            if to + nbytes + 2 > len(raw):
                continue
            if raw[to + nbytes:to + nbytes + 2] != b'\x00\x00':
                continue
            body = raw[to:to + nbytes]
            try:
                txt = body.decode('utf-16-le')
            except UnicodeDecodeError:
                continue
            if '\x00' in txt:
                continue
            # 过滤掉一眼不是文本的
            if not txt.strip():
                continue
            bad = sum(1 for ch in txt if ord(ch) < 0x20 and ch not in '\t\n\r')
            if bad:
                continue
            out.append((va, txt))
    return out


if __name__ == '__main__':
    img = Img(sys.argv[1])
    lits = scan(img)
    print('QStringLiteral count:', len(lits))
    with open(sys.argv[2], 'w', encoding='utf-8') as f:
        for va, t in lits:
            f.write('0x%08x\t%s\n' % (va, t.replace('\n', '\\n').replace('\t', '\\t')))
