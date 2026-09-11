# -*- coding: utf-8 -*-
"""验证：控件负载字段 <- 工程 json 的属性。

映射直接对应固件 include/ui/control.h 里的结构体，属性名取自工程 json：

  type 4  NewLayer  : u8 format(=color_format 枚举值) | u8 rev[3] | action | layout
  type 3  NewLayout : action | ctrl
  type 5  List/Grid : u8 page_mode | s8 highlight_index | u16 pad | action | info
  type 8  ImageList : u8 highlight | u8 pad | u16 cent_x | u16 cent_y | u16 pad
                      | normal_img | highlight_img | action
  type 9  Battery   : normal_image | charge_image | action
  type 12 Text      : char source[8] | char code[8] | int color | int hi_color
                      | str | action
  type 10 Time      : char source[8] | u8 auto_cnt | u8 rev[3] | char format[16]
                      | int color | int hi_color | u16 number[10] | u16 delimiter[10]
                      | action
  type 15 Number    : char source[8] | char format[16] | int color | int hi_color
                      | u16 number[10] | u16 delimiter[10] | u16 space[2] | action

颜色字段与 css 同一套编码：ARGB 字符串 -> RGB565 放低 24 位，alpha=A/255*100 放高 8 位。

用法: python verify_payload.py <工程.json> <ename.h> <JL.sty>
"""
import json
import struct
import sys
from collections import Counter
from sty_dump import parse
from verify_css import load_ids, walk, ename_of, argb_to_565

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def props(n):
    """-name -> [属性对象]（同名可能有多个，比如 Text 的两个 color）"""
    d = {}
    for p in n.get('property', []) or []:
        if isinstance(p, dict):
            d.setdefault(p.get('-name'), []).append(p)
    return d


def cstr(b):
    i = b.find(b'\x00')
    return (b if i < 0 else b[:i]).decode('latin1')


def enumv(p):
    d = p.get('default')
    for e in p.get('enum', []) or []:
        if isinstance(e, dict) and d in e:
            return e[d]
    return None


def color_of(p):
    """color 属性可能写在 "color" 或 "background-color" 键里"""
    s = p.get('color', p.get('background-color', ''))
    return argb_to_565(s)


def main():
    proj, enh, sty = sys.argv[1], sys.argv[2], sys.argv[3]
    ids = load_ids(enh)
    doc = json.load(open(proj, encoding='utf-8'))
    styd = parse(sty)

    recs = {}
    for w in styd['windows']:
        for c in w['controls']:
            recs[c['id'] & 0xFFFFFF] = c

    st = Counter()
    notes = []

    def chk(cond, name):
        st[('%s 一致' if cond else '%s 不一致') % name] += 1
        return cond

    for pi, page in enumerate(doc.get('pages', [])):
        for n, par, _ in walk(page, None, page):
            e = ename_of(n)
            if not e or e not in ids:
                continue
            c = recs.get(ids[e] & 0xFFFFFF)
            if not c:
                continue
            pl = bytes.fromhex(c['payload'])
            P = props(n)
            t = c['type']

            if t == 4 and len(pl) >= 4:                       # NewLayer
                if 'color_format' in P:
                    chk(pl[0] == enumv(P['color_format'][0]), 'NewLayer.format')
            elif t == 5 and len(pl) >= 2:                     # List / Grid
                if 'page_mode' in P:
                    v = enumv(P['page_mode'][0])
                    if v is None:
                        v = P['page_mode'][0].get('default', 0)
                    chk(pl[0] == v, 'List.page_mode')
            elif t == 8 and len(pl) >= 8:                     # ImageList / pic
                if 'highlight' in P:
                    chk(pl[0] == P['highlight'][0].get('default', 0), 'Pic.highlight')
                if 'cent_x' in P:
                    chk(U16(pl, 2) == P['cent_x'][0].get('default', 0), 'Pic.cent_x')
                if 'cent_y' in P:
                    chk(U16(pl, 4) == P['cent_y'][0].get('default', 0), 'Pic.cent_y')
            elif t == 12 and len(pl) >= 24:                   # Text
                if 'source' in P:
                    chk(cstr(pl[0:8]) == P['source'][0].get('default', ''), 'Text.source')
                if 'code' in P:
                    chk(cstr(pl[8:16]) == P['code'][0].get('default', ''), 'Text.code')
                if len(P.get('color', [])) >= 1:
                    col, a = color_of(P['color'][0])
                    got = U32(pl, 16)
                    chk(col is not None and (got & 0xFFFFFF) == col and (got >> 24) == a,
                        'Text.color')
                if len(P.get('color', [])) >= 2:
                    col, a = color_of(P['color'][1])
                    got = U32(pl, 20)
                    chk(col is not None and (got & 0xFFFFFF) == col and (got >> 24) == a,
                        'Text.hi_color')
            elif t == 10 and len(pl) >= 76:                   # Time
                if 'source' in P:
                    chk(cstr(pl[0:8]) == P['source'][0].get('default', ''), 'Time.source')
                if 'auto_cnt' in P:
                    chk(pl[8] == enumv(P['auto_cnt'][0]), 'Time.auto_cnt')
                if 'format' in P:
                    chk(cstr(pl[12:28]) == P['format'][0].get('default', ''), 'Time.format')
                    tail = pl[12 + len(P['format'][0].get('default', '')) + 1:28]
                    if tail.strip(b'\x00'):
                        notes.append('Time.format 尾部非零(未初始化): %s  <- %s'
                                     % (tail.hex(), e))
            elif t == 15 and len(pl) >= 76:                   # Number
                if 'source' in P:
                    chk(cstr(pl[0:8]) == P['source'][0].get('default', ''), 'Number.source')
                if 'format' in P:
                    chk(cstr(pl[8:24]) == P['format'][0].get('default', ''), 'Number.format')

    print('负载字段比对：')
    for k in sorted(st):
        print('   %-22s %d' % (k, st[k]))
    bad = sum(v for k, v in st.items() if '不一致' in k)
    if notes:
        print('\n注意（%d 处）：定长字符缓冲区尾部有未初始化内容 ——' % len(notes))
        print('   参考用 strncpy 之类填的，剩余字节是栈上垃圾，')
        print('   这意味着这些控件**无法做到逐字节复现**，只能保证语义一致。')
        for x in notes[:4]:
            print('   ' + x)
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
