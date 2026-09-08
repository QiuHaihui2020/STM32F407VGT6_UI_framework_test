# -*- coding: utf-8 -*-
import json
import sys

doc = json.load(open(sys.argv[1], encoding='utf-8'))
page = doc['pages'][0]
layer = page['layer'][0]

for p in layer.get('property', []):
    if p.get('-name') == 'element_css':
        print('element_css 顶层键:', sorted(p.keys()))
        print('info =', repr(p.get('info')))
        st = p.get('struct')
        print('struct 类型 %s，长度 %d' % (type(st).__name__, len(st)))
        print(json.dumps(st, ensure_ascii=False, indent=1)[:2500])
        break

print('\n===== 一个 Text 控件的完整 property =====')


def walk(n):
    for key in ('layer', 'layout', 'widget', 'listwidget'):
        for c in n.get(key, []) or []:
            yield c
            for x in walk(c):
                yield x


for c in walk(page):
    if c.get('-type') == 'Text':
        print('-name=%r -class=%r' % (c.get('-name'), c.get('-class')))
        for p in c.get('property', []):
            keys = [k for k in p.keys()]
            print('  -name=%-14r -type=%-12r caption=%-10r keys=%s'
                  % (p.get('-name'), p.get('-type'), p.get('caption'), keys))
        for p in c.get('property', []):
            if p.get('-name') == 'element_css':
                print('  element_css.struct =',
                      json.dumps(p.get('struct'), ensure_ascii=False)[:1200])
        break
