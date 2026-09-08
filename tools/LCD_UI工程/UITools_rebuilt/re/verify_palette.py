# -*- coding: utf-8 -*-
"""验证调色板的构成，并把原厂 ResBuilder.exe 里的内置 255 色表提取出来。

规律（三页实测逐字节复现）：

    palette[0]      恒为 0x55AAA5（原厂硬编码）
    palette[1..k]   Resbuilder.xml 里本页 <ColorList> 的颜色，保持原顺序
    palette[k+1..]  内置 255 色表**去掉已用色**之后的前 255-k 项

内置表在 ResBuilder.exe 文件偏移 0x000E8B64，255 项，每项 4 字节 B,G,R,00。

用法:
    python verify_palette.py <工程目录> <ResBuilder.exe> [result.bin]
    python verify_palette.py --emit-header <ResBuilder.exe> <输出.h>
"""
import os
import re
import sys

from res_dump import parse

PALETTE_VA_OFF = 0xE8B64
PALETTE_N = 255
FIRST = '55AAA5'


def read_table(exe):
    b = open(exe, 'rb').read()
    return ['%02X%02X%02X' % (b[PALETTE_VA_OFF + i * 4 + 2],
                              b[PALETTE_VA_OFF + i * 4 + 1],
                              b[PALETTE_VA_OFF + i * 4])
            for i in range(PALETTE_N)]


def emit_header(exe, out):
    cols = read_table(exe)
    lines = [
        '// 由 re/verify_palette.py --emit-header 从原厂 ResBuilder.exe 0x000E8B64 提取。',
        '// 生成的调色板 = { 0x55AAA5 } + 本页 <Color> 列表 + (本表去掉已用色后的前 255-k 项)。',
        '#ifndef DEFAULTPALETTE_H',
        '#define DEFAULTPALETTE_H',
        '',
        '#include <QtGlobal>',
        '',
        'namespace res {',
        '',
        'const quint32 PALETTE_FIRST = 0x55AAA5u;',
        '',
        'const int DEFAULT_PALETTE_COUNT = %d;' % PALETTE_N,
        'const quint32 DEFAULT_PALETTE[DEFAULT_PALETTE_COUNT] = {',
    ]
    for i in range(0, PALETTE_N, 6):
        lines.append('    ' + ' '.join('0x%s,' % c for c in cols[i:i + 6]))
    lines += ['};', '', '} // namespace res', '', '#endif // DEFAULTPALETTE_H']
    open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('写出 %s（%d 项）' % (out, PALETTE_N))
    return 0


def main():
    if sys.argv[1] == '--emit-header':
        return emit_header(sys.argv[2], sys.argv[3])

    proj = os.path.abspath(sys.argv[1])
    exe = os.path.abspath(sys.argv[2])
    resbin = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 \
        else os.path.join(proj, 'result.bin')

    base = read_table(exe)
    xml = open(os.path.join(proj, 'Resbuilder.xml'), encoding='utf-8',
               errors='replace').read()
    used = [re.findall(r'<Color>(.*?)</Color>', m.group(2))
            for m in re.finditer(r'<Page id="(\d+)">(.*?)</Page>', xml, re.S)]

    d = parse(resbin)
    raw = d['raw']
    ok = True
    for i, p in enumerate(d['pages']):
        q = p['palettes'][0]
        b = raw[q['off']:q['off'] + 1024]
        got = ['%02X%02X%02X' % (b[k * 4 + 2], b[k * 4 + 1], b[k * 4]) for k in range(256)]
        u = [c.upper() for c in used[i]]
        rest = [c for c in base if c not in u][:255 - len(u)]
        exp = [FIRST] + u + rest
        same = exp == got
        ok &= same
        print('页%d 用色 %d 个，复现 %s' % (i, len(u), 'OK' if same else 'FAIL'))
        if not same:
            for k in range(256):
                if exp[k] != got[k]:
                    print('   首个不同 @%d 期望 %s 实际 %s' % (k, exp[k], got[k]))
                    break
    print('全部一致' if ok else '有不一致')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
