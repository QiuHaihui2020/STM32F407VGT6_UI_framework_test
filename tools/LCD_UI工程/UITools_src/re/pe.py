# -*- coding: utf-8 -*-
"""PE 载入 + VA/文件偏移互转的公共库。"""
import pefile
import struct


class Img:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.secs = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b'\x00').decode('latin1')
            self.secs.append((name,
                              self.base + s.VirtualAddress,
                              max(s.Misc_VirtualSize, s.SizeOfRawData),
                              s.PointerToRawData,
                              s.SizeOfRawData))
        with open(path, 'rb') as f:
            self.raw = f.read()

    def sec(self, name):
        for s in self.secs:
            if s[0].startswith(name[:8]):
                return s
        return None

    def data(self, name):
        s = self.sec(name)
        return self.raw[s[3]:s[3] + s[4]], s[1]

    def va2off(self, va):
        for n, vstart, vsize, poff, psize in self.secs:
            if vstart <= va < vstart + vsize:
                d = va - vstart
                return poff + d if d < psize else None
        return None

    def off2va(self, off):
        for n, vstart, vsize, poff, psize in self.secs:
            if poff <= off < poff + psize:
                return vstart + (off - poff)
        return None

    def u32(self, va):
        o = self.va2off(va)
        if o is None or o + 4 > len(self.raw):
            return None
        return struct.unpack_from('<I', self.raw, o)[0]

    def cstr(self, va, maxlen=512):
        o = self.va2off(va)
        if o is None:
            return None
        e = self.raw.find(b'\x00', o, o + maxlen)
        if e < 0:
            return None
        try:
            return self.raw[o:e].decode('utf-8')
        except UnicodeDecodeError:
            return self.raw[o:e].decode('latin1')
