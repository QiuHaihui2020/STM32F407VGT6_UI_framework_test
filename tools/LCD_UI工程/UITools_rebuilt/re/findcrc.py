# -*- coding: utf-8 -*-
"""在二进制里找 256 项 CRC 查表，并反推多项式。

CRC 表的特征：table[0]==0，且 table[i] 可由 table[1] 和 i 的位组合出来
（GF(2) 线性）。找到就能直接算出多项式。
"""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
print('文件 %s  %d 字节' % (sys.argv[1], len(data)))


def check_table(off, width, byteorder='<'):
    fmt = byteorder + ('H' if width == 2 else 'I')
    step = width
    try:
        t = [struct.unpack_from(fmt, data, off + i * step)[0] for i in range(256)]
    except Exception:
        return None
    if t[0] != 0:
        return None
    if len(set(t)) < 200:          # CRC 表基本互不相同
        return None
    # GF(2) 线性性：t[i ^ j] == t[i] ^ t[j] 对 2 的幂成立
    basis = [t[1 << k] for k in range(8)]
    for i in range(2, 256):
        v = 0
        for k in range(8):
            if i >> k & 1:
                v ^= basis[k]
        if v != t[i]:
            return None
    return t


def poly_from_table(t, width):
    """MSB-first 表: t[1] = poly（宽度对齐后）；LSB-first 表: t[0x80] = poly"""
    return {'t1': t[1], 't80': t[0x80], 't128_rev': t[1]}


found = []
# 只在只读段里扫，步长 2（表通常 4/8 字节对齐，但保险起见）
for width in (2, 4):
    step = 4
    i = 0
    n = len(data) - 256 * width
    while i < n:
        if data[i:i + width] == b'\x00' * width:
            t = check_table(i, width)
            if t:
                found.append((i, width, t))
                i += 256 * width
                continue
        i += step
print('找到线性查表 %d 张' % len(found))
for off, width, t in found:
    p = poly_from_table(t, width)
    print('  @0x%08X  宽度=%dB  t[1]=0x%0*X  t[0x80]=0x%0*X'
          % (off, width, width * 2, p['t1'], width * 2, p['t80']))
    print('       前 8 项:', ' '.join('%0*X' % (width * 2, v) for v in t[:8]))
