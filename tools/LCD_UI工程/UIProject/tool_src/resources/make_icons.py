# -*- coding: utf-8 -*-
"""生成 resources/icons/ 下的全套界面图标。

【为什么用脚本画而不是放一堆 png】
图标是这套工具自己的一部分，源头就该在仓库里、能改能重出。这个脚本就是源文件：
每个图标都在一张 24x24 的网格上用直线/圆/圆角矩形描出来，8 倍超采样再缩到
目标尺寸，所以边缘是干净的。改颜色、改线宽、加一个新图标都只改这里。

    python make_icons.py            # 重新生成 icons/ 下全部 png
    python make_icons.py --sheet    # 另外出一张 _sheet.png 便于整体看效果

风格：线性图标，2 单位线宽，圆角端点；主色深蓝灰，动作部分用蓝色点出来，
删除类用红色。这样一排工具栏按钮扫一眼就能分清，又不会花。
"""
import math
import os
import sys

from PIL import Image, ImageDraw

# ---- 画布与配色 -------------------------------------------------------------
GRID = 24            # 设计网格：所有坐标都按 24x24 写
SS = 32              # 超采样倍率（每个网格单位 32 像素）
OUT = 128            # 输出边长
W = 2.0              # 默认线宽（网格单位）

INK = (0x37, 0x47, 0x4F, 255)   # 主色：深蓝灰
ACC = (0x1E, 0x88, 0xE5, 255)   # 强调：蓝
RED = (0xE5, 0x39, 0x35, 255)   # 删除
GRN = (0x43, 0xA0, 0x47, 255)   # 新增
DIM = (0x37, 0x47, 0x4F, 34)    # 灭掉的点：只要一点点，不能盖过亮点
CLR = (0, 0, 0, 0)              # 透明（用来"擦"出缺口）


def _p(v):
    return v * SS


# ---- 基本笔画 ---------------------------------------------------------------
def cap(d, x, y, w, color):
    """圆端点。PIL 的 line 没有 round cap，自己补一个圆。"""
    r = w * SS / 2.0
    d.ellipse([_p(x) - r, _p(y) - r, _p(x) + r, _p(y) + r], fill=color)


def poly(d, pts, color=INK, w=W, closed=False, caps=True):
    """折线。joint='curve' 让拐角圆滑，两端再补圆点。"""
    q = [(_p(x), _p(y)) for x, y in pts]
    if closed:
        q.append(q[0])
    d.line(q, fill=color, width=max(1, int(round(w * SS))), joint='curve')
    if caps:
        cap(d, pts[0][0], pts[0][1], w, color)
        cap(d, pts[-1][0], pts[-1][1], w, color)


def circle(d, cx, cy, r, color=INK, w=W, fill=None):
    box = [_p(cx - r), _p(cy - r), _p(cx + r), _p(cy + r)]
    d.ellipse(box, outline=None if fill else color, fill=fill,
              width=0 if fill else max(1, int(round(w * SS))))
    if fill is None:
        d.ellipse(box, outline=color, width=max(1, int(round(w * SS))))


def dot(d, cx, cy, r, color=INK):
    d.ellipse([_p(cx - r), _p(cy - r), _p(cx + r), _p(cy + r)], fill=color)


def rrect(d, x0, y0, x1, y1, r, color=INK, w=W, fill=None):
    d.rounded_rectangle([_p(x0), _p(y0), _p(x1), _p(y1)], radius=_p(r),
                        outline=color, width=max(1, int(round(w * SS))),
                        fill=fill)


def fill_rrect(d, x0, y0, x1, y1, r, color):
    d.rounded_rectangle([_p(x0), _p(y0), _p(x1), _p(y1)], radius=_p(r),
                        fill=color)


def quad(p0, p1, p2, n=48):
    """二次贝塞尔采样成折线 —— PIL 不会画曲线，只能自己算点。"""
    out = []
    for i in range(n + 1):
        t = i / float(n)
        u = 1.0 - t
        out.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                    u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))
    return out


