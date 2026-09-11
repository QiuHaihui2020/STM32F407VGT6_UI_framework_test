# -*- coding: utf-8 -*-
"""推导并验证：工程 json 的 element_css.struct[N] -> .sty 里的 element_css1 36 字节。

思路：
  ename.h    给出 ename -> id
  JL.sty     给出 id    -> css 块字节
  工程 json  给出 ename -> 该控件的 css 属性（以及父节点，用于坐标换算）
三边对齐后逐字段比对。

element_css1（固件 include/ui/ui_core.h，36 B）：
  +0  u8  align            +1 u8 invisible      +2 u8 z_order     +3 u8 rev
  +4  int left             +8 int top           +12 int width     +16 int height
  +20 u32 background_color:24 | alpha:8
  +24 int background_image:24 | image_quadrant:8
  +28 u8 border.left/top/right/bottom
  +32 int border.color:24 | ?:8

用法: python verify_css.py <工程.json> <ename.h> <JL.sty>
"""
import json
import re
import struct
import sys
from collections import Counter
from sty_dump import parse
from sty_blocks import PTRS, NONE

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
I32 = lambda b, o: struct.unpack_from('<i', b, o)[0]


def load_ids(path):
    txt = open(path, encoding='utf-8', errors='replace').read()
    return {n: int(v, 16) for n, v in
            re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)', txt)}


def walk(node, parent=None, page=None):
    """产出 (节点, 父节点, 页节点)"""
    yield node, parent, page
    for k in ('layer', 'layout', 'widget', 'listwidget'):
        for c in node.get(k, []) or []:
            for x in walk(c, node, page):
                yield x


def ename_of(n):
    for p in n.get('property', []) or []:
        if isinstance(p, dict) and p.get('-name') == 'id':
            return p.get('ename')
    return None


def css_struct(n, state=0):
    for p in n.get('property', []) or []:
        if isinstance(p, dict) and p.get('-name') == 'element_css':
            st = p.get('struct') or []
            if state < len(st):
                return {q.get('-name'): q for q in st[state] if isinstance(q, dict)}
    return {}


def rect_of(n, state=0):
    c = css_struct(n, state).get('rect')
    if c:
        r = c.get('rect') or {}
        return (r.get('x', 0), r.get('y', 0), r.get('width', 0), r.get('height', 0))
    # 页节点：property[0] 是裸 rect
    for p in n.get('property', []) or []:
        if isinstance(p, dict) and 'rect' in p and '-name' not in p:
            r = p['rect']
            return (r.get('x', 0), r.get('y', 0), r.get('width', 0), r.get('height', 0))
    return None


def argb_to_565(s):
    """json 里的颜色是 "#AARRGGBB"；空串表示"不设置"。
    实测：#ff0000ff -> 0x001F(RGB565 蓝)，#ffffaa7f -> 0xFD4F。
    空串 -> 0xFFFFFF（24 位全 1 的"无"哨兵，不是 565）。"""
    if not s:
        return 0xFFFFFF, 100
    t = s.lstrip('#')
    if len(t) == 8:
        a = int(t[0:2], 16); r = int(t[2:4], 16); g = int(t[4:6], 16); b = int(t[6:8], 16)
    elif len(t) == 6:
        a = 255; r = int(t[0:2], 16); g = int(t[2:4], 16); b = int(t[4:6], 16)
    else:
        return None, None
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3), round(a * 100 / 255)


def enum_val(prop):
    """取 enum 里 default 对应的整数值"""
    d = prop.get('default')
    for e in prop.get('enum', []) or []:
        if isinstance(e, dict) and d in e:
            return e[d]
    return None


