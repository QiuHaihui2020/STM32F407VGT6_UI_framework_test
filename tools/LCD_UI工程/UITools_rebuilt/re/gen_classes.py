# -*- coding: utf-8 -*-
"""按逆向出的 moc 元数据生成 Qt 类骨架。

生成的接口（类名/基类/signals/slots/properties/enums）与原 exe 中的
staticMetaObject 逐字一致 —— 重新 moc 之后可以再跑一次 moc_scan.py 对拍。
"""
import json
import os
import re
import sys

# Qt 自带 / Qt 私有类，不属于本应用，不生成
SKIP = {'Qt', 'CloseButton', 'DefaultStateTransition',
        '_QStateMachine_Internal::GoToStateTransition'}

# 基类 -> 头文件
QT_HDR = {
    'QObject': 'QObject', 'QWidget': 'QWidget', 'QDialog': 'QDialog',
    'QFrame': 'QFrame', 'QDockWidget': 'QDockWidget', 'QGroupBox': 'QGroupBox',
    'QTabWidget': 'QTabWidget', 'QScrollArea': 'QScrollArea',
    'QAbstractButton': 'QAbstractButton', 'QPushButton': 'QPushButton',
    'QMainWindow': 'QMainWindow', 'QAbstractTransition': 'QAbstractTransition',
}

# 出现在签名里、需要额外 include 的类型
TYPE_HDR = {
    'QListWidgetItem': 'QListWidgetItem', 'QTreeWidgetItem': 'QTreeWidgetItem',
    'QModelIndex': 'QModelIndex', 'QPoint': 'QPoint', 'QRect': 'QRect',
    'QSize': 'QSize', 'QString': 'QString', 'QColor': 'QColor',
    'QVariant': 'QVariant', 'QStringList': 'QStringList',
}

# 这些类的实现是手写的，只生成头文件（.cpp 由手写文件提供）
HAND_WRITTEN = {
    'MainWindow', 'CanvasManager', 'ScenesScreen', 'BaseForm', 'NewLayer',
    'NewLayout', 'NewFrame', 'NewList', 'NewGrid', 'TreeDock', 'PageView',
    'CompoentControls', 'PropertyTab', 'FormResizer', 'SizeHandleRect',
    'ProjectDialog',
}


def guard(name):
    return re.sub(r'[^A-Za-z0-9]', '_', name).upper() + '_H'


def parent_type(root):
    return 'QObject' if root == 'QObject' else 'QWidget'


def root_of(cls, by_name):
    seen = set()
    cur = cls
    while cur in by_name and cur not in seen:
        seen.add(cur)
        cur = by_name[cur]['super']
    return cur


def sig(m):
    args = []
    for i, (t, n) in enumerate(m['args']):
        n = n or ('a%d' % i)
        # 指针类型的 * 已在类型串里
        args.append('%s %s' % (t, n))
    return '%s %s(%s)' % (m['return'], m['name'], ', '.join(args))


