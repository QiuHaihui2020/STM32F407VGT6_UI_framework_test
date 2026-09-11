# -*- coding: utf-8 -*-
"""把 .sty 数据区里的每一个数据块都走一遍，统计字节覆盖率。

100% 覆盖 = 这个格式没有剩下不认识的东西，可以照着写生成器。

指针字段的位置见各控件的结构说明，
并已用页尾那张重定位表交叉验证过（见 reloc_probe.py）：

  type 3  NewLayout      +12 css  +16 action  +20 ctrl
  type 4  NewLayer       +12 css  +20 action  +24 layout      （+16 是 u8 format）
  type 5  NewList/Grid   +12 css  +20 action  +24 info
  type 8  ImageList/pic  +12 css  +24 normal_img +28 highlight_img +32 action
  type 9  Battery        +12 css  +16 normal +20 charge +24 action
  type 10 Time/Watch     +12 css  +92 action
  type 12 Text           +12 css  +40 str  +44 action
  type 15 Number         +12 css  +92 action

数据块：
  element_css1        36 B 定长
  ui_image_list       u16 num; u16 image[num]
  ui_text_list        u16 num; u16 str[num]
  element_event_action u16 num; { u16 event; u16 action; s32 id; u8 argc; char argv[argc] } * num
"""
import struct
import sys
from collections import defaultdict
from sty_dump import parse, CTRL_TYPE

# 指针字段偏移（相对控件记录起点）
PTRS = {
    3:  [('css', 12), ('action', 16), ('ctrl', 20)],
    4:  [('css', 12), ('action', 20), ('layout', 24)],
    5:  [('css', 12), ('action', 20), ('info', 24)],
    7:  [('css', 12), ('action', 24), ('ctrl', 28)],
    8:  [('css', 12), ('img_normal', 24), ('img_high', 28), ('action', 32)],
    9:  [('css', 12), ('img_normal', 16), ('img_charge', 20), ('action', 24)],
    10: [('css', 12), ('action', 92)],
    12: [('css', 12), ('strlist', 40), ('action', 44)],
    15: [('css', 12), ('action', 92)],
    33: [('css', 12), ('ctrl', 20)],      # vslider 是组合控件，+20 指向控件区不是数据区
    34: [('css', 12)],
    35: [('css', 12)],
    36: [('css', 12)],
}
CSS_SIZE = 36
NONE = (0, 0xFFFFFFFF)


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def s32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def main():
    path = sys.argv[1]
    raw = open(path, 'rb').read()
    doc = parse(path)

    total_unknown = 0
    for w in doc['windows']:
        base = w['offset']
        lo = w['scan_stopped_at']          # 数据区起点 = 控件区结束
        hi = w['table_ptr']                # 数据区终点 = 索引表开始
        covered = bytearray(hi - lo)       # 覆盖位图
        kinds = defaultdict(int)
        sizes = defaultdict(int)
        problems = []

        def mark(off, size, kind):
            """off 是绝对文件偏移"""
            if off < lo or off + size > hi:
                problems.append('%s @0x%X size %d 越出数据区' % (kind, off, size))
                return False
            for i in range(off - lo, off - lo + size):
                if covered[i]:
                    kinds[kind + '(重叠)'] += 1
                    return False
                covered[i] = 1
            kinds[kind] += 1
            sizes[kind] += size
            return True

        def resolve(ptr):
            if ptr in NONE:
                return None
            return base + (ptr & 0xFFFF)

        for c in w['controls']:
            pl = bytes.fromhex(c['payload'])
            rec = bytes([c['type'], c['ctrl_num'], c['css_num'], c['len'],
                         c['page'], 0xFF, 0xFF, 0xFF]) + \
                  struct.pack('<iI', c['id'], c['css_off']) + pl
            for name, off in PTRS.get(c['type'], [('css', 12)]):
                if off + 4 > len(rec):
                    continue
                a = resolve(u32(rec, off))
                if a is None:
                    continue
                if name == 'css':
                    mark(a, CSS_SIZE, 'css')
                elif name in ('img_normal', 'img_high', 'img_charge'):
                    n = u16(raw, a)
                    mark(a, 2 + 2 * n, 'image_list')
                elif name == 'strlist':
                    n = u16(raw, a)
                    mark(a, 2 + 2 * n, 'text_list')
                elif name == 'action':
                    n = u16(raw, a)
                    size = 2
                    p = a + 2
                    ok = True
                    for _ in range(n):
                        if p + 9 > hi:
                            ok = False
                            break
                        argc = raw[p + 8]
                        step = 9 + argc
                        step = (step + 3) & ~3        # 4 字节对齐（先按对齐试）
                        size += step
                        p += step
                    if ok:
                        mark(a, size, 'action')
                    else:
                        problems.append('action @0x%X 解析越界' % a)
                # ctrl / layout / info 指向的是控件记录本身，不在数据区

        # 第二遍：把剩下的按"填充 / 空列表"归类。
        # 实测规律：每个数据块按 4 字节对齐，尾部用 0xFF 填齐；
        # 空的 image_list / text_list 即使没有指针指向，也照样写一个 0x0000。
        i = 0
        while i < len(covered):
            if covered[i]:
                i += 1
                continue
            j = i
            while j < len(covered) and not covered[j]:
                j += 1
            seg = raw[lo + i:lo + j]
            k = 0
            while k < len(seg):
                if seg[k] == 0xFF:
                    m = k
                    while m < len(seg) and seg[m] == 0xFF:
                        m += 1
                    kinds['padding(0xFF)'] += 1
                    sizes['padding(0xFF)'] += m - k
                    for t in range(i + k, i + m):
                        covered[t] = 1
                    k = m
                elif seg[k:k + 2] == bytes(2):
                    kinds['empty_list'] += 1
                    sizes['empty_list'] += 2
                    covered[i + k] = covered[i + k + 1] = 1
                    k += 2
                else:
                    k += 1
            i = j

        used = sum(covered)
        gap = len(covered) - used
        total_unknown += gap
        print('页 @0x%X  数据区 0x%X..0x%X (%d B)  已解释 %d B  未解释 %d B  覆盖 %.1f%%'
              % (base, lo, hi, hi - lo, used, gap, 100.0 * used / max(1, hi - lo)))
        for k in sorted(kinds):
            print('     %-14s %4d 个  %6d B' % (k, kinds[k], sizes[k]))
        if problems:
            print('     问题 %d 条，前 3:' % len(problems))
            for x in problems[:3]:
                print('        ' + x)
        # 未覆盖区间
        runs = []
        i = 0
        while i < len(covered):
            if not covered[i]:
                j = i
                while j < len(covered) and not covered[j]:
                    j += 1
                runs.append((lo + i, j - i))
                i = j
            else:
                i += 1
        if runs:
            print('     未覆盖区段 %d 段，最大 %d B，前 5: %s'
                  % (len(runs), max(r[1] for r in runs),
                     ', '.join('0x%X+%d' % r for r in runs[:5])))
    print('\n总未解释字节: %d' % total_unknown)
    return 0 if total_unknown == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
