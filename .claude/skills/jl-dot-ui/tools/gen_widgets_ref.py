# -*- coding: utf-8 -*-
"""从控件库生成 reference/widgets.md 的「编辑器属性」部分。

【为什么要生成而不是手写】
属性表手写的话，控件库一改文档就过时，而 AI 拿着过时的表会写出「json 语法
正确、工具不认」的工程 —— 比没有文档更糟。所以属性一律从工具实际读的那份
数据来：工具读哪份，文档就从哪份出。

数据源（工具侧，勿改这些文件的格式）：
    resources/assets/widgets.json    主控件库
    resources/assets/widgets.d/*.json 追加控件（slider / vslider）
    resources/assets/typecodes.ini   -type -> 数字类型码

用法：
    python gen_widgets_ref.py [tool_src 目录] > widgets_props.md
不给参数时按本文件位置往上找仓库根，再拼默认路径。
"""
import configparser
import io
import json
import os
import sys

sys.stdout.reconfigure(encoding="utf-8")

DEFAULT_TOOL_SRC = os.path.join(
    "tools", "LCD_UI工程", "UIProject", "tool_src")


def repo_root():
    """本文件在 <root>/.claude/skills/jl-dot-ui/tools/ 下，往上四层。"""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "..", "..", ".."))


def load_assets(tool_src):
    assets = os.path.join(tool_src, "resources", "assets")
    with io.open(os.path.join(assets, "widgets.json"), encoding="utf-8") as f:
        comps = json.load(f)["compoents"]

    extra_dir = os.path.join(assets, "widgets.d")
    if os.path.isdir(extra_dir):
        for name in sorted(os.listdir(extra_dir)):
            if not name.endswith(".json"):
                continue
            with io.open(os.path.join(extra_dir, name), encoding="utf-8") as f:
                d = json.load(f)
            for c in d.get("compoents", [d]):
                c["_from"] = "widgets.d/" + name
                comps.append(c)

    codes = {}
    cp = configparser.ConfigParser()
    # 键区分大小写：number 和 Number 是两回事
    cp.optionxform = str
    cp.read(os.path.join(assets, "typecodes.ini"), encoding="utf-8-sig")
    if cp.has_section("Control"):
        codes = dict(cp.items("Control"))
    return comps, codes


def type_code(node, codes):
    """类型码的查表规则必须和 StyBuilder.cpp:446 一致：**caption 优先，再退回 -type**。

    扩展控件（slider/vslider 及其零件）的 -type 都是 NewLayout/ImageList/Text，
    真正决定类型码的是 caption。这里跟错了，导出的表就会把 slider 标成布局。
    """
    cap = node.get("caption")
    if cap in codes:
        return codes[cap], "caption"
    t = node.get("-type")
    if t in codes:
        return codes[t], "-type"
    return "?", ""


def child_nodes(node):
    """节点下面挂子节点的键：layer / layout / listwidget。"""
    out = []
    for k, v in node.items():
        if isinstance(v, list) and v and isinstance(v[0], dict) and "-class" in v[0]:
            out += [(k, c) for c in v]
    return out


def enum_str(item):
    """enum 是 [{名字: 值}, ...] 这种一项一字典的形状。"""
    out = []
    for e in item.get("enum", []):
        for k, v in e.items():
            out.append("`%s`=%s" % (k, v))
    return " / ".join(out)


def describe(item):
    """一个属性项 -> 一行说明。13 种 -type，逐个照顾到。"""
    t = item.get("-type")
    if t in ("enum", "color-format"):
        s = enum_str(item)
        d = item.get("default")
        return "%s（默认 `%s`）" % (s, d) if d is not None else s
    if t in ("int8", "int16"):
        return "整数 %s..%s，默认 `%s`" % (
            item.get("min"), item.get("max"), item.get("default"))
    if t in ("piclist", "arrlist", "text-pic"):
        n = item.get("maxlength") or 0
        lim = "不限" if not n else str(n)
        return "列表（上限 %s），默认 `%s`" % (lim, item.get("default", ""))
    if t == "text-str":
        return "字符串（上限 %s），默认 `%s`" % (
            item.get("maxlength"), item.get("default", ""))
    if t == "id":
        return "唯一 ID。`ename` 就是生成到 ename.h 里的宏名"
    if t == "color":
        return "颜色"
    if t == "background-color":
        return "背景色"
    if t == "action":
        return "事件动作表（见 layout.md「action」一节）"
    if t == "struct":
        return "复合项，展开见下"
    return t


def emit_struct(item, indent):
    """element_css 这种 struct 里还套一层二维数组。"""
    for row in item.get("struct", []):
        for sub in row:
            name = sub.get("-name")
            st = sub.get("-type")
            if st == "rect":
                desc = "坐标 `{x, y, width, height}`，**相对父节点**，单位像素"
            elif st == "border":
                desc = "边框 `{top, bottom, left, right}`"
            elif st == "background-image":
                desc = "背景图路径"
            else:
                desc = describe(sub)
            print("%s- `%s` — %s（%s）" % (
                indent, name, sub.get("caption", ""), desc))


def main():
    tool_src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        repo_root(), DEFAULT_TOOL_SRC)
    if not os.path.isdir(tool_src):
        sys.exit("找不到 tool_src：%s" % tool_src)

    comps, codes = load_assets(tool_src)

    print("<!-- 本文件由 .claude/skills/jl-dot-ui/tools/gen_widgets_ref.py 生成，勿手改 -->")
    print("<!-- 控件库一变就重跑：python gen_widgets_ref.py > ../reference/widgets_props.md -->")
    print()
    print("# 控件属性表（自动导出）")
    print()
    print("共 %d 个控件。「类型码」是 `-type` 在 typecodes.ini 里的值，"
          "也是设备端 `control.h` 里 `CTRL_TYPE_*` 的值。" % len(comps))
    print()

    for c in comps:
        t = c.get("-type")
        cap = c.get("caption", "")
        code, via = type_code(c, codes)
        src = c.get("_from", "widgets.json")
        kids = child_nodes(c)
        # 组合控件（slider/vslider）在库里就是一棵子树，标题用 caption 才认得出
        title = cap if via == "caption" else t
        print("## %s%s" % (title, "" if title == cap else " —— " + cap))
        print()
        print("| | |")
        print("|---|---|")
        print("| `-class` | `%s` |" % c.get("-class"))
        print("| `-type` | `%s` |" % t)
        print("| `caption` | `%s` |" % cap)
        print("| 类型码 | %s（由 %s 定）|" % (code, via or "查不到"))
        print("| 来源 | %s |" % src)
        if c.get("tip"):
            print("| 说明 | %s |" % str(c["tip"]).replace("\n", " "))
        print()
        if kids:
            print("**组合控件**：库里就是一棵子树，插入时整棵一起进工程。"
                  "零件的类型码同样由 `caption` 决定，"
                  "**改了 caption 就不再是这个零件**。")
            print()
            for k, sub in kids:
                sc, sv = type_code(sub, codes)
                print("- `%s[]` → `%s`（`-type`=%s，类型码 %s 由 %s 定）" % (
                    k, sub.get("caption", ""), sub.get("-type"), sc, sv or "查不到"))
            print()
        for p in c.get("property", []):
            print("- `%s` — %s（%s）" % (
                p.get("-name"), p.get("caption", ""), describe(p)))
            if p.get("-type") == "struct":
                emit_struct(p, "  ")
        print()


if __name__ == "__main__":
    main()
