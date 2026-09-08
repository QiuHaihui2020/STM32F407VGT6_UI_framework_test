# -*- coding: utf-8 -*-
"""归纳 ui-tools 工程 json 的 schema。"""
import json
import sys
from collections import defaultdict, Counter

MAXD = int(sys.argv[2]) if len(sys.argv) > 2 else 6
paths = defaultdict(Counter)
samples = {}


def walk(o, p, d=0):
    if d > MAXD:
        return
    if isinstance(o, dict):
        for k, v in o.items():
            np = p + '.' + k
            paths[np][type(v).__name__] += 1
            if np not in samples and not isinstance(v, (dict, list)):
                samples[np] = v
            walk(v, np, d + 1)
    elif isinstance(o, list):
        paths[p + '[]']['len'] += len(o)
        for v in o[:3]:
            walk(v, p + '[]', d + 1)


doc = json.load(open(sys.argv[1], encoding='utf-8'))
print('顶层类型:', type(doc).__name__)
if isinstance(doc, dict):
    print('顶层键:', list(doc.keys()))
walk(doc, '')
for p in sorted(paths):
    t = dict(paths[p])
    s = samples.get(p, '')
    if isinstance(s, str) and len(s) > 48:
        s = s[:48] + '...'
    print('%-58s %-28s %r' % (p, t, s))
