# -*- coding: utf-8 -*-
"""在 .text 里找 id 拼装的特征：shl r32, 22 / shl r32, 16 / shl r32, 29。"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

PATH = sys.argv[1]
pe = pefile.PE(PATH, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
raw = open(PATH, 'rb').read()
text = None
for s in pe.sections:
    if s.Name.rstrip(b'\0') == b'.text':
        text = (base + s.VirtualAddress, s.PointerToRawData, s.SizeOfRawData)
va0, off0, size = text
print('.text VA 0x%08X  %d 字节' % (va0, size))

# shl r32, imm8 = C1 /4 ib ；ModRM 的 reg 位 = 4 -> E0..E7
targets = {22: [], 29: [], 16: []}
i = off0
end = off0 + size
while i < end - 3:
    if raw[i] == 0xC1 and 0xE0 <= raw[i + 1] <= 0xE7:
        imm = raw[i + 2]
        if imm in targets:
            targets[imm].append(va0 + (i - off0))
        i += 3
        continue
    i += 1
for k in (22, 29):
    print('  shl r32,%d 出现 %d 处: %s' % (k, len(targets[k]),
          ', '.join('0x%08X' % v for v in targets[k][:20])))
print('  shl r32,16 出现 %d 处（太常见，仅计数）' % len(targets[16]))

md = Cs(CS_ARCH_X86, CS_MODE_32)


def show(va, back=0x40, fwd=0x60):
    o = off0 + (va - va0) - back
    print('\n----- 0x%08X 附近 -----' % va)
    for ins in md.disasm(raw[o:o + back + fwd], va - back):
        mark = ' <<<' if ins.address == va else ''
        print('  %08X  %-28s%s' % (ins.address, ins.mnemonic + ' ' + ins.op_str, mark))


for va in targets[22][:6]:
    show(va)
