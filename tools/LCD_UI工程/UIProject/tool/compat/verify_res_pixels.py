# -*- coding: utf-8 -*-
"""验证：源 BMP -> OSD1 单色竖向分页 == result.bin 里的像素字节。

固件 MONO 读法（lcd_drive/middle/ui_synthesis_oled.c:488）：
    offset = (y / 8) * width + x ;  bit = y % 8 ;  1 = 点亮
所以落盘就是标准 OLED "页" 位图，**行距 = width**。
真实字节数 = width * ceil(height/8)（注意 RES_BMP_T.dwLength 是错的，见
docs/FILE_FORMATS.md 10.3）。

BMP -> 点阵：颜色 != bmp_transparent_color（默认白）就点亮。

用法: python verify_res_pixels.py <工程目录> [result.bin]
"""
import os
import struct
import sys

from res_dump import parse


def load_bmp(path):
    """-> (w, h, bitcount, palette, rows[y][x])；索引图返回调色板下标"""
    b = open(path, 'rb').read()
    assert b[:2] == b'BM', path
    data_off = struct.unpack_from('<I', b, 10)[0]
    hdr = struct.unpack_from('<I', b, 14)[0]
    w, h = struct.unpack_from('<ii', b, 18)
    bitcount = struct.unpack_from('<H', b, 28)[0]
    ncol = struct.unpack_from('<I', b, 46)[0]
    bottom_up = h > 0
    h = abs(h)
    pal_off = 14 + hdr
    pal = None
    if bitcount <= 8:
        n = ncol or (1 << bitcount)
        pal = [struct.unpack_from('<BBBB', b, pal_off + i * 4) for i in range(n)]
    stride = ((w * bitcount + 31) // 32) * 4
    rows = []
    for y in range(h):
        o = data_off + y * stride
        row = []
        for x in range(w):
            if bitcount == 1:
                row.append((b[o + x // 8] >> (7 - x % 8)) & 1)
            elif bitcount == 4:
                row.append((b[o + x // 2] >> (0 if x % 2 else 4)) & 0xF)
            elif bitcount == 8:
                row.append(b[o + x])
            elif bitcount == 24:
                bb, gg, rr = b[o + x * 3:o + x * 3 + 3]
                row.append((rr << 16) | (gg << 8) | bb)
            else:
                bb, gg, rr, _a = b[o + x * 4:o + x * 4 + 4]
                row.append((rr << 16) | (gg << 8) | bb)
        rows.append(row)
    if bottom_up:
        rows.reverse()
    return w, h, bitcount, pal, rows


def to_mono(w, h, bitcount, pal, rows, transparent=0x00FFFFFF):
    out = bytearray(w * ((h + 7) // 8))
    for y in range(h):
        for x in range(w):
            v = rows[y][x]
            if bitcount <= 8:
                b, g, r, _ = pal[v]
                rgb = (r << 16) | (g << 8) | b
            else:
                rgb = v
            if rgb != transparent:
                out[(y // 8) * w + x] |= 1 << (y % 8)
    return bytes(out)


def render(data, w, h):
    return '\n'.join('   ' + ''.join('#' if (data[(y // 8) * w + x] >> (y % 8)) & 1 else '.'
                                     for x in range(w)) for y in range(h))


def load_pic_index(path):
    out, page = {}, -1
    for line in open(path, 'rb'):
        s = line.decode('latin1').rstrip()
        if s.startswith('//PAGE'):
            page = int(s.split()[1])
            out[page] = {}
        else:
            p = s.split()
            if len(p) >= 3 and p[0] == '#define' and p[2].isdigit():
                out.setdefault(page, {})[int(p[2])] = s.split('\\')[-1].strip()
    return out


def main():
    proj = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else '.'
    resbin = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 \
        else os.path.join(proj, 'result.bin')
    pic = os.path.join(proj, 'config', 'pic_lcd')

    d = parse(resbin)
    raw = d['raw']
    idx = load_pic_index(os.path.join(proj, 'result_pic_index.h'))

    total = ok = 0
    firstbad = None
    for p in d['pages']:
        for it in p['items']:
            fn = idx.get(p['num'], {}).get(it['id'])
            if not fn:
                continue
            path = os.path.join(pic, fn)
            if not os.path.exists(path):
                continue
            total += 1
            w, h, bc, pal, rows = load_bmp(path)
            got = raw[it['off']:it['off'] + it['w'] * ((it['h'] + 7) // 8)]
            if (w, h) != (it['w'], it['h']):
                if firstbad is None:
                    firstbad = (fn, '尺寸 bmp=%dx%d res=%dx%d' % (w, h, it['w'], it['h']))
                continue
            exp = to_mono(w, h, bc, pal, rows)
            if exp == got:
                ok += 1
            elif firstbad is None:
                firstbad = (fn, exp, got, w, h)

    print('BMP -> .res 像素一致: %d / %d' % (ok, total))
    if firstbad and len(firstbad) == 5:
        fn, exp, got, w, h = firstbad
        print('\n首个不一致: %s' % fn)
        print('BMP 转换出来:')
        print(render(exp, w, h))
        print('.res 里的:')
        print(render(got, w, h))
    elif firstbad:
        print('首个问题:', firstbad)
    return 0 if (total and ok == total) else 1


if __name__ == '__main__':
    sys.exit(main())
