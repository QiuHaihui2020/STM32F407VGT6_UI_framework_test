# -*- coding: utf-8 -*-
"""验证：控件类型码完全来自 UITools/config/ini/option.ini 的 [Control] 段。

做法：
  ename.h 里每个宏的 id -> (id>>16)&0x3F 得到实际写进 .sty 的 type；
  工程 json 里同名 ename 的节点 -> 取它的 "-type"；
  用 option.ini 把 "-type" 映射成类型码，两边对拍。

全对 = 这张表就是权威来源，重写 QtToolBin 时照抄即可，不用逆向。

用法: python verify_types.py <option.ini> <工程.json> <ename.h>
"""
import json
import re
import sys
from collections import defaultdict


def load_option_ini(path):
    """[Control] 段：名字=类型码。文件是 UTF-8 带 BOM，含中文别名。"""
    tbl = {}
    sect = None
    for line in open(path, encoding='utf-8-sig', errors='replace'):
        line = line.strip()
        if not line or line.startswith(';') or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            sect = line[1:-1]
            continue
        if sect != 'Control' or '=' not in line:
            continue
        k, v = line.split('=', 1)
        try:
            tbl[k.strip()] = int(v.strip())
        except ValueError:
            pass
    return tbl


def walk(node):
    if isinstance(node, dict):
        yield node
        for k in ('layer', 'layout', 'widget', 'listwidget'):
            for c in node.get(k, []) or []:
                for x in walk(c):
                    yield x


def main():
    ini, proj, enh = sys.argv[1], sys.argv[2], sys.argv[3]
    tbl = load_option_ini(ini)
    print('option.ini [Control] 共 %d 条' % len(tbl))

    ids = {n: int(v, 16) for n, v in
           re.findall(r'#define\s+(\S+)\s+0X([0-9A-Fa-f]+)',
                      open(enh, encoding='utf-8', errors='replace').read())}

    doc = json.load(open(proj, encoding='utf-8'))
    # ename -> (页号, -type, -class)
    info = {}
    for pi, page in enumerate(doc.get('pages', [])):
        for n in walk(page):
            ename = None
            for p in n.get('property', []) or []:
                if isinstance(p, dict) and p.get('-name') == 'id':
                    ename = p.get('ename')
            if ename:
                info[ename] = (pi, n.get('-type'), n.get('-class'), n.get('caption'))

    ok = bad = miss = 0
    badlist = []
    pagebad = 0
    for ename, (pi, jtype, jcls, jcap) in info.items():
        if ename not in ids:
            miss += 1
            continue
        v = ids[ename]
        got_type = (v >> 16) & 0x3F
        got_page = (v >> 22) & 0x7F
        # 扩展控件（control/ex 里的 slider / vslider 及其零件）的 -type 都是
        # NewLayout / ImageList / Text，真正决定类型码的是 caption —— 所以
        # 查表顺序是 caption 优先，再退回 -type。
        want = tbl.get(jcap) if jcap in tbl else tbl.get(jtype)
        if want is None:
            miss += 1
            continue
        if want == got_type:
            ok += 1
        else:
            bad += 1
            if len(badlist) < 10:
                badlist.append((ename, jtype, want, got_type))
        if got_page != pi:
            pagebad += 1

    print('\n类型码对拍: 一致 %d / 不一致 %d / 无法对照 %d' % (ok, bad, miss))
    for e, t, w, g in badlist:
        print('   %-24s -type=%-14s option.ini=%-3s 实际=%s' % (e, t, w, g))
    print('页号对拍: 不一致 %d / 共 %d' % (pagebad, ok + bad))

    print('\n各 -type 用到的类型码：')
    used = defaultdict(set)
    for ename, (pi, jtype, jcls, jcap) in info.items():
        if ename in ids:
            used[jcap if jcap in tbl else jtype].add((ids[ename] >> 16) & 0x3F)
    for t in sorted(used):
        print('   %-16s -> %s   (option.ini: %s)'
              % (t, sorted(used[t]), tbl.get(t, '—')))
    return 0 if bad == 0 and pagebad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
