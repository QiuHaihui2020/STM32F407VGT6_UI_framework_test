# -*- coding: utf-8 -*-
"""工程脚本语义体检 —— 查 --json-roundtrip 查不到的那些。

`--json-roundtrip` 只证明「读得进、写得回」，语义错它一律放行：
ename 重复、slider 零件被改名、子数组键名写错，它全都通过。
这些错要到写应用代码或上板才发现，返工代价大，所以单独体检一遍。

【标定原则】规则全部拿参考工程 SmallColor_oled.uiproj 校准过 ——
那是出厂正确的工程，**它身上不该报错**。一个在正确工程上刷屏的检查器
等于没有，会被直接忽略，所以这里宁可漏报也不误报：
  * 背景色只出一行汇总，不逐个报（参考工程自己就大量用彩色背景）
  * rect 只查负坐标/零尺寸（列表条目超出可视区正是滚动的实现方式，不是错）
  * caption 只查 slider/vslider 零件（别处改名会回退到 -type，结果照样对）
  * 占位名是 WARN 不是 ERROR（slider 零件这类代码不单独碰的控件，留占位名没问题）

用法：
    python check_project.py <工程.uiproj> [--tool-src <tool_src 目录>]

退出码：0 = 没有 ERROR；1 = 有 ERROR。
"""
import argparse
import collections
import configparser
import io
import json
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

# 挂子节点的键名，每层不同。写错了工具读不到那棵子树，且不报错。
CHILD_KEYS = ("pages", "layer", "layout", "listwidget")

# 编辑器自动起的名字。合法，但应用代码拿 #define BASEFORM_12 是找不到东西的。
PLACEHOLDER = re.compile(
    r"^(BaseForm|NewFrame|NewLayout|NewLayer|NewList|NewGrid)(_\d+)?$")

ENAME_OK = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# 组合控件的零件：类型码全靠 caption，改了就退化成普通图片/文字，且不报错。
COMPOSITE_PARTS = {
    "slider":  {"right_pic", "left_pic", "slider_pic", "slider_text"},
    "vslider": {"vslider_right_pic", "vslider_left_pic",
                "vslider_pic", "vslider_text"},
}

# 控件库里每个控件都带一个彩色 background_color 默认值。从库里 deepcopy
# 建节点时不清掉，控件就是一坨实心色块，把内容整个盖住 —— 而且全程不报错。
# 判据：StyBuilder::argbTo565() 空串 -> 0xFFFFFF；设备端 ui_core_show_rect()
# 只在 background_color != 0xffffff 时 fill_rect。所以空串 = 透明。
LIB_DEFAULT_BG = {
    "#368fee", "#d2ee45", "#8deedb", "#d9ee94",
    "#eeb797", "#eeb7d2", "#eed9c1", "#38ee44",
}
TRANSPARENT = {"", "#ffffff", "#ffffffff"}


class Report(object):
    def __init__(self):
        self.errors = []
        self.warns = []
        self.notes = []

    def error(self, where, msg):
        self.errors.append((where, msg))

    def warn(self, where, msg):
        self.warns.append((where, msg))


def load_typecodes(tool_src):
    if not tool_src:
        return {}
    ini = os.path.join(tool_src, "resources", "assets", "typecodes.ini")
    if not os.path.isfile(ini):
        return {}
    cp = configparser.ConfigParser()
    cp.optionxform = str          # number 和 Number 是两回事
    cp.read(ini, encoding="utf-8-sig")
    return dict(cp.items("Control")) if cp.has_section("Control") else {}


def children(node):
    for k, v in node.items():
        if isinstance(v, list) and v and isinstance(v[0], dict) and "-class" in v[0]:
            for c in v:
                yield k, c


def prop(node, name):
    for p in node.get("property", []):
        if p.get("-name") == name:
            return p
    return None


def css_item(node, name):
    p = prop(node, "element_css")
    if not p:
        return None
    for row in p.get("struct", []):
        for it in row:
            if it.get("-name") == name:
                return it
    return None


def label(node, path):
    return "%s [%s/%s]" % (path, node.get("-class"), node.get("-type"))


