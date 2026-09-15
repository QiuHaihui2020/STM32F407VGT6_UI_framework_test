# -*- coding: utf-8 -*-
"""照着老设备复刻界面时，把「每块动态内容用的是哪个帧集、精确坐标是多少」算出来。

【解决什么问题】
复刻既有设备的界面时，最耗时的不是摆控件，是**坐标对不准** —— 差 1px 就看得出来，
而肉眼估坐标既慢又不可靠。如果手上有底图和运行时截图，这一步可以完全自动：

    底图 ≠ 运行时截图  →  差异块 = 运行时画上去的字段
    每块拿去和资源里所有帧集逐像素比  →  同时得到帧集名 + 精确 rect

输出直接能填进工程 json 的 `element_css.rect` 和 `normal_image`。

【用法】
    python locate_frames.py --base screens/x.bmp --preview screen_previews/x.bmp \
                            --frames <资源目录> [--frames <另一个>] [--top 3]

    # 一次跑多屏（--base/--preview 给目录，按同名配对）
    python locate_frames.py --base screens --preview screen_previews \
                            --frames <资源根目录>

帧集的发现方式：把 `--frames` 下所有 `.bmp` 按**文件名去掉 `_fNNN` 后的词干**归组。
`--frames` 可以给多次，**只指向真正放帧的目录**（如 `common`、`controls`），
别把整屏底图目录（`screens`、`screen_previews`）也算进来 —— 那会让整屏底图
当成一个"帧集"，每块都匹配到它。（同尺寸的帧另有一道自动跳过，但别依赖它。）

实测（DCX2496 资源，124 组帧集）：

    块 x=76  y=9  17x5  -> scrn_001_win_000   rect=(75,8,33,7)   共 4 帧
                         ? scrn_015_knob_000  rect=(75,8,33,7)   共 4 帧
    块 x=78  y=21 46x9  -> scrn_001_knob_004  rect=(78,18,46,19) 共 4 帧

注意第一块给了两个候选 —— 两个帧集内容相同，本屏私有的排前面，但**语义上
选哪个还得人看**（见局限 2）。

【前提】
  * 有底图，也有运行时截图（或参考机的抓图），两者**同尺寸、1:1 原始像素**
  * 资源里的帧集就是设备实际用的那批

【已知局限 —— 看完再用，不然会被误导】

1. **只认逐像素相等，没有容差。** 截图必须是 1:1 原始像素的 BMP/PNG，
   缩放过的一律不行。拿 `--shot --zoom 400` 的图缩回 128×64 来比是不可信的
   （NEAREST 采样的边界误差就足以让 array_equal 全线失败）。
   要用工具截图做比对，得先按画布精确裁出来再二值化，**不要 resize**。

2. **一个块可能命中多个帧集。** 内容相同的帧（比如同一张空白 f0）会一起命中。
   本脚本按「本屏私有 > 通用图集 > 其它屏」+ 面积大优先排序，并用 `--top`
   列出候选 —— 但**排第一的不一定语义上对**。真实例子：`winbmp_015`(99 帧)
   和 `scrn_006_win_000`(10 帧) 都能盖住"对比度"字段，最后是按
   「帧数正好等于该字段的档位数」选了后者 —— 这个判断脚本做不了，得人看。

3. **全空的帧会到处命中**，已自动跳过（没有一个墨点的帧不参与匹配）。
   若仍看到一长串连续坐标的候选（`(52,7) (53,7) (54,7)...`），那就是撞上
   近似空白的帧了，丢弃该候选。

4. **矢量绘制的东西永远匹配不上，这是正确结果不是失败。** 曲线、连接线、
   运行时排版的数值串都属于这类。脚本报「未匹配」就照实留空，
   **不要硬塞一个最接近的帧集** —— 那会让后面的人以为坐标是对过的。
"""
import argparse
import glob
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

try:
    import numpy as np
    from PIL import Image
    from scipy import ndimage
except ImportError as e:                                     # noqa: BLE001
    sys.exit("缺依赖：%s（需要 numpy / scipy / Pillow）" % e)


def load_bw(path):
    """转成布尔点阵：True = 有墨。单色屏资源按 128 阈值二值化即可。"""
    return np.array(Image.open(path).convert("L")) < 128


class FrameSets(object):
    """帧集库。帧图按需加载并缓存 —— 一屏要比几万次，重复解码扛不住。"""

    def __init__(self, roots):
        self.sets = self._discover(roots)
        self._cache = {}
        self._blank = {}

    @staticmethod
    def _discover(roots):
        """一条规则收全：按**文件名去掉 _fNNN 后的词干**归组。

        资源命名是统一的 `<词干>_f<帧号>.bmp`，所以不管帧是摊在一个目录里
        （controls/scrn_001_knob_002_f0.bmp）还是每套一个子目录
        （common/winbmp_000/winbmp_000_f000.bmp），归出来都对。
        """
        groups = {}
        for root in roots:
            for f in glob.glob(os.path.join(root, "**", "*.bmp"), recursive=True):
                stem = re.sub(r"_f\d+\.bmp$|\.bmp$", "", os.path.basename(f))
                groups.setdefault(stem, []).append(f)

        def frame_no(p):
            m = re.search(r"_f(\d+)\.", os.path.basename(p))
            return int(m.group(1)) if m else 0

        return [(k, sorted(groups[k], key=frame_no)) for k in sorted(groups)]

    def img(self, path):
        if path not in self._cache:
            a = load_bw(path)
            self._cache[path] = a
            self._blank[path] = not a.any()   # 全空帧：滑窗时满屏都相等
        return self._cache[path]

    def is_blank(self, path):
        self.img(path)
        return self._blank[path]