def main():
    meta = json.load(open(sys.argv[1], encoding='utf-8'))
    outdir = sys.argv[2]
    inc = os.path.join(outdir, 'include')
    src = os.path.join(outdir, 'src', 'gen')
    os.makedirs(inc, exist_ok=True)
    os.makedirs(src, exist_ok=True)

    def is_qt(n):
        return bool(re.match(r'^Q[A-Z0-9_]', n))

    app = {c['classname']: c for c in meta['classes']
           if not is_qt(c['classname']) and c['classname'] not in SKIP}

    written = []
    for name, c in sorted(app.items()):
        sup = c['super']
        root = root_of(name, app)
        ptype = parent_type(root)

        incs = set()
        if sup in app:
            incs.add('"%s.h"' % sup)
        else:
            incs.add('<%s>' % QT_HDR.get(sup, 'QWidget'))
        for m in c['methods']:
            for t, _ in m['args'] + [(m['return'], '')]:
                base = t.rstrip('*').strip()
                if base in TYPE_HDR:
                    incs.add('<%s>' % TYPE_HDR[base])
        for p in c['properties']:
            base = p['type'].rstrip('*').strip()
            if base in TYPE_HDR:
                incs.add('<%s>' % TYPE_HDR[base])

        h = []
        h.append('/* %s —— 由 gen_classes.py 依据 ui-tools.exe 的 moc 元数据生成。' % (name + '.h'))
        h.append(' * 接口（基类 / signals / slots / properties / enums）与原始二进制一致，')
        h.append(' * 不要手工改签名；实现写在 src/ 下的同名 .cpp。')
        h.append(' */')
        h.append('#ifndef %s' % guard(name))
        h.append('#define %s' % guard(name))
        h.append('')
        for i in sorted(incs):
            h.append('#include %s' % i)
        h.append('')
        h.append('class %s : public %s' % (name, sup))
        h.append('{')
        h.append('    Q_OBJECT')
        for p in c['properties']:
            h.append('    Q_PROPERTY(%s %s READ %s WRITE set%s%s)'
                     % (p['type'], p['name'], p['name'],
                        p['name'][0].upper(), p['name'][1:]))
        h.append('')
        h.append('public:')
        h.append('    explicit %s(%s *parent = nullptr);' % (name, ptype))
        h.append('    ~%s() override;' % name)
        for e in c['enums']:
            if e['isFlag']:
                continue
            h.append('')
            h.append('    enum %s {' % e['name'])
            for n, v in e['values']:
                h.append('        %s = %d,' % (n, v))
            h.append('    };')
            h.append('    Q_ENUM(%s)' % e['name'])
        for p in c['properties']:
            h.append('')
            h.append('    %s %s() const;' % (p['type'], p['name']))
            h.append('    void set%s%s(const %s &v);'
                     % (p['name'][0].upper(), p['name'][1:], p['type']))

        for kind, label in (('signal', 'signals:'), ('slot', 'public slots:')):
            ms = [m for m in c['methods'] if m['kind'] == kind and m['access'] == 'public']
            if ms:
                h.append('')
                h.append(label)
                for m in ms:
                    h.append('    %s;' % sig(m))
        ms = [m for m in c['methods'] if m['kind'] == 'slot' and m['access'] != 'public']
        if ms:
            h.append('')
            h.append('private slots:')
            for m in ms:
                h.append('    %s;' % sig(m))
        h.append('};')
        h.append('')
        h.append('#endif // %s' % guard(name))
        open(os.path.join(inc, name + '.h'), 'w', encoding='utf-8').write('\n'.join(h) + '\n')

        if name in HAND_WRITTEN:
            written.append((name, True))
            continue

        s = []
        s.append('/* %s.cpp —— 生成的默认实现骨架。' % name)
        s.append(' * 槽体是空的：原始实现是编译过的机器码，无法还原，只能按行为重写。')
        s.append(' */')
        s.append('#include "%s.h"' % name)
        s.append('')
        s.append('%s::%s(%s *parent)' % (name, name, ptype))
        s.append('    : %s(parent)' % sup)
        s.append('{')
        s.append('}')
        s.append('')
        s.append('%s::~%s() = default;' % (name, name))
        for p in c['properties']:
            s.append('')
            s.append('%s %s::%s() const' % (p['type'], name, p['name']))
            s.append('{')
            s.append('    return property("%s").value<%s>();' % (p['name'], p['type']))
            s.append('}')
            s.append('')
            s.append('void %s::set%s%s(const %s &v)'
                     % (name, p['name'][0].upper(), p['name'][1:], p['type']))
            s.append('{')
            s.append('    setProperty("%s", v);' % p['name'])
            s.append('}')
        for m in c['methods']:
            if m['kind'] == 'signal':
                continue
            args = ', '.join('%s %s' % (t, n or 'a%d' % i)
                             for i, (t, n) in enumerate(m['args']))
            s.append('')
            s.append('%s %s::%s(%s)' % (m['return'], name, m['name'], args))
            s.append('{')
            for i, (t, n) in enumerate(m['args']):
                s.append('    Q_UNUSED(%s)' % (n or 'a%d' % i))
            s.append('    // TODO(UITools): 行为待补 —— 见 docs/RE_REPORT.md')
            s.append('}')
        open(os.path.join(src, name + '.cpp'), 'w', encoding='utf-8').write('\n'.join(s) + '\n')
        written.append((name, False))

    print('生成 %d 个类（%d 个只出头文件，实现手写）'
          % (len(written), sum(1 for _, hw in written if hw)))
    for n, hw in written:
        print('   %-24s %s' % (n, '头文件（实现手写）' if hw else '头文件 + 骨架实现'))


if __name__ == '__main__':
    main()