def plus(d, cx, cy, r, color, w=W):
    poly(d, [(cx - r, cy), (cx + r, cy)], color, w)
    poly(d, [(cx, cy - r), (cx, cy + r)], color, w)


def cross(d, cx, cy, r, color, w=W):
    k = r * 0.72
    poly(d, [(cx - k, cy - k), (cx + k, cy + k)], color, w)
    poly(d, [(cx + k, cy - k), (cx - k, cy + k)], color, w)


def badge(d, cx, cy, kind):
    """右下角的小圆徽标：新增/删除。先擦出一圈底，再画，才不会和主体糊在一起。"""
    dot(d, cx, cy, 6.2, CLR)
    color = GRN if kind == '+' else RED
    dot(d, cx, cy, 5.2, color)
    if kind == '+':
        plus(d, cx, cy, 2.6, (255, 255, 255, 255), 1.7)
    else:
        poly(d, [(cx - 2.6, cy), (cx + 2.6, cy)], (255, 255, 255, 255), 1.7)


# ---- 逐个图标 ---------------------------------------------------------------
def i_project_new(d):
    """新建工程：一页纸 + 新增徽标"""
    poly(d, [(4.5, 21), (4.5, 3), (13, 3), (17.5, 7.5), (17.5, 21), (4.5, 21)],
         INK, W, caps=False)
    poly(d, [(13, 3), (13, 7.5), (17.5, 7.5)], INK, 1.6)
    badge(d, 18, 18.5, '+')


def i_project_open(d):
    """打开工程：翻开的文件夹"""
    poly(d, [(2.5, 19.5), (2.5, 5.5), (9, 5.5), (11.5, 8.5), (19, 8.5), (19, 11)],
         INK, W, caps=False)
    poly(d, [(2.5, 19.5), (6, 11.5), (22.5, 11.5), (19, 19.5), (2.5, 19.5)],
         INK, W, caps=False)


def i_project_save(d):
    """保存：软盘"""
    poly(d, [(3.5, 3.5), (16.5, 3.5), (20.5, 7.5), (20.5, 20.5), (3.5, 20.5),
             (3.5, 3.5)], INK, W, caps=False)
    poly(d, [(8, 3.5), (8, 9), (15.5, 9), (15.5, 3.5)], ACC, 1.8, caps=False)
    poly(d, [(7, 20.5), (7, 13.5), (17, 13.5), (17, 20.5)], INK, 1.8, caps=False)


def i_project_save_as(d):
    """另存为：软盘 + 铅笔"""
    poly(d, [(3.5, 3.5), (13.5, 3.5), (16.5, 6.5), (16.5, 13), (3.5, 13),
             (3.5, 3.5)], INK, W, caps=False)
    poly(d, [(7, 3.5), (7, 8), (13, 8), (13, 3.5)], INK, 1.6, caps=False)
    # 铅笔：笔身 + 笔尖，描成一圈轮廓才认得出是铅笔
    poly(d, [(11.4, 22.4), (13.0, 18.6), (19.4, 12.2), (21.8, 14.6),
             (15.4, 21.0), (11.4, 22.4)], ACC, 1.7, closed=True, caps=False)
    poly(d, [(13.0, 18.6), (15.4, 21.0)], ACC, 1.3)


def _screen(d):
    rrect(d, 2.5, 3.5, 21.5, 16.5, 2, INK, W)
    poly(d, [(9, 20.5), (15, 20.5)], INK, W)
    poly(d, [(12, 16.5), (12, 20.5)], INK, W)


def i_page_new(d):
    """新建页面：屏 + 加号"""
    _screen(d)
    plus(d, 12, 10, 3.6, ACC, 2.2)


def i_page_delete(d):
    """删除页面：屏 + 减号"""
    _screen(d)
    poly(d, [(8.4, 10), (15.6, 10)], RED, 2.2)


