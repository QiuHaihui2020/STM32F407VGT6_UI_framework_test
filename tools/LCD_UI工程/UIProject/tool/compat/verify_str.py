# -*- coding: utf-8 -*-
"""验证：GDI(宋体 -16) 渲染出来的点阵 == result.str 里的字节。

ResBuilder 是 Windows 程序，字符串图是 GDI 光栅化的
（Resbuilder.xml 里存的就是整套 LOGFONT 字段）。在同一台机器上用同样的
LOGFONT + 单色 DIB + TextOutW，就能得到一模一样的点阵。

两个坑：
  1) **不要 trim**。有 5 条英文末尾带空格，宽度要多 8 px。
  2) 清底不能用 PatBlt(WHITENESS/BLACKNESS)：单色 DIB 下它按物理调色板索引填，
     方向和自定义调色板相反，留白会变成 1。直接 memset 位图内存。

宽度 = GetTextExtentPoint32W(text).cx 向上取整到 8 的倍数，高度 = |lfHeight|。

用法: python verify_str.py <工程目录> [result.str]
"""
import ctypes as C
import os
import sys

from res_dump import parse

gdi = C.windll.gdi32
LF_FACESIZE = 32


class LOGFONTW(C.Structure):
    _fields_ = [('lfHeight', C.c_long), ('lfWidth', C.c_long),
                ('lfEscapement', C.c_long), ('lfOrientation', C.c_long),
                ('lfWeight', C.c_long), ('lfItalic', C.c_byte),
                ('lfUnderline', C.c_byte), ('lfStrikeOut', C.c_byte),
                ('lfCharSet', C.c_byte), ('lfOutPrecision', C.c_byte),
                ('lfClipPrecision', C.c_byte), ('lfQuality', C.c_byte),
                ('lfPitchAndFamily', C.c_byte), ('lfFaceName', C.c_wchar * LF_FACESIZE)]


class BITMAPINFOHEADER(C.Structure):
    _fields_ = [('biSize', C.c_uint32), ('biWidth', C.c_long), ('biHeight', C.c_long),
                ('biPlanes', C.c_uint16), ('biBitCount', C.c_uint16),
                ('biCompression', C.c_uint32), ('biSizeImage', C.c_uint32),
                ('biXPelsPerMeter', C.c_long), ('biYPelsPerMeter', C.c_long),
                ('biClrUsed', C.c_uint32), ('biClrImportant', C.c_uint32)]


class BITMAPINFO1(C.Structure):
    _fields_ = [('h', BITMAPINFOHEADER), ('pal', C.c_uint32 * 2)]


class SIZE(C.Structure):
    _fields_ = [('cx', C.c_long), ('cy', C.c_long)]


def make_font(face='宋体', height=-16, weight=400, charset=134, quality=0):
    lf = LOGFONTW()
    lf.lfHeight = height
    lf.lfWeight = weight
    lf.lfCharSet = charset
    lf.lfQuality = quality
    lf.lfPitchAndFamily = 2
    lf.lfFaceName = face
    return gdi.CreateFontIndirectW(C.byref(lf))


def extent(text, hf):
    hdc = gdi.CreateCompatibleDC(None)
    old = gdi.SelectObject(hdc, hf)
    sz = SIZE()
    gdi.GetTextExtentPoint32W(hdc, text, len(text), C.byref(sz))
    gdi.SelectObject(hdc, old)
    gdi.DeleteDC(hdc)
    return sz.cx


