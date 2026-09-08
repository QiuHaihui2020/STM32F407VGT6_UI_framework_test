# -*- coding: utf-8 -*-
import json
import sys

doc = json.load(open(sys.argv[1], encoding='utf-8'))
page = doc['pages'][0]
print('page keys:', sorted(page.keys()))
print('page property:', json.dumps(page.get('property'), ensure_ascii=False)[:300])

layer = page['layer'][0]
print('\nlayer -name=%r -class=%r -type=%r caption=%r' %
      (layer.get('-name'), layer.get('-class'), layer.get('-type'), layer.get('caption')))
print('layer keys:', sorted(layer.keys()))
for p in layer.get('property', []):
    print('   prop -name=%-12r -type=%-10r caption=%-8r keys=%s'
          % (p.get('-name'), p.get('-type'), p.get('caption'),
             [k for k in p.keys() if k not in ('-name', '-type', 'caption')]))
    if p.get('-name') == 'rect':
        print('        rect =', p.get('rect'))

lay = layer['layout'][0]
print('\nlayout -name=%r' % lay.get('-name'))
for p in lay.get('property', []):
    if p.get('-name') in ('rect', 'id'):
        print('   %s -> %s' % (p.get('-name'), json.dumps(
            {k: v for k, v in p.items() if k in ('rect', 'ename', 'id')}, ensure_ascii=False)))


def walk(n, path=''):
    for key in ('layer', 'layout', 'widget', 'listwidget'):
        for c in n.get(key, []) or []:
            yield c
            for x in walk(c):
                yield x


print('\n各 -class 的 -name 举例:')
seen = {}
for c in walk(page):
    cls = c.get('-class')
    if cls not in seen:
        seen[cls] = c
        r = None
        for p in c.get('property', []):
            if p.get('-name') == 'rect':
                r = p.get('rect')
        print('   %-10s -name=%-16r -type=%-12r rect=%s'
              % (cls, c.get('-name'), c.get('-type'), r))
