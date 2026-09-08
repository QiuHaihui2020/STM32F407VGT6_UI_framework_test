# -*- coding: utf-8 -*-
"""按 VA 反汇编一段，并标注常量引用到的字符串。"""
import struct
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PATH = sys.argv[1]
START = int(sys.argv[2], 16)
LEN = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x200

pe = pefile.PE(PATH, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
raw = open(PATH, 'rb').read()
secs = []
for s in pe.sections:
    secs.append((s.Name.rstrip(b'\0').decode('latin1'), base + s.VirtualAddress,
                 max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData))


def va2off(va):
    for n, vs, vl, po, ps in secs:
        if vs <= va < vs + vl:
            d = va - vs
            return po + d if d < ps else None
    return None


def cstr(va, maxlen=64):
    o = va2off(va)
    if o is None:
        return None
    e = raw.find(b'\x00', o, o + maxlen)
    if e < 0:
        return None
    s = raw[o:e]
    if not s or any(c < 9 or (13 < c < 32) for c in s):
        return None
    try:
        return s.decode('utf-8')
    except UnicodeDecodeError:
        return s.decode('latin1')


off = va2off(START)
code = raw[off:off + LEN]
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
for ins in md.disasm(code, START):
    note = ''
    for tok in ins.op_str.replace(',', ' ').replace('[', ' ').replace(']', ' ').split():
        if tok.startswith('0x'):
            try:
                v = int(tok, 16)
            except ValueError:
                continue
            s = cstr(v)
            if s:
                note = '   ; "%s"' % s.replace('\n', '\\n').replace('\r', '\\r')
                break
    print('%08X  %-24s %-34s%s' % (ins.address, ins.bytes.hex(), ins.mnemonic + ' ' + ins.op_str, note))
