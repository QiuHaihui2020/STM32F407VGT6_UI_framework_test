# -*- coding: utf-8 -*-
"""第四轮：把重建要用的具体像素数字一次性量清楚。"""
import sys
sys.path.insert(0, r'C:\Users\haihui.qiu\AppData\Local\Temp\claude\D--MyFile-zh-jieli-sdk-demo-JIELI-STM32-UI-STM32F407VGT6-Template\c7a80eea-f78a-4b8f-b408-70ed945ff332\scratchpad')
from crop import load_png

W, H, CH, BUF = load_png(sys.argv[1])


def px(x, y):
    i = (y * W + x) * CH
    return tuple(BUF[i:i + 3])


def dark_rows(x0, x1, y0, y1, thr=380):
    """返回 [(起,止)]：这些行里存在深色像素（= 有文字/线条）"""
    out, inrun, start = [], False, 0
    for y in range(y0, y1):
        n = sum(1 for x in range(x0, x1) if sum(px(x, y)) < thr)
        if n and not inrun:
            start, inrun = y, True
        elif not n and inrun:
            out.append((start, y - 1))
            inrun = False
    if inrun:
        out.append((start, y1 - 1))
    return out


def dark_cols(y0, y1, x0, x1, thr=380):
    out, inrun, start = [], False, 0
    for x in range(x0, x1):
        n = sum(1 for y in range(y0, y1) if sum(px(x, y)) < thr)
        if n and not inrun:
            start, inrun = x, True
        elif not n and inrun:
            out.append((start, x - 1))
            inrun = False
    if inrun:
        out.append((start, x1 - 1))
    return out


print('=== A. 工具栏 ===')
print('  按钮框（y=32..60 的深色列区间，即每个按钮的左右边框）：')
segs = dark_cols(33, 60, 0, 1200, 300)
merged = []
for a, b in segs:
    if merged and a - merged[-1][1] <= 3:
        merged[-1][1] = b
    else:
        merged.append([a, b])
print('   ', [(a, b, b - a + 1) for a, b in merged][:30])
print('  纵向：y 32..62 上 x=20 的颜色')
for y in range(30, 64):
    if px(20, y) != px(20, y - 1):
        print('     y=%d -> #%02X%02X%02X' % ((y,) + px(20, y)))

print('\n=== B. 树 ===')
rows = dark_rows(12, 262, 84, 400)
print('  文字行区间（前 14）:', rows[:14])
if len(rows) > 3:
    print('  行距:', [rows[i + 1][0] - rows[i][0] for i in range(min(10, len(rows) - 1))])
print('  表头分隔（y=93 上的竖线 x）:')
hdr = [x for x in range(8, 268) if sum(px(x, 88)) < 600 and sum(px(x, 86)) < 600]
print('   ', hdr[:20])
print('  表头底边 y：')
for y in range(84, 110):
    row = [px(x, y) for x in range(12, 260, 8)]
    if all(sum(c) < 700 for c in row):
        print('     y=%d 是一条横线' % y)

print('\n=== C. 控件列表 ===')
print('  组框上下边（x=276 竖扫，找 #94AA94/#899D89 类边框）：')
for y in range(78, 400):
    c = px(276, y)
    if abs(c[0] - 0x89) < 30 and abs(c[1] - 0x9D) < 30:
        print('     y=%d #%02X%02X%02X' % ((y,) + c))
print('  "图层"大按钮：y=95..175 上 x=300 的颜色变化')
for y in range(90, 200):
    if px(300, y) != px(300, y - 1):
        print('     y=%d -> #%02X%02X%02X' % ((y,) + px(300, y)))
print('  网格按钮：y=225 行上按钮边框 x')
print('   ', dark_cols(215, 240, 275, 505, 500)[:12])
print('  网格按钮行：x=320 列上 y=210..320 的边框')
print('   ', dark_rows(300, 340, 205, 320, 500)[:12])

print('\n=== D. 属性区 ===')
print('  第二列里 y=380..1010 的横向边框（x=290..500 全宽同色行）：')
for y in range(380, 1010):
    row = [px(x, y) for x in range(292, 496, 4)]
    if len(set(row)) == 1 and sum(row[0]) < 700:
        print('     y=%d #%02X%02X%02X' % ((y,) + row[0]))

print('\n=== E. 页面栏 ===')
print('  x=1440..1690 上 y=78..400 的内容行：')
print('   ', dark_rows(1440, 1685, 78, 400, 600)[:12])
print('  第一页渲染的左右边：y=110 上 x=1433..1690 的变化')
for x in range(1434, 1690):
    if px(x, 110) != px(x - 1, 110):
        print('     x=%d -> #%02X%02X%02X' % ((x,) + px(x, 110)))

print('\n=== F. 画布里的页面 ===')
print('  y=110 横扫 x=505..700 变化：')
for x in range(506, 700):
    if px(x, 110) != px(x - 1, 110):
        print('     x=%d -> #%02X%02X%02X' % ((x,) + px(x, 110)))
print('  x=560 竖扫 y=62..180 变化：')
for y in range(63, 180):
    if px(560, y) != px(560, y - 1):
        print('     y=%d -> #%02X%02X%02X' % ((y,) + px(560, y)))