class Checker(object):
    def __init__(self, codes):
        self.codes = codes
        self.rep = Report()
        self.seen = {}
        self.bg_default = collections.Counter()
        self.placeholders = []
        self.total = 0

    def visit(self, node, path="root", parent_cap=None):
        self.total += 1
        where = label(node, path)
        typ = node.get("-type")
        cap = node.get("caption")

        self._check_ename(node, where, typ)
        self._check_color_format(node, where, typ)
        self._check_composite(node, where, cap, parent_cap)
        self._check_bg(node, typ)
        self._check_rect(node, where)

        for k, c in children(node):
            if k not in CHILD_KEYS:
                self.rep.error(where, "子数组键名 %r 不认识（应为 %s 之一）"
                               % (k, " / ".join(CHILD_KEYS)))
            self.visit(c, "%s/%s" % (path, c.get("-type")), cap)

    def _check_ename(self, node, where, typ):
        idp = prop(node, "id")
        if idp is None:
            # 页节点没有 id 项是正常的
            if typ != "page" and node.get("property") is not None:
                self.rep.error(where, "property 里没有 id 项 —— "
                                      "应用代码永远够不着这个控件")
            return
        ename = (idp.get("ename") or "").strip()
        if not ename:
            self.rep.error(where, "ename 是空的")
        elif not ENAME_OK.match(ename):
            self.rep.error(where, "ename=%s 含非法字符（宏名只能字母数字下划线）"
                           % ename)
        elif ename in self.seen:
            self.rep.error(where, "ename=%s 和 %s 重复 —— id 是哈希，重名就撞车"
                           % (ename, self.seen[ename]))
        else:
            self.seen[ename] = where
            if PLACEHOLDER.match(ename):
                self.placeholders.append((where, ename))

    def _check_composite(self, node, where, cap, parent_cap):
        """组合控件：零件的类型码全靠 caption，改名就退化成普通控件且不报错。

        只查「一个零件都认不出来」这种整体改名的情况。零件之外**允许**再放
        普通控件（参考工程的 slider 里就多摆了一张图），所以不能见到陌生
        caption 就报。"""
        parts = COMPOSITE_PARTS.get(cap)
        if not parts:
            return
        got = {c.get("caption") for _, c in children(node)} & parts
        if not got:
            self.rep.error(where,
                           "这是 %s，但子节点里一个零件都认不出来（应有 %s）—— "
                           "零件的类型码由 caption 定，改了名它就只是普通图片"
                           % (cap, " / ".join(sorted(parts))))

    def _check_color_format(self, node, where, typ):
        """图层的 color_format 必须是 OSD1。

        这是**点阵屏**框架：OSD1(=4) 在设备端映射成 DC_DATA_FORMAT_MONO，
        画图走 `if (color) draw_point()` —— 只写亮点，暗点不动背景（叠加）。
        OSD16(=2) 走的是彩屏 16bpp 路径，无 alpha 时整行 memcpy（覆盖）。

        选了 OSD16 有两个后果：设备端行为和点阵屏对不上；而且编辑器预览是按
        点阵屏（叠加）画的，屏上却是覆盖，**预览和实机不一致**，很难查。
        """
        if typ != "NewLayer":
            return
        p = prop(node, "color_format")
        if not p:
            return
        v = p.get("default")
        if v and v != "OSD1":
            self.rep.error(where,
                           "图层 color_format=%s —— 点阵屏工程必须是 OSD1。"
                           "OSD16 是彩屏 16bpp 路径（整行 memcpy 覆盖），"
                           "而预览按点阵屏的叠加画，两边对不上" % v)

    def _check_bg(self, node, typ):
        it = css_item(node, "background_color")
        if not it:
            return
        v = (it.get("background-color") or "").strip().lower()
        if v in LIB_DEFAULT_BG:
            self.bg_default[typ] += 1

    def _check_rect(self, node, where):
        """只查铁定错的：零/负尺寸。

        「超出父节点」不算错 —— 列表条目正是靠超出可视区实现滚动的。
        负坐标也不算 —— 参考工程里有 y=-11 的图，是有意的裁切。"""
        it = css_item(node, "rect")
        if not it:
            return
        r = it.get("rect") or {}
        if r.get("width", 0) <= 0 or r.get("height", 0) <= 0:
            self.rep.error(where, "rect 宽或高是 0：%s —— 这个控件画不出来" % r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--tool-src", default="")
    a = ap.parse_args()

    with io.open(a.project, encoding="utf-8") as f:
        doc = json.load(f)

    tool_src = a.tool_src
    if not tool_src:
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.abspath(os.path.join(here, "..", "..", "..", ".."))
        guess = os.path.join(root, "tools", "LCD_UI工程", "UIProject", "tool_src")
        if os.path.isdir(guess):
            tool_src = guess

    ck = Checker(load_typecodes(tool_src))
    ck.visit(doc)
    rep = ck.rep

    for where, msg in rep.errors:
        print("ERROR  %s\n       %s" % (where, msg))
    for where, msg in rep.warns:
        print("WARN   %s\n       %s" % (where, msg))

    # ---- 汇总项：逐个报会刷屏，一行说清就够 --------------------------------
    if ck.placeholders:
        print("\n提示  %d 个控件用的是编辑器占位名（BaseForm_12 这种）："
              % len(ck.placeholders))
        for where, e in ck.placeholders[:6]:
            print("        %-10s %s" % (e, where))
        if len(ck.placeholders) > 6:
            print("        ...（还有 %d 个）" % (len(ck.placeholders) - 6))
        print("      应用代码要碰的控件必须起业务名；"
              "组合控件零件这类代码不单独访问的，留占位名没问题。")

    if ck.bg_default:
        n = sum(ck.bg_default.values())
        print("\n提示  %d 个控件带着**控件库的默认背景色**：" % n)
        for t, c in ck.bg_default.most_common():
            print("        %-16s %d 个" % (t, c))
        print("      在点阵屏上这不等于「没效果」，等于「整块擦灭再画图」——")
        print("      也就是这个控件会**盖住**底下布局的背景图。")
        print("      要叠加（让底图透出来）就清成空串 \"\"；")
        print("      要覆盖就留着（复刻老设备的字段框正是要这个）。")
        print("      从控件库 deepcopy 建节点时最容易在这儿想当然，确认一下要哪种。")

    print("\n共 %d 个节点：%d 个错误，%d 个可疑"
          % (ck.total, len(rep.errors), len(rep.warns)))
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