def main():
    proj, enh, sty = sys.argv[1], sys.argv[2], sys.argv[3]
    ids = load_ids(enh)
    doc = json.load(open(proj, encoding='utf-8'))
    styd = parse(sty)
    raw = open(sty, 'rb').read()

    # id -> css 块字节
    css_by_id = {}
    for w in styd['windows']:
        base = w['offset']
        for c in w['controls']:
            pl = bytes.fromhex(c['payload'])
            rec = bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                         0xFF, 0xFF, 0xFF]) + struct.pack('<iI', c['id'], c['css_off']) + pl
            for name, off in PTRS.get(c['type'], [('css', 12)]):
                if name != 'css' or off + 4 > len(rec):
                    continue
                pv = U32(rec, off)
                if pv in NONE:
                    continue
                a = base + (pv & 0xFFFF)
                css_by_id[c['id'] & 0xFFFFFF] = raw[a:a + 36]

    # ename -> (节点, 父, 页)
    info = {}
    for pi, page in enumerate(doc.get('pages', [])):
        for n, par, _ in walk(page, None, page):
            e = ename_of(n)
            if e:
                info[e] = (n, par, page, pi)

    stats = Counter()
    samples = []
    for e, (n, par, page, pi) in info.items():
        if e not in ids:
            continue
        blk = css_by_id.get(ids[e] & 0xFFFFFF)
        if blk is None or len(blk) < 36:
            continue
        cs = css_struct(n)
        al, iv, zo, rv = blk[0], blk[1], blk[2], blk[3]
        L, T, W, H = struct.unpack_from('<iiii', blk, 4)
        bg = U32(blk, 20)
        bi = U32(blk, 24)
        bl, bt, br, bb = blk[28], blk[29], blk[30], blk[31]
        bc = U32(blk, 32)

        # --- align / invisible / z_order ---
        if 'align' in cs:
            stats['align 一致' if enum_val(cs['align']) == al else 'align 不一致'] += 1
        if 'invisible' in cs:
            stats['invisible 一致' if enum_val(cs['invisible']) == iv else 'invisible 不一致'] += 1
        if 'z_order' in cs:
            stats['z_order 一致' if cs['z_order'].get('default', 0) == zo else 'z_order 不一致'] += 1
        stats['rev==0xFF' if rv == 0xFF else 'rev!=0xFF'] += 1

        # --- 坐标：万分比，分母取"页"还是"父"？ ---
        r = rect_of(n)
        if r:
            x, y, w_, h_ = r
            pr = rect_of(page)
            rr = rect_of(par) if par is not None else None
            # 换算是「万分比 + **向上取整**」，不是四舍五入：
            #   95*10000/128 = 7421.875 -> 7422 ；17*10000/128 = 1328.125 -> 1329
            #   3*10000/128  =  234.375 ->  235
            def per(v, ref):
                return -((-v * 10000) // ref)      # ceil(v*10000/ref)，整数运算
            for tag, ref in (('按页', pr), ('按父', rr)):
                if not ref or not ref[2] or not ref[3]:
                    continue
                RW, RH = ref[2], ref[3]
                pred = (per(x, RW), per(y, RH), per(w_, RW), per(h_, RH))
                stats['坐标%s 一致' % tag if pred == (L, T, W, H) else '坐标%s 不一致' % tag] += 1
        # --- 背景色 / 背景图 / 边框 ---
        if 'background_color' in cs:
            want, wa = argb_to_565(cs['background_color'].get('background-color', ''))
            if want is not None:
                ok = (bg & 0xFFFFFF) == want and (bg >> 24) == wa
                stats['背景色 一致' if ok else '背景色 不一致'] += 1
                if not ok and stats['背景色 不一致'] <= 3:
                    print('   背景色不符 %s: json=%r -> 期望 0x%06X/a%d，实际 0x%06X/a%d'
                          % (e, cs['background_color'].get('background-color', ''),
                             want, wa, bg & 0xFFFFFF, bg >> 24))
        if 'background_image' in cs:
            v = cs['background_image'].get('background-image', '')
            ok = (bi == 0xFFFFFFFF) if not v else (bi != 0xFFFFFFFF)
            stats['背景图 一致' if ok else '背景图 不一致'] += 1
        if 'border' in cs:
            bb_ = cs['border'].get('border', {})
            want = (bb_.get('left', 0), bb_.get('top', 0), bb_.get('right', 0), bb_.get('bottom', 0))
            stats['边框宽 一致' if want == (bl, bt, br, bb) else '边框宽 不一致'] += 1
            gc = cs['border'].get('gray-color', None)
            wantc, wanta = argb_to_565(cs['border'].get('color', '')) if 'color' in cs['border']                 else (0xFFFFFF, 100)
            okc = (bc & 0xFFFFFF) == wantc and (bc >> 24) == wanta
            stats['边框色 一致' if okc else '边框色 不一致'] += 1

        if len(samples) < 6:
            samples.append((e, cs, (al, iv, zo, rv, L, T, W, H, bg, bi,
                                    (bl, bt, br, bb), bc), r,
                            rect_of(page), rect_of(par) if par is not None else None))

    print('样本 %d 个\n' % sum(1 for e in info if e in ids and css_by_id.get(ids[e] & 0xFFFFFF)))
    for k in sorted(stats):
        print('  %-18s %d' % (k, stats[k]))

    print('\n前几个样本明细：')
    for e, cs, v, r, pr, rr in samples:
        al, iv, zo, rv, L, T, W, H, bg, bi, bd, bc = v
        print('  %-22s json rect=%s  页=%s 父=%s' % (e, r, pr, rr))
        print('     二进制 align=%d inv=%d z=%d rev=%d  l/t/w/h=%d/%d/%d/%d' %
              (al, iv, zo, rv, L, T, W, H))
        print('     bg=0x%08X(色0x%06X α=%d)  img=0x%08X(id 0x%06X q=%d)  border=%s 色0x%08X'
              % (bg, bg & 0xFFFFFF, bg >> 24, bi, bi & 0xFFFFFF, bi >> 24, bd, bc))
        print('     json css 键: %s' % sorted(cs.keys()))
        for k in ('background_color', 'background_image', 'border'):
            if k in cs:
                print('        %s -> %s' % (k, json.dumps(
                    {kk: vv for kk, vv in cs[k].items() if kk not in ('-name', '-type', 'caption')},
                    ensure_ascii=False)[:110]))


if __name__ == '__main__':
    main()
