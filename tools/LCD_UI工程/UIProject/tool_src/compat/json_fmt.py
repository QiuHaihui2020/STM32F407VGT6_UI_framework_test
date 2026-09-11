# -*- coding: utf-8 -*-
"""确认工程 json 的写出格式，以及 uitoolbin.bin 与工程 json 的关系。"""
import json
import sys
import io


def sniff(path):
    raw = open(path, 'rb').read()
    crlf = raw.count(b'\r\n')
    lf = raw.count(b'\n') - crlf
    bom = raw[:3] == b'\xef\xbb\xbf'
    return {
        'size': len(raw), 'crlf': crlf, 'lf_only': lf, 'bom': bom,
        'tail': raw[-8:], 'head': raw[:40],
    }


def keys_sorted(o, path='', bad=None):
    if bad is None:
        bad = []
    if isinstance(o, dict):
        ks = list(o.keys())
        if ks != sorted(ks):
            bad.append((path, ks[:6]))
        for k, v in o.items():
            keys_sorted(v, path + '/' + k, bad)
    elif isinstance(o, list):
        for i, v in enumerate(o[:2]):
            keys_sorted(v, path + '[]', bad)
    return bad


for p in sys.argv[1:]:
    s = sniff(p)
    print('== %s' % p)
    print('   %d B  CRLF=%d  裸LF=%d  BOM=%s  尾=%r' %
          (s['size'], s['crlf'], s['lf_only'], s['bom'], s['tail']))
    doc = json.load(open(p, encoding='utf-8'))
    bad = keys_sorted(doc)
    print('   顶层键: %s' % list(doc.keys()))
    print('   键未按字典序的对象数: %d %s' % (len(bad), bad[:2]))

if len(sys.argv) == 3:
    a = json.load(open(sys.argv[1], encoding='utf-8'))
    b = json.load(open(sys.argv[2], encoding='utf-8'))
    print('\n== 两份内容比较 ==')
    print('   完全相等: %s' % (a == b))
    ka, kb = set(a), set(b)
    print('   仅 A 有的顶层键: %s' % (ka - kb))
    print('   仅 B 有的顶层键: %s' % (kb - ka))
    for k in sorted(ka & kb):
        same = a[k] == b[k]
        if not same:
            if isinstance(a[k], list) and isinstance(b[k], list):
                print('   键 %-12s 不同：len %d vs %d' % (k, len(a[k]), len(b[k])))
            else:
                print('   键 %-12s 不同：%r vs %r' % (k, str(a[k])[:40], str(b[k])[:40]))
    # 页级比较
    if 'pages' in a and 'pages' in b and len(a['pages']) == len(b['pages']):
        for i, (pa, pb) in enumerate(zip(a['pages'], b['pages'])):
            if pa != pb:
                print('   页%d 不同；键 A=%s B=%s' % (i, sorted(pa), sorted(pb)))
                sa, sb = json.dumps(pa, sort_keys=True), json.dumps(pb, sort_keys=True)
                print('        序列化长度 %d vs %d' % (len(sa), len(sb)))
                for j in range(min(len(sa), len(sb))):
                    if sa[j] != sb[j]:
                        print('        首个差异 @%d: A…%s… B…%s…'
                              % (j, sa[max(0, j - 60):j + 60], sb[max(0, j - 60):j + 60]))
                        break
                break