def i_export(d):
    """资源导出：从箱子里出来的箭头"""
    poly(d, [(3.5, 12.5), (3.5, 20.5), (20.5, 20.5), (20.5, 12.5)], INK, W,
         caps=False)
    poly(d, [(12, 16), (12, 3.5)], ACC, 2.2)
    poly(d, [(7.6, 8), (12, 3.5), (16.4, 8)], ACC, 2.2, caps=False)
    cap(d, 7.6, 8, 2.2, ACC)
    cap(d, 16.4, 8, 2.2, ACC)


def i_screenshot(d):
    """截屏：取景框四角 + 快门"""
    for a, b, c in [((3.5, 8), (3.5, 3.5), (8, 3.5)),
                    ((16, 3.5), (20.5, 3.5), (20.5, 8)),
                    ((20.5, 16), (20.5, 20.5), (16, 20.5)),
                    ((8, 20.5), (3.5, 20.5), (3.5, 16))]:
        poly(d, [a, b, c], INK, 2.2, caps=False)
        cap(d, a[0], a[1], 2.2, INK)
        cap(d, c[0], c[1], 2.2, INK)
    circle(d, 12, 12, 3.8, ACC, 2.2)


def i_settings(d):
    """全局设置：齿轮。按齿形描一圈轮廓，比八根放射线像齿轮得多。"""
    teeth, ro, ri = 8, 9.6, 6.9
    half = math.radians(360.0 / teeth / 4.0)      # 齿顶占 1/4 齿距
    pts = []
    for k in range(teeth):
        a = math.radians(k * 360.0 / teeth)
        for r, ang in ((ri, a - half * 2.0), (ro, a - half),
                       (ro, a + half), (ri, a + half * 2.0)):
            pts.append((12 + r * math.cos(ang), 12 + r * math.sin(ang)))
    poly(d, pts, INK, 1.9, closed=True, caps=False)
    circle(d, 12, 12, 3.4, INK, 1.9)
    dot(d, 12, 12, 1.5, ACC)


def i_resize(d):
    """工程缩放：外框 + 对角双箭头"""
    rrect(d, 2.5, 2.5, 21.5, 21.5, 2, INK, 1.8)
    poly(d, [(7.5, 7.5), (16.5, 16.5)], ACC, 2.0)
    poly(d, [(7.5, 12), (7.5, 7.5), (12, 7.5)], ACC, 2.0, caps=False)
    poly(d, [(16.5, 12), (16.5, 16.5), (12, 16.5)], ACC, 2.0, caps=False)


def i_about(d):
    """关于：信息圆"""
    circle(d, 12, 12, 9.2, INK, 2.0)
    dot(d, 12, 7.3, 1.35, ACC)
    poly(d, [(12, 10.8), (12, 17)], ACC, 2.2)


def i_row_add(d):
    circle(d, 12, 12, 8.6, INK, 2.0)
    plus(d, 12, 12, 4.0, GRN, 2.2)


def i_row_delete(d):
    circle(d, 12, 12, 8.6, INK, 2.0)
    cross(d, 12, 12, 4.6, RED, 2.2)


def i_row_up(d):
    poly(d, [(12, 19.5), (12, 5.5)], INK, 2.2)
    poly(d, [(6.6, 10.9), (12, 5.5), (17.4, 10.9)], INK, 2.2, caps=False)
    cap(d, 6.6, 10.9, 2.2, INK)
    cap(d, 17.4, 10.9, 2.2, INK)


def i_row_down(d):
    poly(d, [(12, 4.5), (12, 18.5)], INK, 2.2)
    poly(d, [(6.6, 13.1), (12, 18.5), (17.4, 13.1)], INK, 2.2, caps=False)
    cap(d, 6.6, 13.1, 2.2, INK)
    cap(d, 17.4, 13.1, 2.2, INK)


def i_move_up(d):
    poly(d, [(5, 15.5), (12, 8.5), (19, 15.5)], ACC, 2.8)


def i_move_down(d):
    poly(d, [(5, 8.5), (12, 15.5), (19, 8.5)], ACC, 2.8)


def _eye_shape(d, color=INK, w=2.0):
    poly(d, quad((2.3, 12), (12, 3.2), (21.7, 12)), color, w, caps=False)
    poly(d, quad((2.3, 12), (12, 20.8), (21.7, 12)), color, w, caps=False)


