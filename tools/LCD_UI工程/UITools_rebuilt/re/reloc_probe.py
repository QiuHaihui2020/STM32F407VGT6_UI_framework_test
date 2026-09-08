# -*- coding: utf-8 -*-
"""验证两件事：
 1) css/str/action 等指针是「页内相对」(ptr & 0xFFFF)，页号在 bit22..28；
 2) 页尾那张 u16 表是「重定位表」——列出控件区里所有指针字段的页内偏移。
"""
import struct
import sys
from collections import Counter, defaultdict
sys.path.insert(0, r'D:\MyFile\zh-jieli\sdk_demo\JIELI_STM32_UI\STM32F407VGT6_Template\tools\LCD_UI工程\UITools_src\re')
from sty_dump import parse, CTRL_TYPE

path = sys.argv[1]
raw = open(path, 'rb').read()
doc = parse(path)

for w in doc['windows']:
    base = w['offset']
    ctrls = w['controls']
    tbl = raw[w['table_ptr']:w['table_ptr'] + w['table_size']]
    vals = list(struct.unpack('<%dH' % (len(tbl) // 2), tbl))
    tset = set(vals)
    ctrl_end = w['scan_stopped_at']

    print('=' * 78)
    print('页 base=0x%X 控件 %d 条 控件区 0x%X..0x%X css区..0x%X 表 %d 项'
          % (base, len(ctrls), ctrls[0]['off'], ctrl_end, w['table_ptr'], len(vals)))

    # --- 1) css 指针页内相对 ---
    ok = bad = 0
    firstcss = None
    for c in ctrls:
        p = c['css_off']
        rel = p & 0xFFFF
        pg = (p >> 22) & 0x7F
        absolute = base + rel
        if ctrl_end <= absolute < w['table_ptr']:
            ok += 1
            if firstcss is None or absolute < firstcss:
                firstcss = absolute
        else:
            bad += 1
    print('  css 指针按 base+(ptr&0xFFFF) 落在 css 区: %d/%d   最小 0x%X (控件区结束 0x%X)'
          % (ok, ok + bad, firstcss or 0, ctrl_end))
    print('  css 页号字段 (ptr>>22)&0x7f 取值:',
          Counter((c['css_off'] >> 22) & 0x7F for c in ctrls).most_common())

    # --- 2) 重定位表 ---
    # 把每条控件记录里"值落在本页范围内、且该字段偏移出现在表里"的 dword 找出来
    per_type = defaultdict(Counter)
    hit = 0
    for c in ctrls:
        rel_rec = c['off'] - base
        for k in range(0, c['len'], 4):
            fo = rel_rec + k
            if fo in tset:
                hit += 1
                per_type[c['type']][k] += 1
    print('  表项命中控件字段: %d / 表项 %d' % (hit, len(vals)))
    print('  未命中的表项:', sorted(v for v in vals
                                if not any(c['off'] - base <= v < c['off'] - base + c['len']
                                           for c in ctrls)))
    print('  按控件类型统计「哪些字段偏移被登记为指针」:')
    for t in sorted(per_type):
        cnt = sum(1 for c in ctrls if c['type'] == t)
        fields = ' '.join('+%d×%d' % (k, n) for k, n in sorted(per_type[t].items()))
        print('     type=%-3d %-16s 共 %-3d 条   %s'
              % (t, CTRL_TYPE.get(t, '?'), cnt, fields))

    # --- 3) 按 element_css1 解第一个 css ---
    if firstcss:
        for c in ctrls[:3]:
            o = base + (c['css_off'] & 0xFFFF)
            b = raw[o:o + 36]
            al, iv, z, rv = b[0], b[1], b[2], b[3]
            left, top, wid, hei = struct.unpack_from('<iiii', b, 4)
            bg, bi = struct.unpack_from('<II', b, 20)
            bl, bt, br, bb = b[28], b[29], b[30], b[31]
            bc = struct.unpack_from('<I', b, 32)[0]
            print('  css@0x%-5X %-12s align=%d inv=%d z=%d rev=%d rect=(%d,%d,%d,%d) '
                  'bg=0x%06X a=%d img=0x%06X q=%d border=%d/%d/%d/%d c=0x%06X'
                  % (o, c['type_name'], al, iv, z, rv, left, top, wid, hei,
                     bg & 0xFFFFFF, bg >> 24, bi & 0xFFFFFF, bi >> 24,
                     bl, bt, br, bb, bc & 0xFFFFFF))
    if w is doc['windows'][0]:
        continue
