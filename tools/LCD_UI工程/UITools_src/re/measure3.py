# -*- coding: utf-8 -*-
"""直方图法找界面分界线：统计每列/每行上"整条贯穿的同色"占比。"""
import sys
sys.path.insert(0, r'C:\Users\haihui.qiu\AppData\Local\Temp\claude\D--MyFile-zh-jieli-sdk-demo-JIELI-STM32-UI-STM32F407VGT6-Template\c7a80eea-f78a-4b8f-b408-70ed945ff332\scratchpad')
from crop import load_png
from collections import Counter

W, H, CH, BUF = load_png(sys.argv[1])
X0, Y0, X1, Y1 = (int(v) for v in sys.argv[2:6]) if len(sys.argv) > 5 else (0, 0, W, H)


def px(x, y):
    i = (y * W + x) * CH
    return tuple(BUF[i:i + 3])


print('图 %dx%d，分析区 (%d,%d)-(%d,%d)' % (W, H, X0, Y0, X1, Y1))

print('\n=== 竖直分界：每列"主色一致度"最高的列 ===')
cols = []
for x in range(X0, X1):
    c = Counter(px(x, y) for y in range(Y0, Y1, 2))
    col, n = c.most_common(1)[0]
    cols.append((x, n / len(range(Y0, Y1, 2)), col))
cand = [(x, r, c) for x, r, c in cols if r > 0.80]
# 合并相邻
groups = []
for x, r, c in cand:
    if groups and x - groups[-1][-1][0] <= 2:
        groups[-1].append((x, r, c))
    else:
        groups.append([(x, r, c)])
for g in groups:
    x0, x1 = g[0][0], g[-1][0]
    best = max(g, key=lambda t: t[1])
    print('    x=%4d..%-4d  一致度 %.2f  #%02X%02X%02X' % ((x0, x1, best[1]) + best[2]))

print('\n=== 水平分界：每行"主色一致度"最高的行 ===')
rows = []
for y in range(Y0, Y1):
    c = Counter(px(x, y) for x in range(X0, X1, 2))
    col, n = c.most_common(1)[0]
    rows.append((y, n / len(range(X0, X1, 2)), col))
cand = [(y, r, c) for y, r, c in rows if r > 0.80]
groups = []
for y, r, c in cand:
    if groups and y - groups[-1][-1][0] <= 2:
        groups[-1].append((y, r, c))
    else:
        groups.append([(y, r, c)])
for g in groups:
    y0, y1 = g[0][0], g[-1][0]
    best = max(g, key=lambda t: t[1])
    print('    y=%4d..%-4d  一致度 %.2f  #%02X%02X%02X' % ((y0, y1, best[1]) + best[2]))