def i_eye(d):
    """可见"""
    _eye_shape(d)
    circle(d, 12, 12, 3.3, INK, 1.8)
    dot(d, 12, 12, 1.5, ACC)


def i_eye_off(d):
    """隐藏：眼睛被划掉。先擦一道窄缺口，斜线才不会和眼睛糊在一起。
    按通行画法不画瞳孔 —— 划掉的眼睛里再放个瞳孔反而看不清。"""
    _eye_shape(d)
    poly(d, [(4.0, 20.4), (20.0, 4.0)], CLR, 3.4, caps=False)
    poly(d, [(4.0, 20.4), (20.0, 4.0)], RED, 2.2)


def i_browse(d):
    """选文件的 [...] 按钮"""
    for x in (5.6, 12.0, 18.4):
        dot(d, x, 12, 2.2, INK)


def i_color(d):
    """选颜色：四分色块"""
    box = [_p(3.2), _p(3.2), _p(20.8), _p(20.8)]
    for start, col in ((180, (0xE5, 0x39, 0x35, 255)),
                       (270, (0xFB, 0xC0, 0x2D, 255)),
                       (0, (0x43, 0xA0, 0x47, 255)),
                       (90, (0x1E, 0x88, 0xE5, 255))):
        d.pieslice(box, start, start + 90, fill=col)
    circle(d, 12, 12, 8.8, INK, 1.8)


LOGO = [
    "#...#.###",
    "#...#..#.",
    "#...#..#.",
    "#...#..#.",
    "#...#..#.",
    "#...#..#.",
    ".###..###",
]


def i_logo(d):
    """关于对话框那个标志：一块点阵屏，亮出 UI 两个字。
    点位按屏内可用区自动居中排，改 LOGO 里的图案不用重新算坐标。"""
    x0, y0, x1, y1, pad = 1.4, 2.6, 22.6, 21.4, 1.6
    rrect(d, x0, y0, x1, y1, 2.0, INK, 1.7)
    cols, rows = len(LOGO[0]), len(LOGO)
    sx = (x1 - x0 - 2 * pad) / cols
    sy = (y1 - y0 - 2 * pad) / rows
    lit = min(sx, sy) * 0.30
    for r, row in enumerate(LOGO):
        for c, ch in enumerate(row):
            x = x0 + pad + (c + 0.5) * sx
            y = y0 + pad + (r + 0.5) * sy
            dot(d, x, y, lit if ch == '#' else lit * 0.28,
                ACC if ch == '#' else DIM)


