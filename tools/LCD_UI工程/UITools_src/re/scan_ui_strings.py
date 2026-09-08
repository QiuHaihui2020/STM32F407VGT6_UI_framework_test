# -*- coding: utf-8 -*-
"""把 ui-tools.exe 里的界面用语扫出来 —— 原厂"操作逻辑与限制"的唯一权威来源。

    python scan_ui_strings.py <ui-tools.exe> [输出.txt]

原厂的界面文字**不是** Qt 的 UTF-16 QStringLiteral，而是 UTF-8 的 char* 常量
（多半来自 QString::fromUtf8 / tr 的源串），所以按 UTF-8 扫。

【为什么要这么严的过滤】直接"找连续汉字"会把 x86 指令字节当成汉字：
0xE0..0xEF 开头 + 两个 0x80..0xBF 在机器码里遍地都是，实测能扫出 48 万条垃圾。
所以这里要求一段串同时满足：

  * 前后都以 0x00 收边（真正的 C 字符串常量）
  * 中间只有「ASCII 可见字符」或「合法的 3 字节 UTF-8 汉字/中文标点」，
    出现任何别的字节整段作废
  * 至少 2 个汉字，长度不超过 120

这三条一收，288 条，去掉 Qt 自带的 IDN 顶级域名表（"香港" "台灣" "公司.cn" …）
之后 153 条全是界面用语，一条噪声都没有。

扫出来的东西怎么用，见 docs/FACTORY_UI.md 第 5 节。
"""
import io
import re
import sys

# ASCII 里会出现在界面串中的字符。注意别放进控制字符，那是噪声的主要来源。
ALLOW = set(b" ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            b"0123456789_-.,:;/()[]%+*=#'\"?!<>")

# Qt 自带的 IDN 顶级域名表从这一条开始，后面全是域名，不是界面用语
IDN_TABLE_HEAD = u'军同已愿既星'   # "军同已愿既星..."


def scan(path):
    data = open(path, 'rb').read()
    n = len(data)
    out = []
    i = 0
    while i < n:
        if data[i] == 0:
            i += 1
            continue
        j = i
        buf = bytearray()
        cjk = 0
        bad = False
        while j < n and data[j] != 0:
            b = data[j]
            if b in ALLOW:
                buf.append(b)
                j += 1
            elif (0xe0 <= b <= 0xef and j + 2 < n
                  and 0x80 <= data[j + 1] <= 0xbf and 0x80 <= data[j + 2] <= 0xbf):
                buf += data[j:j + 3]
                j += 3
                cjk += 1
            else:
                bad = True
                break
        if not bad and cjk >= 2 and j > i:
            try:
                s = buf.decode('utf-8')
            except UnicodeDecodeError:
                s = None
            if s and re.search(u'[一-鿿]', s) and len(s) <= 120:
                out.append(s)
        i = j + 1

    seen = set()
    uniq = []
    for s in out:
        if s in seen:
            continue
        seen.add(s)
        uniq.append(s)
    return uniq


def main():
    if len(sys.argv) < 2:
        sys.stdout.write(__doc__)
        return 2
    all_str = scan(sys.argv[1])
    # 截掉尾部的域名表
    ui = all_str
    for k, s in enumerate(all_str):
        if s.startswith(IDN_TABLE_HEAD):
            ui = all_str[:k]
            break
    text = u'\n'.join(ui) + u'\n'
    if len(sys.argv) > 2:
        io.open(sys.argv[2], 'w', encoding='utf-8').write(text)
        sys.stdout.write('%d 条界面用语 -> %s\n' % (len(ui), sys.argv[2]))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