def rasterize(text, hf, w, h=16):
    hdc = gdi.CreateCompatibleDC(None)
    oldf = gdi.SelectObject(hdc, hf)
    bi = BITMAPINFO1()
    bi.h.biSize = C.sizeof(BITMAPINFOHEADER)
    bi.h.biWidth = w
    bi.h.biHeight = -h                      # top-down
    bi.h.biPlanes = 1
    bi.h.biBitCount = 1
    bi.pal[0] = 0x00FFFFFF                  # 背景白 -> bit 0
    bi.pal[1] = 0x00000000                  # 前景黑 -> bit 1
    bits = C.c_void_p()
    hbm = gdi.CreateDIBSection(hdc, C.byref(bi), 0, C.byref(bits), None, 0)
    oldbm = gdi.SelectObject(hdc, hbm)
    stride = (w + 31) // 32 * 4
    C.memset(bits, 0, stride * h)
    gdi.SetBkMode(hdc, 2)                   # OPAQUE
    gdi.SetBkColor(hdc, 0x00FFFFFF)
    gdi.SetTextColor(hdc, 0x00000000)
    if text:
        gdi.TextOutW(hdc, 0, 0, text, len(text))
    gdi.GdiFlush()
    buf = (C.c_ubyte * (stride * h)).from_address(bits.value)
    out = bytearray(w * ((h + 7) // 8))
    for y in range(h):
        for x in range(w):
            if (buf[y * stride + x // 8] >> (7 - x % 8)) & 1:
                out[(y // 8) * w + x] |= 1 << (y % 8)
    gdi.SelectObject(hdc, oldbm)
    gdi.DeleteObject(hbm)
    gdi.SelectObject(hdc, oldf)
    gdi.DeleteDC(hdc)
    return bytes(out)


def show(data, w, h=16):
    return '\n'.join('   ' + ''.join('#' if (data[(y // 8) * w + x] >> (y % 8)) & 1 else '.'
                                     for x in range(w)) for y in range(h))


def main():
    proj = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else '.'
    strfile = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 \
        else os.path.join(proj, 'result.str')

    # result.csv 是 UTF-16LE，字段分隔 ",\t"，**不要 strip 单元格**
    txt = open(os.path.join(proj, 'result.csv'), 'rb').read().decode('utf-16')
    lines = [l for l in txt.splitlines() if l.strip()]
    hdr = [c.strip() for c in lines[0].split(',\t')]
    tab = {}
    for l in lines[1:]:
        c = l.split(',\t')
        tab[c[0].strip().lower()] = c[1:]

    id2name = {}
    for line in open(os.path.join(proj, 'result_str_index.h'), 'rb'):
        p = line.decode('latin1').split()
        if len(p) >= 3 and p[0] == '#define' and p[2].isdigit():
            id2name[int(p[2])] = p[1]

    d = parse(strfile)
    raw = d['raw']
    e = d['pages'][0]['item_entry']
    items = d['pages'][0]['items']
    per = e['wCount'] // e['langsum']
    langs = [i for i in range(32) if e['language'] >> i & 1]
    print('语言掩码 0x%X -> %s' % (e['language'], [hdr[1 + i] for i in langs]))

    hf = make_font()
    ok = bad = 0
    bads = []
    wrule_ok = 0
    for li, lang in enumerate(langs):
        for k in range(per):
            it = items[li * per + k]
            want = raw[it['off']:it['off'] + it['w'] * ((it['h'] + 7) // 8)]
            text = tab[id2name[k + 1].lower()][lang]
            cx = extent(text, hf) if text else 0
            if it['w'] == (cx + 7) // 8 * 8:
                wrule_ok += 1
            got = rasterize(text, hf, it['w'], it['h'])
            if got == want:
                ok += 1
            else:
                bad += 1
                bads.append((lang, k + 1, text, it, want, got))

    print('宽度 == ceil8(GetTextExtentPoint32) : %d / %d' % (wrule_ok, e['wCount']))
    print('点阵逐字节一致 : %d / %d' % (ok, e['wCount']))
    for lang, rid, text, it, want, got in bads[:2]:
        print('\n--- 语言列%d id=%d %r %dx%d ---' % (lang, rid, text, it['w'], it['h']))
        print('.str:')
        print(show(want, it['w']))
        print('GDI :')
        print(show(got, it['w']))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