def diff_blocks(base, preview):
    """差异块。

    ⚠ 包围盒必须用**原始 diff** 重算，不能用膨胀后的 —— 膨胀只是为了把断开的
    笔画连成一块，直接拿它的包围盒会横向虚胖 2px，把正好等宽的帧当成"太窄"
    过滤掉（实测漏掉过 13px 宽的 ON/OFF 帧集）。"""
    diff = base != preview
    lbl, _ = ndimage.label(ndimage.binary_dilation(diff, np.ones((3, 5))))
    out = []
    for sl in ndimage.find_objects(lbl):
        ys, xs = np.where(diff[sl])
        if not len(xs):
            continue
        out.append((sl[1].start + xs.min(), sl[0].start + ys.min(),
                    xs.max() - xs.min() + 1, ys.max() - ys.min() + 1))
    return out


def match_block(preview, block, lib, own_prefix, top):
    """在所有帧集上滑窗，找能逐像素盖住这块的帧。

    滑窗范围限定在「帧必须完整盖住块」：x ∈ [bx+bw-gw, bx]，y 同理。
    这一条把全图搜索压到每块几十次比较。"""
    bx, by, bw, bh = block
    H, W = preview.shape
    hits = []
    for name, files in lib.sets:
        own = own_prefix and name.startswith(own_prefix)
        for f in files:
            if lib.is_blank(f):
                continue
            g = lib.img(f)
            gh, gw = g.shape
            if gw < bw or gh < bh:
                continue
            if (gh, gw) == (H, W):
                continue          # 和整屏同尺寸的不是控件帧，是底图，跳过
            for y in range(max(0, by + bh - gh), min(by + 1, H - gh + 1)):
                for x in range(max(0, bx + bw - gw), min(bx + 1, W - gw + 1)):
                    if np.array_equal(preview[y:y + gh, x:x + gw], g):
                        hits.append(((1 if own else 0, gw * gh),
                                     name, x, y, gw, gh, len(files)))
                        break
                else:
                    continue
                break
    hits.sort(key=lambda h: h[0], reverse=True)
    # 同一帧集只保留最好的那条
    seen, uniq = set(), []
    for h in hits:
        if h[1] in seen:
            continue
        seen.add(h[1])
        uniq.append(h)
    return uniq[:top]


def run_one(base_path, prev_path, lib, own_prefix, top):
    base = load_bw(base_path)
    prev = load_bw(prev_path)
    if base.shape != prev.shape:
        print("!! 底图和截图尺寸不同（%s vs %s），跳过" % (base.shape, prev.shape))
        return
    name = os.path.splitext(os.path.basename(base_path))[0]
    print("=" * 60)
    print(name)
    blocks = diff_blocks(base, prev)
    placed = []
    for b in sorted(blocks, key=lambda t: (t[1], t[0])):
        bx, by, bw, bh = b
        if any(bx >= px and by >= py and bx + bw <= px + pw and by + bh <= py + ph
               for px, py, pw, ph in placed):
            continue                      # 已被更大的命中块包住
        cands = match_block(prev, b, lib, own_prefix, top)
        tag = "块 x=%-3d y=%-2d %2dx%-2d" % (bx, by, bw, bh)
        if not cands:
            print("  %s -> 未匹配（矢量绘制，照实留空）" % tag)
            continue
        for i, (_, nm, x, y, gw, gh, n) in enumerate(cands):
            mark = "->" if i == 0 else "  ?"
            print("  %s %s %-24s rect=(%d,%d,%d,%d)  共 %d 帧"
                  % (tag if i == 0 else " " * len(tag), mark, nm, x, y, gw, gh, n))
        _, nm, x, y, gw, gh, _n = cands[0]
        placed.append((x, y, gw, gh))
    if len(blocks):
        print("  —— %d 块" % len(blocks))


def main():
    ap = argparse.ArgumentParser(
        description="底图 vs 运行时截图逐像素定位帧集和 rect")
    ap.add_argument("--base", required=True, help="底图文件或目录")
    ap.add_argument("--preview", required=True, help="运行时截图文件或目录（同名配对）")
    ap.add_argument("--frames", required=True, action="append",
                    help="帧集资源目录，可给多次。"
                         "只指向真正放帧的目录（如 common、controls），"
                         "别把整屏底图目录也算进来")
    ap.add_argument("--own-prefix", default="",
                    help="本屏私有帧集的名字前缀，排序时优先（如 scrn_031）")
    ap.add_argument("--top", type=int, default=1,
                    help="每块列出几个候选（默认 1；拿不准时给 3 自己挑）")
    a = ap.parse_args()

    lib = FrameSets(a.frames)
    print("帧集 %d 组\n" % len(lib.sets))

    if os.path.isdir(a.base):
        pairs = []
        for f in sorted(glob.glob(os.path.join(a.base, "*.bmp"))):
            p = os.path.join(a.preview, os.path.basename(f))
            if os.path.isfile(p):
                pairs.append((f, p))
    else:
        pairs = [(a.base, a.preview)]

    if not pairs:
        sys.exit("没有配对上任何底图/截图")
    for b, p in pairs:
        own = a.own_prefix or os.path.basename(b)[:9]
        run_one(b, p, lib, own, a.top)


if __name__ == "__main__":
    main()
