# -*- coding: utf-8 -*-
"""把 UI 框架的源文件与头文件路径加进 Keil .uvprojx(两个 Target 都加)。

幂等: 重复运行不会产生重复条目, 所以可以随时重跑。

【什么时候要跑】
  CubeMX 重新生成代码之后。本工程 ProjectManager.TargetToolchain = MDK-ARM,
  CubeMX 会重写 MDK-ARM/*.uvprojx, 把这里加的 8 个 UI 分组和 7 条头文件路径
  全部冲掉 —— 表现是编译报一堆 "file not found" 或链接缺一大片 UI 符号。
  重跑本脚本即可恢复。

用法:  python tools/ui_add_to_keil.py
"""
import io
import os
import re
import sys

# 路径由脚本自身位置推导(本脚本放在 <工程>/tools/ 下), 换机器/换目录都不用改
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJ = os.path.join(ROOT, 'MDK-ARM', 'STM32F407VGT6_Template.uvprojx')
UIDIR = os.path.join(ROOT, 'User', 'ui_framework')

# Keil 分组 -> 该组下的源文件目录(相对 ui_framework)
GROUPS = [
    ('UI/common',    ['common']),
    ('UI/port',      ['port', 'port/hal']),
    ('UI/platform',  ['platform']),
    ('UI/lcd_drive', ['lcd_drive']),
    ('UI/ui_dot',    ['ui_dot']),
    ('UI/font',      ['font']),
    ('UI/ui_draw',   ['ui_draw']),
    ('UI/res',       ['res']),
]

# 新增的头文件搜索路径(相对 MDK-ARM 目录)。
# 用正斜杠, 与工程里已有的那批写法保持一致。
INCLUDES = [
    '../User/ui_framework/include',
    '../User/ui_framework/include/common',
    '../User/ui_framework/common',
    '../User/ui_framework/include/ui/cpu/br27',
    '../User/ui_framework/compat',
    '../User/ui_framework/port',
    '../User/ui_framework/port/hal',
]


def collect(subdirs):
    """列出这些子目录下的 .c 文件, 返回 (显示名, 工程相对路径) 列表"""
    out = []
    for sub in subdirs:
        d = os.path.join(UIDIR, sub.replace('/', os.sep))
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith('.c'):
                rel = os.path.join('..', 'User', 'ui_framework',
                                   sub.replace('/', os.sep), name)
                out.append((name, rel))
    return out


def make_group_xml(group_name, files, indent='        '):
    i1 = indent
    i2 = indent + '  '
    i3 = indent + '    '
    i4 = indent + '      '
    lines = ['%s<Group>' % i1,
             '%s<GroupName>%s</GroupName>' % (i2, group_name),
             '%s<Files>' % i2]
    for disp, rel in files:
        lines += ['%s<File>' % i3,
                  '%s<FileName>%s</FileName>' % (i4, disp),
                  '%s<FileType>1</FileType>' % i4,
                  '%s<FilePath>%s</FilePath>' % (i4, rel),
                  '%s</File>' % i3]
    lines += ['%s</Files>' % i2, '%s</Group>' % i1]
    return '\n'.join(lines)


def main():
    s = io.open(PROJ, encoding='utf-8', errors='replace').read()
    orig = s

    # ---- 1) 头文件路径: 两个 Target 各有一个 <IncludePath> ----
    def fix_inc(m):
        body = m.group(1)
        # .uvprojx 里一共有 45 个 <IncludePath>: 2 个是 Target 级(非空, 含工程
        # 主头路径), 另外 43 个是"每文件选项"与汇编器的空标签。
        # 只能改前者 —— 往汇编器配置里塞 C 的头路径会破坏汇编配置。
        if '../Core/Inc' not in body:
            return m.group(0)
        parts = [p for p in body.split(';') if p.strip()]
        added = 0
        for inc in INCLUDES:
            if inc not in parts:
                parts.append(inc)
                added += 1
        return '<IncludePath>%s</IncludePath>' % ';'.join(parts)

    before = s
    s = re.sub(r'<IncludePath>(.*?)</IncludePath>', fix_inc, s, flags=re.S)
    n_inc = sum(1 for _ in re.finditer(r'<IncludePath>[^<]*ui_framework[^<]*</IncludePath>', s))
    print('IncludePath: 改动 %s, 含 ui_framework 的标签 %d 个(应为 2)'
          % ('有' if s != before else '无', n_inc))

    # ---- 2) 源文件分组: 追加到每个 </Groups> 之前 ----
    total_files = 0
    blocks = []
    for gname, subs in GROUPS:
        files = collect(subs)
        total_files += len(files)
        blocks.append(make_group_xml(gname, files))
        print('  %-14s %d 个文件' % (gname, len(files)))

    new_groups = '\n'.join(blocks)

    # 已经加过就先整段删掉, 保证幂等
    for gname, _ in GROUPS:
        pat = r'\s*<Group>\s*<GroupName>%s</GroupName>.*?</Group>' % re.escape(gname)
        s = re.sub(pat, '', s, flags=re.S)

    cnt = s.count('</Groups>')
    s = s.replace('</Groups>', new_groups + '\n      </Groups>')
    print('源文件分组写入 %d 个 Target, 共 %d 个 .c' % (cnt, total_files))

    if s == orig:
        print('无变化')
        return 1

    io.open(PROJ, 'w', encoding='utf-8', newline='\r\n').write(s)
    print('已写入 %s' % PROJ)
    return 0


sys.exit(main())
