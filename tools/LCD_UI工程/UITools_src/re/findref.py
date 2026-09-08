# -*- coding: utf-8 -*-
"""找字符串 -> 找引用它的代码 -> 反汇编附近。"""
import struct
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_MODE_64

PATH = sys.argv[1]
NEEDLES = sys.argv[2:] or ['ename.h', 'UI_TOOL_ENAME', '#define', 'UI_VERSION']

pe = pefile.PE(PATH, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
is64 = pe.FILE_HEADER.Machine == 0x8664
raw = open(PATH, 'rb').read()
secs = []
for s in pe.sections:
    nm = s.Name.rstrip(b'\0').decode('latin1')
    secs.append((nm, base + s.VirtualAddress,
                 max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData))
print('%s  %s  ImageBase=0x%X' % (PATH, 'x64' if is64 else 'x86', base))
for s in secs:
    print('   %-10s VA 0x%08X len 0x%X  file 0x%X' % (s[0], s[1], s[2], s[3]))


def off2va(off):
    for n, va, vs, po, ps in secs:
        if po <= off < po + ps:
            return va + (off - po)
    return None


def va2off(va):
    for n, vs, vl, po, ps in secs:
        if vs <= va < vs + vl:
            d = va - vs
            return po + d if d < ps else None
    return None


text = None
for n, va, vl, po, ps in secs:
    if n.startswith('.text'):
        text = (va, po, ps)
print()
for needle in NEEDLES:
    pat = needle.encode('latin1')
    hits = []
    i = 0
    while True:
        j = raw.find(pat, i)
        if j < 0:
            break
        hits.append(j)
        i = j + 1
        if len(hits) > 40:
            break
    print('=== %r 出现 %d 次 ===' % (needle, len(hits)))
    for j in hits[:12]:
        va = off2va(j)
        ctx = raw[max(0, j - 24):j + 40]
        printable = ''.join(chr(c) if 32 <= c < 127 else '.' for c in ctx)
        print('   off 0x%08X va %s  ctx: %s' % (j, ('0x%08X' % va) if va else '-', printable))
        if va is None:
            continue
        # 在 .text 里找 4 字节地址引用
        addr = struct.pack('<I', va) if not is64 else None
        refs = []
        if addr:
            k = text[1]
            end = text[1] + text[2]
            while True:
                m = raw.find(addr, k, end)
                if m < 0:
                    break
                refs.append(off2va(m))
                k = m + 1
                if len(refs) > 8:
                    break
        if refs:
            print('        被引用 @ ' + ', '.join('0x%08X' % r for r in refs))
