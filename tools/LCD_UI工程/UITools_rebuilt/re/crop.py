# -*- coding: utf-8 -*-
"""从原厂截图里裁区域放大，并采样配色。"""
import sys
import struct
import zlib


def load_png(path):
    """极简 PNG 读取（只处理 8bit RGB/RGBA 非隔行）。"""
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n'
    pos = 8
    idat = b''
    w = h = bd = ct = None
    while pos < len(d):
        ln = struct.unpack_from('>I', d, pos)[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack_from('>IIBB', data, 0)
        elif typ == b'IDAT':
            idat += data
        elif typ == b'IEND':
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, ch, bytes(out)


def save_png(path, w, h, ch, buf):
    raw = bytearray()
    stride = w * ch
    for y in range(h):
        raw.append(0)
        raw += buf[y * stride:(y + 1) * stride]
    ct = {1: 0, 3: 2, 4: 6}[ch]
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, ct, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def crop_scale(w, h, ch, buf, x0, y0, x1, y1, s):
    cw, chh = x1 - x0, y1 - y0
    ow, oh = cw * s, chh * s
    out = bytearray(ow * oh * ch)
    for y in range(oh):
        sy = y0 + y // s
        for x in range(ow):
            sx = x0 + x // s
            si = (sy * w + sx) * ch
            di = (y * ow + x) * ch
            out[di:di + ch] = buf[si:si + ch]
    return ow, oh, out


if __name__ == '__main__':
    src = sys.argv[1]
    w, h, ch, buf = load_png(src)
    print('图 %dx%d ch=%d' % (w, h, ch))

    def px(x, y):
        i = (y * w + x) * ch
        return tuple(buf[i:i + 3])

    print('取色:')
    for name, (x, y) in {
        '面板绿底': (1550, 700), '工具栏底': (600, 47), '树白底': (120, 700),
        '控件列表底': (390, 200), '画布灰底': (900, 500), '按钮面': (360, 133),
        '窗口标题栏': (400, 15), '属性区绿': (300, 950),
    }.items():
        print('   %-10s (%4d,%4d) = #%02X%02X%02X' % ((name, x, y) + px(x, y)))

    regions = {
        'toolbar': (0, 30, 1000, 62, 2),
        'tree': (0, 82, 268, 300, 3),
        'ctrllist': (268, 80, 510, 385, 3),
        'prop': (268, 390, 510, 1010, 2),
        'pages': (1430, 82, 1690, 340, 3),
        'canvas': (505, 75, 700, 170, 4),
    }
    for name, (x0, y0, x1, y1, s) in regions.items():
        ow, oh, ob = crop_scale(w, h, ch, buf, x0, y0, min(x1, w), min(y1, h), s)
        p = r'C:\bt\crop_%s.png' % name
        save_png(p, ow, oh, ch, ob)
        print('   -> %s  %dx%d' % (p, ow, oh))
