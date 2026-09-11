# -*- coding: utf-8 -*-
"""控件记录在 .sty 里的**排列顺序**，以及父子指针的含义。

要确认的：
  1) 记录顺序是 json 树的哪种遍历（前序 / 层序 / 其它）
  2) 父节点的 ctrl / layout / info 指针是不是指向"第一个孩子"，
     孩子是不是连续排放（ctrl_num 个）

用法: python verify_order.py <工程目录> <JL.sty>
"""
import glob
import json
import os
import struct
import sys

from sty_dump import parse
from sty_blocks import PTRS, NONE
from verify_css import load_ids, walk, ename_of

U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def children(n):
    out = []
    for k in ('layer', 'layout', 'widget', 'listwidget'):
        for c in n.get(k, []) or []:
            out.append(c)
    return out


def preorder(node, out):
    for c in children(node):
        out.append(c)
        preorder(c, out)


def childblock(node, out):
    """排法：先把 node 的孩子**连续**写完，再依次递归每个孩子。
    也就是"每层一块、深度优先地展开各子树"。"""
    ch = children(node)
    out.extend(ch)
    for c in ch:
        childblock(c, out)


def levelorder(node, out):
    q = list(children(node))
    while q:
        n = q.pop(0)
        out.append(n)
        q.extend(children(n))


def main():
    proj = sys.argv[1]
    sty = sys.argv[2]
    jsons = [f for f in glob.glob(os.path.join(proj, '*.json'))
             if 'autosave' not in os.path.basename(f).lower()]
    doc = json.load(open(jsons[0], encoding='utf-8'))
    ids = load_ids(os.path.join(proj, 'ename.h'))
    styd = parse(sty)
    raw = open(sty, 'rb').read()
    id2name = {v & 0xFFFFFF: k for k, v in ids.items()}

    allok = True
    for pi, w in enumerate(styd['windows']):
        page = doc['pages'][pi]
        order_file = [id2name.get(c['id'] & 0xFFFFFF, '?%X' % c['id']) for c in w['controls']]

        pre, lev, blk = [], [], []
        preorder(page, pre)
        levelorder(page, lev)
        childblock(page, blk)
        pre = [ename_of(n) for n in pre]
        lev = [ename_of(n) for n in lev]
        blk = [ename_of(n) for n in blk]
        print('页%d 记录 %d 条' % (pi, len(order_file)))
        print('   前序一致 %s   层序一致 %s   孩子块序一致 %s'
              % (order_file == pre, order_file == lev, order_file == blk))
        if order_file != blk:
            allok = False
            for i in range(min(len(order_file), len(blk))):
                if order_file[i] != blk[i]:
                    print('   首个不同 @%d 文件=%s 孩子块序=%s'
                          % (i, order_file[i], blk[i]))
                    print('   文件  : %s' % order_file[max(0, i - 2):i + 5])
                    print('   孩子块: %s' % blk[max(0, i - 2):i + 5])
                    break

    # --- 父子指针 ---
    print('\n父子指针检查：')
    ok = bad = 0
    for pi, w in enumerate(styd['windows']):
        base = w['offset']
        offs = {c['off'] - base: i for i, c in enumerate(w['controls'])}
        for i, c in enumerate(w['controls']):
            pl = bytes.fromhex(c['payload'])
            rec = (bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'], c['page'],
                          0xFF, 0xFF, 0xFF])
                   + struct.pack('<iI', c['id'], c['css_off']) + pl)
            for name, off in PTRS.get(c['type'], []):
                if name not in ('ctrl', 'layout', 'info') or off + 4 > len(rec):
                    continue
                pv = U32(rec, off)
                if pv in NONE:
                    ok += 1 if c['ctrl_num'] == 0 else 0
                    bad += 1 if c['ctrl_num'] != 0 else 0
                    continue
                target = pv & 0xFFFF
                if target in offs:
                    ok += 1
                else:
                    bad += 1
                    print('   页%d 控件%d(%s) %s 指到 0x%X，不是任何控件记录'
                          % (pi, i, id2name.get(c['id'] & 0xFFFFFF, '?'), name, target))
        # 窗口记录的 layer 指针
        first = w['controls'][0]['off'] - base if w['controls'] else 0
        rec = raw[base:base + 28]
        lp = U32(rec, 24)
        print('   页%d window.layer -> 0x%X，第一个控件在 0x%X  %s'
              % (pi, lp, first, 'OK' if lp == first else 'BAD'))
    print('   ctrl 指针指向已知记录: %d，异常 %d' % (ok, bad))
    return 0 if allok and bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
