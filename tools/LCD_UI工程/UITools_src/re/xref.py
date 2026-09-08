# -*- coding: utf-8 -*-
"""找 .text 里对某个 VA 常量的所有引用。"""
import struct
import sys
import pefile

PATH = sys.argv[1]
VA = int(sys.argv[2], 16)
pe = pefile.PE(PATH, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
raw = open(PATH, 'rb').read()
secs = [(s.Name.rstrip(b'\0').decode('latin1'), base + s.VirtualAddress,
         max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData)
        for s in pe.sections]
text = [s for s in secs if s[0] == '.text'][0]
va0, off0, size = text[1], text[3], text[4]
pat = struct.pack('<I', VA)
hits = []
i = off0
end = off0 + size
while True:
    j = raw.find(pat, i, end)
    if j < 0:
        break
    hits.append(va0 + (j - off0))
    i = j + 1
print('对 0x%08X 的引用 %d 处:' % (VA, len(hits)))
for h in hits:
    print('   0x%08X' % h)
