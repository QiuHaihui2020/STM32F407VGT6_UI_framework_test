# -*- coding: utf-8 -*-
"""Qt5 QJsonDocument::toJson(Indented) 的逐字节复刻。

用途：在没装 Qt 的机器上先验证「原厂工程 json 就是 Qt 写出来的」，
以及 src/core/ProjectModel.cpp 的保真往返能不能达到字节一致。
C++ 那边直接用 Qt，本文件只是等价对照实现。

规则取自 Qt 5.15 src/corelib/serialization/qjsonwriter.cpp：
  - 缩进 4 空格；对象/数组即使为空也换行，写成 "{\\n<indent>}" / "[\\n<indent>]"
  - 对象键按 QString（UTF-16 码元）序排列 —— QJsonObject 本来就是有序容器
  - 整数值打印成整数，不带小数点
  - 字符串只转义 " \\ 和 <0x20 的控制字符；非 ASCII 直接输出 UTF-8
  - 文档末尾补一个 '\\n'
"""
import json
import sys


def _esc(s):
    out = []
    for ch in s:
        o = ord(ch)
        if o > 0x7F:
            out.append(ch)                    # 非 ASCII 原样，最终 UTF-8 编码
        elif ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '\b':
            out.append('\\b')
        elif ch == '\f':
            out.append('\\f')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\r':
            out.append('\\r')
        elif ch == '\t':
            out.append('\\t')
        elif o < 0x20:
            out.append('\\u%04x' % o)
        else:
            out.append(ch)
    return ''.join(out)


def _num(v):
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, int):
        return str(v)
    # Qt: 能无损表示成 <=2^53 的整数就按整数打印
    if v == int(v) and abs(v) <= (1 << 53):
        return str(int(v))
    return repr(v)


def _val(v, indent, out):
    if v is None:
        out.append('null')
    elif isinstance(v, bool):
        out.append('true' if v else 'false')
    elif isinstance(v, (int, float)):
        out.append(_num(v))
    elif isinstance(v, str):
        out.append('"' + _esc(v) + '"')
    elif isinstance(v, dict):
        out.append('{\n')
        _obj(v, indent + 1, out)
        out.append(' ' * (4 * indent))
        out.append('}')
    elif isinstance(v, list):
        out.append('[\n')
        _arr(v, indent + 1, out)
        out.append(' ' * (4 * indent))
        out.append(']')
    else:
        raise TypeError(type(v))


def _obj(d, indent, out):
    pad = ' ' * (4 * indent)
    keys = sorted(d.keys())          # QJsonObject 按键有序
    for i, k in enumerate(keys):
        out.append(pad)
        out.append('"' + _esc(k) + '": ')
        _val(d[k], indent, out)
        out.append('\n' if i == len(keys) - 1 else ',\n')


def _arr(a, indent, out):
    pad = ' ' * (4 * indent)
    for i, v in enumerate(a):
        out.append(pad)
        _val(v, indent, out)
        out.append('\n' if i == len(a) - 1 else ',\n')


def to_json(doc):
    """返回 bytes，与 QJsonDocument(doc).toJson(Indented) 一致。"""
    out = []
    if isinstance(doc, list):
        out.append('[\n')
        _arr(doc, 1, out)
        out.append(']')
    else:
        out.append('{\n')
        _obj(doc, 1, out)
        out.append('}')
    out.append('\n')
    return ''.join(out).encode('utf-8')


def main():
    path = sys.argv[1]
    orig = open(path, 'rb').read()
    doc = json.loads(orig.decode('utf-8'))
    again = to_json(doc)
    if again == orig:
        print('字节一致：%d B —— 原厂工程 json 确实就是 Qt 的 toJson(Indented) 输出' % len(orig))
        return 0
    print('不一致：原 %d B，复刻 %d B' % (len(orig), len(again)))
    n = min(len(orig), len(again))
    for i in range(n):
        if orig[i] != again[i]:
            a = max(0, i - 80)
            print('首个差异 @%d' % i)
            print('  原  : %r' % orig[a:i + 80])
            print('  复刻: %r' % again[a:i + 80])
            break
    return 1


if __name__ == '__main__':
    sys.exit(main())