def i_app(d):
    """应用图标：一块亮着的点阵屏，屏上是一版界面 —— 左边一个写着 UI 的文字
    控件正被选中（蓝框 + 四角手柄），右边两行文字，底下一条。

    三样东西各说一句话：点阵屏 = 面向什么设备；屏上那两个**点阵字** = 编的是
    屏上显示的内容；蓝色选中框 + 手柄 = 这是个能摆能拖的编辑器，不是查看器。

    【字为什么要画成一颗颗点】屏就是点阵屏，字在上面本来就是由亮点拼出来的。
    画成实心笔画看着更清楚，但那不是这块屏上会出现的东西。为了点还看得见，
    选中的那个控件占了屏的一多半，右边的文字行相应收窄。

    配色跟真机走：屏底是 STN 那种橄榄绿，点亮是黄绿，选中框用界面强调色。
    字模复用 LOGO（关于框那块屏用的是同一份），改一处两处都变。"""
    SCR = (0x46, 0x4B, 0x0E, 255)
    LIT = (0xCD, 0xE0, 0x00, 255)
    fill_rrect(d, 0.6, 2.4, 23.4, 21.6, 3.4, (0x22, 0x2C, 0x31, 255))
    fill_rrect(d, 2.4, 4.2, 21.6, 19.8, 1.2, SCR)

    def bar(x0, y0, x1, y1):
        d.rectangle([_p(x0), _p(y0), _p(x1), _p(y1)], fill=LIT)

    # 左边那个文字控件：UI 两个点阵字
    tx0, ty0, tx1, ty1 = 5.0, 7.2, 12.6, 13.2
    cols, rows = len(LOGO[0]), len(LOGO)
    cell = min((tx1 - tx0) / cols, (ty1 - ty0) / rows)
    ox = (tx0 + tx1) / 2.0 - cols * cell / 2.0
    oy = (ty0 + ty1) / 2.0 - rows * cell / 2.0
    dotsz = cell * 0.86
    for r, row in enumerate(LOGO):
        for c, ch in enumerate(row):
            if ch != '#':
                continue
            x = ox + c * cell + (cell - dotsz) / 2.0
            y = oy + r * cell + (cell - dotsz) / 2.0
            d.rectangle([_p(x), _p(y), _p(x + dotsz), _p(y + dotsz)], fill=LIT)

    # 右边两行文字 + 底下一条
    bar(14.8, 8.5, 19.4, 9.7)
    bar(14.8, 11.1, 18.0, 12.3)
    bar(4.2, 16.6, 19.4, 17.9)

    # 选中框：套住左边那个文字控件，四角各一个手柄
    x0, y0, x1, y1 = 4.2, 6.4, 13.4, 14.0
    poly(d, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], ACC, 0.9,
         closed=True, caps=False)
    for hx, hy in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        d.rectangle([_p(hx - 0.95), _p(hy - 0.95),
                     _p(hx + 0.95), _p(hy + 0.95)], fill=ACC)


ICONS = [
    ('app',             i_app),
    ('project-new',     i_project_new),
    ('project-open',    i_project_open),
    ('project-save',    i_project_save),
    ('project-save-as', i_project_save_as),
    ('page-new',        i_page_new),
    ('page-delete',     i_page_delete),
    ('export',          i_export),
    ('screenshot',      i_screenshot),
    ('settings',        i_settings),
    ('resize',          i_resize),
    ('about',           i_about),
    ('row-add',         i_row_add),
    ('row-delete',      i_row_delete),
    ('row-up',          i_row_up),
    ('row-down',        i_row_down),
    ('move-up',         i_move_up),
    ('move-down',       i_move_down),
    ('eye',             i_eye),
    ('eye-off',         i_eye_off),
    ('browse',          i_browse),
    ('color',           i_color),
    ('logo',            i_logo),
]

SIZES = {'logo': 256, 'app': 256}


def render(fn, size):
    img = Image.new('RGBA', (GRID * SS, GRID * SS), (0, 0, 0, 0))
    fn(ImageDraw.Draw(img))
    return img.resize((size, size), Image.LANCZOS)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, 'icons')
    if not os.path.isdir(outdir):
        os.makedirs(outdir)
    made = []
    for name, fn in ICONS:
        size = SIZES.get(name, OUT)
        im = render(fn, size)
        im.save(os.path.join(outdir, name + '.png'))
        made.append((name, size))
        print('  %-18s %dx%d' % (name, size, size))
    print('共 %d 个' % len(made))

    # exe 的图标：多尺寸 .ico，任务栏/资源管理器各取所需
    render(i_app, 256).save(
        os.path.join(here, 'app.ico'),
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
               (64, 64), (128, 128), (256, 256)])
    print('  app.ico            多尺寸')

    if '--sheet' in sys.argv:
        cols, cell = 6, 96
        rows = (len(ICONS) + cols - 1) // cols
        sheet = Image.new('RGBA', (cols * cell, rows * cell),
                          (0xF0, 0xF0, 0xF0, 255))
        for i, (name, fn) in enumerate(ICONS):
            im = render(fn, 64)
            x = (i % cols) * cell + (cell - 64) // 2
            y = (i // cols) * cell + (cell - 64) // 2
            sheet.alpha_composite(im, (x, y))
        sheet.save(os.path.join(here, '_sheet.png'))
        print('样张: _sheet.png')


if __name__ == '__main__':
    main()
