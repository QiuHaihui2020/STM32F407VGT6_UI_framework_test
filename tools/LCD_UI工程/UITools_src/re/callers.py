# -*- coding: utf-8 -*-
"""找某个函数的所有调用点，并反汇编调用前的一段。"""
import struct
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PATH = sys.argv[1]
TARGET = int(sys.argv[2], 16)
BACK = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x70

pe = pefile.PE(PATH, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
raw = open(PATH, 'rb').read()
secs = []
for s in pe.sections:
    secs.append((s.Name.rstrip(b'\0').decode('latin1'), base + s.VirtualAddress,
                 max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData))
text = [s for s in secs if s[0] == '.text'][0]
va0, off0, size = text[1], text[3], text[4]


def va2off(va):
    for n, vs, vl, po, ps in secs:
        if vs <= va < vs + vl:
            d = va - vs
            return po + d if d < ps else None
    return None


def cstr(va, maxlen=48):
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
        return None


calls = []
i = off0
end = off0 + size
while i < end - 5:
    if raw[i] == 0xE8:
        rel = struct.unpack_from('<i', raw, i + 1)[0]
        site = va0 + (i - off0)
        if site + 5 + rel == TARGET:
            calls.append(site)
        i += 1
        continue
    i += 1
print('0x%08X 的调用点 %d 处: %s' % (TARGET, len(calls), ', '.join('0x%08X' % c for c in calls)))

md = Cs(CS_ARCH_X86, CS_MODE_32)
for site in calls[:8]:
    o = va2off(site) - BACK
    print('\n========== 调用点 0x%08X ==========' % site)
    for ins in md.disasm(raw[o:o + BACK + 0x18], site - BACK):
        note = ''
        for tok in ins.op_str.replace(',', ' ').replace('[', ' ').replace(']', ' ').split():
            if tok.startswith('0x'):
                try:
                    v = int(tok, 16)
                except ValueError:
                    continue
                s = cstr(v)
                if s:
                    note = '   ; "%s"' % s.replace('\n', '\\n')
                    break
        mark = ' <<< CALL' if ins.address == site else ''
        print('  %08X  %-34s%s%s' % (ins.address, ins.mnemonic + ' ' + ins.op_str, note, mark))
