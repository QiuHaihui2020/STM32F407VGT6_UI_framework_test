# -*- coding: utf-8 -*-
"""验证：.sty 里的图片/字符串 ID 就是 ResBuilder 分配的资源号。

要证的三件事（这是 QtToolBin 生成器最后缺的一环）：
  1) 图片列表块 = u16 个数 + u16 资源号[]，资源号是**页内**编号
     （result_pic_index.h 里 //PAGE n 那一段，按宏名排序 1..N）
  2) 文字列表块 = u16 个数 + u16 资源号[]，资源号是**全局**编号
     （result_str_index.h，全部 cell 去重按名排序 1..N）
  3) css 的 background_image 低 24 位同样是页内图片号，高 8 位是 image_quadrant

用法: python verify_resids.py <工程目录>
     （目录里要有 工程.json / ename.h / result_pic_index.h / result_str_index.h，
       .sty 用 tools/JL/JL.sty）
"""
import glob
import json
import os
import struct
import sys

from sty_dump import parse
from sty_blocks import PTRS, NONE
from verify_css import load_ids, walk, ename_of, css_struct

U16 = lambda b, o: struct.unpack_from('<H', b, o)[0]
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def load_pic_index(path):
    """-> {页号: {宏名: id}}"""
    out = {}
    page = -1
    for line in open(path, 'rb'):
        s = line.decode('latin1').rstrip()
        if s.startswith('//PAGE'):
            page = int(s.split()[1])
            out[page] = {}
        else:
            p = s.split()
            if len(p) >= 3 and p[0] == '#define' and p[2].isdigit():
                out.setdefault(page, {})[p[1]] = int(p[2])
    return out


def load_str_index(path):
    out = {}
    for line in open(path, 'rb'):
        p = line.decode('latin1').split()
        if len(p) >= 3 and p[0] == '#define' and p[2].isdigit():
            out[p[1]] = int(p[2])
    return out


def symbol_of(rel):
    return os.path.splitext(os.path.basename(rel))[0].upper()


def prop(n, name):
    for p in n.get('property', []) or []:
        if isinstance(p, dict) and p.get('-name') == name:
            return p
    return None


def main():
    proj = sys.argv[1]
    sty = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(proj.rstrip('\\/')), 'JL.sty')
    jsons = [f for f in glob.glob(os.path.join(proj, '*.json'))
             if 'autosave' not in os.path.basename(f).lower()]
    doc = json.load(open(jsons[0], encoding='utf-8'))
    ids = load_ids(os.path.join(proj, 'ename.h'))
    pic = load_pic_index(os.path.join(proj, 'result_pic_index.h'))
    strx = load_str_index(os.path.join(proj, 'result_str_index.h'))

    styd = parse(sty)
    raw = open(sty, 'rb').read()

    # id -> (页基址, 记录字节)
    recs = {}
    for w in styd['windows']:
        for c in w['controls']:
            pl = bytes.fromhex(c['payload'])
            rec = (bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                          0xFF, 0xFF, 0xFF])
                   + struct.pack('<iI', c['id'], c['css_off']) + pl)
            recs[c['id'] & 0xFFFFFF] = (w['offset'], rec, c['type'])

    ok = bad = 0
    msgs = []

    def check(cond, what, detail=''):
        nonlocal ok, bad
        if cond:
            ok += 1
        else:
            bad += 1
            if len(msgs) < 12:
                msgs.append('%s 不符 %s' % (what, detail))

    for pi, page in enumerate(doc.get('pages', [])):
        pagePics = pic.get(pi, {})
        for n, par, _ in walk(page, None, page):
            e = ename_of(n)
            if not e or e not in ids:
                continue
            got = recs.get(ids[e] & 0xFFFFFF)
            if not got:
                continue
            base, rec, ctype = got

            def block(field):
                for name, off in PTRS.get(ctype, []):
                    if name != field or off + 4 > len(rec):
                        continue
                    pv = U32(rec, off)
                    if pv in NONE:
                        return []
                    a = base + (pv & 0xFFFF)
                    cnt = U16(raw, a)
                    return [U16(raw, a + 2 + 2 * i) for i in range(cnt)]
                return None

            # ---- 图片列表 ----
            for jsonName, blkName in (('normal_image', 'img_normal'),
                                      ('highlight_image', 'img_high'),
                                      ('image', 'img_normal'),
                                      ('charge_image', 'img_charge')):
                p = prop(n, jsonName)
                if p is None:
                    continue
                want = [0xFFFF if not x else pagePics.get(symbol_of(x), 0xFFFF)
                        for x in (p.get('list') or [])]
                have = block(blkName)
                if have is None:
                    continue
                check(want == have, '%s.%s' % (e, jsonName),
                      'json=%s 二进制=%s' % (want, have))

            # ---- 文字列表 ----
            p = prop(n, 'str')
            if p is not None:
                # 空串（json 里确实有 str.list == [""] 的控件）落盘写 0xFFFF
                want = [0xFFFF if not x else strx.get(x.upper(), 0xFFFF)
                        for x in (p.get('list') or [])]
                have = block('strlist')
                if have is not None:
                    check(want == have, '%s.str' % e, 'json=%s 二进制=%s' % (want, have))

            # ---- css 背景图 ----
            cs = css_struct(n)
            if 'background_image' in cs:
                v = cs['background_image'].get('background-image', '')
                for name, off in PTRS.get(ctype, [('css', 12)]):
                    if name != 'css' or off + 4 > len(rec):
                        continue
                    pv = U32(rec, off)
                    if pv in NONE:
                        break
                    bi = U32(raw, base + (pv & 0xFFFF) + 24)
                    if not v:
                        check(bi == 0xFFFFFFFF, '%s.bg_image(空)' % e, hex(bi))
                    else:
                        want = pagePics.get(symbol_of(v), -1)
                        check((bi & 0xFFFFFF) == want, '%s.bg_image' % e,
                              'json=%s(%d) 二进制=0x%06X' % (v, want, bi & 0xFFFFFF))
                    break

    print('资源号比对: 一致 %d / 不一致 %d' % (ok, bad))
    for m in msgs:
        print('   ' + m)
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
