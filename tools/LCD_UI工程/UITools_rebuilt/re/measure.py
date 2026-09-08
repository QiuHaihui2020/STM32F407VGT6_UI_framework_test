# -*- coding: utf-8 -*-
"""从截图上按像素量界面几何：扫行/列的颜色游程，报告分界线位置。"""
import sys
sys.path.insert(0, r'C:\Users\haihui.qiu\AppData\Local\Temp\claude\D--MyFile-zh-jieli-sdk-demo-JIELI-STM32-UI-STM32F407VGT6-Template\c7a80eea-f78a-4b8f-b408-70ed945ff332\scratchpad')
from crop import load_png


class Img:
    def __init__(self, path):
        self.w, self.h, self.ch, self.buf = load_png(path)

    def px(self, x, y):
        i = (y * self.w + x) * self.ch
        return tuple(self.buf[i:i + 3])


def runs(img, fixed, start, end, horizontal=True, minrun=3):
    """沿一行(horizontal)或一列扫描，返回 [(起点, 长度, 颜色)]"""
    out = []
    prev = None
    s = start
    for i in range(start, end):
        c = img.px(i, fixed) if horizontal else img.px(fixed, i)
        if c != prev:
            if prev is not None and i - s >= minrun:
                out.append((s, i - s, prev))
            prev = c
            s = i
    if prev is not None and end - s >= minrun:
        out.append((s, end - s, prev))
    return out


def edges(img, fixed, start, end, horizontal=True):
    """只报"颜色发生变化"的位置，用来找分界线"""
    res = []
    prev = img.px(start, fixed) if horizontal else img.px(fixed, start)
    for i in range(start + 1, end):
        c = img.px(i, fixed) if horizontal else img.px(fixed, i)
        if c != prev:
            res.append((i, prev, c))
            prev = c
    return res


def main():
    path = sys.argv[1]
    img = Img(path)
    print('图 %dx%d' % (img.w, img.h))

    print('\n=== 窗口边界：沿 y=%d 扫最右非桌面像素 ===' % (img.h // 2))
    # 从右往左找窗口右边缘（桌面/其它窗口一般颜色不同）
    print('   行 y=600 的颜色游程（长度>=8）：')
    for s, ln, c in runs(img, 600, 0, img.w, True, 8):
        print('      x=%4d  len=%4d  #%02X%02X%02X' % ((s, ln) + c))

    print('\n=== 竖向分界（沿 y=600 逐像素变色点，只列大跳变）===')
    prev_x = 0
    for x, a, b in edges(img, 600, 0, min(img.w, 1750), True):
        if abs(sum(a) - sum(b)) > 60 and x - prev_x > 3:
            print('      x=%4d  #%02X%02X%02X -> #%02X%02X%02X' % ((x,) + a + b))
            prev_x = x

    print('\n=== 横向分界（沿 x=900 逐像素变色点）===')
    prev_y = 0
    for y, a, b in edges(img, 900, 0, min(img.h, 1040), False):
        if abs(sum(a) - sum(b)) > 60 and y - prev_y > 3:
            print('      y=%4d  #%02X%02X%02X -> #%02X%02X%02X' % ((y,) + a + b))
            prev_y = y

    print('\n=== 工具栏区：沿 x 扫 y=47 的按钮边框 ===')
    for s, ln, c in runs(img, 47, 0, 1200, True, 2):
        if ln >= 4:
            print('      x=%4d len=%3d #%02X%02X%02X' % ((s, ln) + c))

    print('\n=== 树行高：沿 x=%d 扫 y=85..320 的变色点 ===' % 20)
    ys = [y for y, a, b in edges(img, 20, 84, 330, False)]
    print('      变色 y:', ys[:40])

    print('\n=== 控件列表区：沿 y 扫 x=390 的 y=80..390 ===')
    for s, ln, c in runs(img, 390, 78, 392, False, 3):
        print('      y=%4d len=%3d #%02X%02X%02X' % ((s, ln) + c))


if __name__ == '__main__':
    main()
