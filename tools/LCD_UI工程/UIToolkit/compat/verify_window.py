# -*- coding: utf-8 -*-
"""解开页头那 28 字节窗口记录。

推断（并用三页验证）：

    u8  type;          // 窗口类型，= option.ini 里 page/ScenesScreen = 2
    u8  ctrl_num;      // 直接子节点数（图层数）
    u8  css_num;
    u8  len;
    u8  rev[4];        // 00 FF FF FF
    int left, top, width, height;   // 万分比，整页就是 0,0,10000,10000
    u32 layer;         // 页内相对偏移，指向第一个图层控件

合计 28 字节，正好等于实测的窗口记录长度。
对应固件 include/ui/control.h 的 struct window_info（那里 rect 写成 struct rect，
实际落盘是 4 个 int 的万分比矩形）。

用法: python verify_window.py <JL.sty> [工程.json]
"""
import json
import struct
import sys
from sty_dump import parse


def main():
    sty = sys.argv[1]
    raw = open(sty, 'rb').read()
    doc = parse(sty)
    proj = None
    if len(sys.argv) > 2:
        proj = json.load(open(sys.argv[2], encoding='utf-8'))

    ok = bad = 0
    for i, w in enumerate(doc['windows']):
        base = w['offset']
        first = w['controls'][0]['off'] if w['controls'] else base
        rec = raw[base:first]
        print('页%d 窗口记录 %d 字节: %s' % (i, len(rec), rec.hex()))
        if len(rec) != 28:
            print('   !! 不是 28 字节，跳过')
            bad += 1
            continue
        typ, cnum, cssn, ln = rec[0], rec[1], rec[2], rec[3]
        rev = rec[4:8]
        L, T, W, H = struct.unpack_from('<iiii', rec, 8)
        layer = struct.unpack_from('<I', rec, 24)[0]
        print('   type=%d ctrl_num=%d css_num=%d len=%d rev=%s' % (typ, cnum, cssn, ln, rev.hex()))
        print('   rect(万分比)=(%d,%d,%d,%d)   layer -> 页内 0x%X' % (L, T, W, H, layer))

        checks = []
        checks.append(('type==2(page)', typ == 2))
        checks.append(('rev==00FFFFFF', rev == b'\x00\xff\xff\xff'))
        checks.append(('rect==0,0,10000,10000', (L, T, W, H) == (0, 0, 10000, 10000)))
        checks.append(('layer==第一个控件的页内偏移', layer == first - base))
        if w['controls']:
            checks.append(('第一个控件是图层(type 4)', w['controls'][0]['type'] == 4))
        if proj and i < len(proj.get('pages', [])):
            nlayer = len(proj['pages'][i].get('layer', []) or [])
            checks.append(('ctrl_num==json 里的图层数(%d)' % nlayer, cnum == nlayer))
        for name, good in checks:
            print('      %s %s' % ('OK ' if good else 'BAD', name))
            if good:
                ok += 1
            else:
                bad += 1
        print()
    print('一致 %d / 不一致 %d' % (ok, bad))
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
