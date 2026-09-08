# -*- coding: utf-8 -*-
"""从 uitools_meta.json 中筛出应用自有类并打印接口。"""
import json
import sys
import re

data = json.load(open(sys.argv[1], encoding='utf-8'))
classes = data['classes']

# Qt 自带类一律以 Q 开头 + 大写，或位于已知命名空间
def is_qt(n):
    if re.match(r'^Q[A-Z0-9_]', n):
        return True
    if n.startswith('QtPrivate') or n.startswith('Qt::'):
        return True
    if '::' in n and re.match(r'^Q[A-Z]', n.split('::')[0]):
        return True
    return False

app = [c for c in classes if not is_qt(c['classname'])]
app.sort(key=lambda c: c['classname'])
print('=== 应用自有类 %d 个 (总 %d) ===' % (len(app), len(classes)))
for c in app:
    print('%-28s : %s' % (c['classname'], c.get('super')))
print()
if len(sys.argv) > 2 and sys.argv[2] == 'full':
    for c in app:
        print('#' * 70)
        print('class %s : public %s' % (c['classname'], c.get('super')))
        for ci in c['classinfo']:
            print('   Q_CLASSINFO("%s", "%s")' % tuple(ci))
        for e in c['enums']:
            print('   enum %s%s { %s }' % (e['name'], ' [flag]' if e['isFlag'] else '',
                                           ', '.join('%s=%d' % (n, v) for n, v in e['values'])))
        for p in c['properties']:
            print('   Q_PROPERTY(%s %s)  flags=0x%x' % (p['type'], p['name'], p['flags']))
        for m in c['methods']:
            args = ', '.join('%s %s' % (t, n) for t, n in m['args'])
            print('   [%-6s %-9s] %s %s(%s)' % (m['kind'], m['access'], m['return'], m['name'], args))
