# -*- coding: utf-8 -*-
"""把「调色板差异」这件事钉住：集合必须相同，差异必须只在顺序上。

    python verify_palette_order.py <本版产物目录> <原厂参考目录>

例：
    python verify_palette_order.py C:\\bt\\chain ..\\..\\ui_128_64_JL02\\模式界面\\project

背景（详见 docs/QTTOOLBIN.md「ColorList 排序」一节）：
  result.bin 里 0x370 起是调色板，每项 4 字节 B G R + 填充；ResBuilder 原样
  照抄 Resbuilder.xml 的 ColorList，不重排。本版与原厂的 ColorList **集合相同、
  顺序不同**，原厂那个顺序试遍了各种假设都找不到规律（很可能是 Qt 容器
  + 随机 hash seed，原厂自己重跑也会变）。

  本工程 104 张图全是 OSD1（1bpp，像素里不存调色板下标），所以顺序对显示
  没有影响。但**集合**要是变了就是真 bug —— 少一个颜色意味着某个控件的
  背景/边框色没被收进去。这个脚本守的就是这条线。

退出码：0 全对；1 有集合差异或多出的差异区间；2 参数/文件问题。
"""
import os
import re
import sys

PAL_OFF = 0x370          # result.bin 里调色板的起点
PAL_ITEM = 4             # 每项 4 字节：B G R + 填充
RESVER_RANGE = (0x0C, 0x10)   # 4 字节 resver，原厂就是随机值


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def colors_of(xml_path):
    """按页取 ColorList。xml 是 UTF-8（原厂如此），历史上本版写过 GBK，两种都认。"""
    with open(xml_path, 'rb') as f:
        raw = f.read()
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError:
        text = raw.decode('gbk', 'replace')
    out = []
    for _, body in re.findall(r'(?s)<Page id="(\d+)">(.*?)</Page>', text):
        out.append(re.findall(r'<Color>([^<]*)</Color>', body))
    return out


def palette_at(buf, off, n):
    out = []
    for i in range(n):
        p = off + i * PAL_ITEM
        if p + 3 > len(buf):
            break
        out.append('%02X%02X%02X' % (buf[p + 2], buf[p + 1], buf[p]))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mine_dir, ref_dir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])

    bad = 0

    # ---- 1. ColorList：逐页比集合 ----
    mx = os.path.join(mine_dir, 'Resbuilder.xml')
    rx = os.path.join(ref_dir, 'Resbuilder.xml')
    if not os.path.exists(mx) or not os.path.exists(rx):
        print('缺 Resbuilder.xml：%s / %s' % (mx, rx))
        return 2
    mine_c, ref_c = colors_of(mx), colors_of(rx)
    if len(mine_c) != len(ref_c):
        print('★ 页数不同：本版 %d / 原厂 %d' % (len(mine_c), len(ref_c)))
        bad += 1
    for i, (a, b) in enumerate(zip(mine_c, ref_c)):
        sa, sb = set(a), set(b)
        if sa == sb and len(a) == len(b):
            same_order = a == b
            print('  页%d ColorList %2d 个，集合一致%s'
                  % (i, len(a), '（顺序也相同）' if same_order else '（顺序不同，已知）'))
        else:
            print('★ 页%d ColorList 集合不一致' % i)
            print('    只在本版: %s' % ' '.join(sorted(sa - sb)))
            print('    只在原厂: %s' % ' '.join(sorted(sb - sa)))
            bad += 1

    # ---- 2. result.bin：差异只许落在 resver 和调色板区 ----
    mb = os.path.join(mine_dir, 'result.bin')
    rb = os.path.join(ref_dir, 'result.bin')
    if not os.path.exists(mb) or not os.path.exists(rb):
        print('缺 result.bin')
        return 2
    a, b = read(mb), read(rb)
    if len(a) != len(b):
        print('★ result.bin 长度不同：%d / %d' % (len(a), len(b)))
        return 1

    total = sum(1 for i in range(len(a)) if a[i] != b[i])

    # 【各页调色板不是连着排的】页0 在 0x370，页1 在 0x10E0 附近 —— 中间隔着
    # 图片数据。所以逐页去 result.bin 里**搜**它自己那串，拿到各页真实起点。
    def find_palette(buf, cs):
        """按该页 ColorList 拼出字节串去找；找不到返回 None。"""
        blob = b''.join(bytes((int(c[4:6], 16), int(c[2:4], 16),
                               int(c[0:2], 16), 0)) for c in cs)
        at = buf.find(blob)
        return None if at < 0 else (at, at + len(blob))

    spans = []
    for i, cs in enumerate(ref_c):
        got = find_palette(b, cs)
        if got is None:
            print('★ 页%d 的调色板在原厂 result.bin 里找不到（格式假设不成立？）' % i)
            bad += 1
        else:
            spans.append(got)
            print('  页%d 调色板位于 0x%X..0x%X' % (i, got[0], got[1] - 1))

    known, unknown = 0, []
    for i in range(len(a)):
        if a[i] == b[i]:
            continue
        if RESVER_RANGE[0] <= i < RESVER_RANGE[1]:
            known += 1
            continue
        if any(lo <= i < hi for lo, hi in spans):
            known += 1
        else:
            unknown.append(i)

    print()
    print('result.bin 共 %d 处差异：resver+调色板可解释 %d 处，无法解释 %d 处'
          % (total, known, len(unknown)))
    if unknown:
        print('★ 有无法解释的差异，前 16 处偏移：%s'
              % ' '.join('0x%X' % u for u in unknown[:16]))
        bad += 1

    print()
    print('不合格 %d 项' % bad)
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
