---
name: jl-dot-ui
description: 杰理(JL)单色点阵屏 UI 框架的界面开发，图层只走 OSD1/MONO。做界面布局（直接编辑 .uiproj 工程脚本）、给控件写应用层回调、生成并导出资源时用。涉及 UITools 工具链、设备端 ui_framework、ename.h 绑定、--gen/--pack 导出链、JL.sty/JL.res/JL.str 产物。不适用于彩屏（OSD16）。
---

# 杰理单色点阵屏 UI 开发

适用范围：**杰理(JL)的单色点阵屏 UI 框架**。硬约束是**单色点阵**——
图层的 `color_format` 只能是 `OSD1`（设备端 `DC_DATA_FORMAT_MONO`），
彩屏 `OSD16` 走的是另一条完全不同的绘制路径，这套不适用。

资源产物叫 `JL.sty` / `JL.res` / `JL.str`，ID 头是 `style_jl02.h`。

> **分辨率不限死。** 屏幕尺寸是**每个工程自己的**：编辑器里在 ui-config 的
> `Size=宽*高`，json 里就是页节点的 `rect`，设备端按面板的
> `info.width/height` 来。本仓库的样例工程是 128×64，文档里的坐标例子都按它写，
> 换个尺寸的屏这套照样用，只是数字不同。

这套东西分三层，做界面时三层都会碰到：

```
工程脚本 .uiproj  ──UITools──►  布局数据 + 资源  ──►  设备端 ui_framework 渲染
      ▲                              │
      └── 你在这里排版                └── ename.h ──► 应用代码挂回调、调控件 API
```

**AI 做界面不需要开编辑器** —— 直接编辑 `.uiproj`（就是 JSON），
用命令行验格式、出图看效果。编辑器只在需要人工微调时才开。

---

## 工作循环

```
改 .uiproj
   ↓
--json-roundtrip --out x   规范化（见铁律 7），再验就该 rc=0
   ↓
tools/check_project.py     语义体检：ename 重复/非法、零尺寸、
   ↓                       组合控件被改名、背景色盖图
--shot --no-chrome --zoom 400   出图（不放大只有指甲盖大）
   ↓
读图看排版 ──不对──► 回去改
   ↓ 对了
--gen                看有没有「认不出控件类型」的警告
   ↓
--gen --run-resbuilder   出资源（要更新固件时才跑，见铁律 6）
```

**每一步都有退出码或图可判，不要凭想象说"应该没问题"。**

> ⚠ **工具的输出是 GBK。** 在 Bash / UTF-8 终端里直接看 `--gen`、
> `--json-roundtrip` 的信息全是乱码，要 `2>&1 | iconv -f GBK -t UTF-8`。
> 退出码是准的，但**警告内容看不懂就等于没看**。

---

## 八条铁律

### 1. `caption` 决定控件类型，不是 `-type`

生成器查类型码是 **caption 优先**。slider 的四个零件 `-type` 全是 `ImageList`，
靠 `caption` 是 `right_pic`/`left_pic`/`slider_pic`/`slider_text` 才被认出来。
**不要"顺手整理" caption** —— 改了就静默退化成普通控件，不报错。

### 2. 图层的 `color_format` 必须是 `OSD1`

**这是点阵屏框架，`OSD16` 不能用。** 两者在设备端是两条完全不同的绘制路径：

| color_format | 设备端 | 画图行为 |
|---|---|---|
| `OSD1` = 4 | `DC_DATA_FORMAT_MONO` | `if (color) draw_point()` → **只写亮点，暗点不动背景（叠加）** |
| `OSD16` = 2 | 彩屏 16bpp | 无 alpha 时整行 `memcpy` → **覆盖，连 0 像素一起写** |

选了 OSD16 有两层后果：设备端跑的是彩屏路径；而且**编辑器预览是按点阵屏的
叠加画的，屏上却是覆盖 —— 预览和实机对不上**，这类问题极难查。

`tools/check_project.py` 会把非 OSD1 的图层报成错误。

### 3. 「背景填充」是三态，它决定叠加还是覆盖

每个控件的 `element_css.background_color` 有三种效果，**不是两种**：

| 值 | 效果 |
|---|---|
| 空串 `""` | 不填 → 透明 → 图**叠加**在底下的背景图上 |
| `#ff555aaa` | 整块点亮再画图 |
| **其它任何颜色** | 整块**擦灭**再画图 → **盖住**底下的背景图 |

固件是 `background_color != 0xffffff` 才 `fill_rect`，而 `argbTo565()`
只有空串才给 `0xFFFFFF` —— 所以**设了颜色就一定会 fill**，区别只在亮/灭。

两个后果：

- 控件库的彩色默认值（`#D2EE45` `#D9EE94` …）**不是"没效果"，等于"擦除"**。
  从控件库 deepcopy 建节点时要**想清楚要哪一种**，别无脑清成空串 ——
  清成空串就是叠加，复刻老设备的字段框会和底图糊在一起。
- 反过来，要「盖住」就得**主动设一个颜色**，光靠图片本身盖不住
  （MONO 画图只写亮点，暗点不动背景）。

详见 `reference/layout.md` 第 7 节，那里有对比图和固件代码。
`tools/check_project.py` 会数出带控件库默认色的控件当提示。

### 4. 绘制期回调里不能调 UI 更新 API

`ON_CHANGE_SHOW_PROBE / SHOW / SHOW_POST / FIRST_SHOW / SHOW_COMPLETED`
都是**在绘制过程中**发的。在里面调 `ui_pic_show_image_by_id()` /
`ui_text_set_*` / `ui_number_update_by_id()` 会**重入整套绘制递归**，
还会覆盖资源管理器里那份唯一的 `static union ui_control_info` ——
外层遍历读到垃圾，**整棵子树画不出来**。

症状极具误导性：一整页只剩一两个控件，看着完全像资源坏了。
（真实案例查了很久 `.sty`，逐字段全是对的。）

**正解是框架自带的 `ui_set_call(cb, 0)`** —— 把刷界面推迟到本轮事件分发结束：

```c
case ON_CHANGE_FIRST_SHOW:
    ui_set_call(refresh_cb, 0);    /* 不要在这里直接调 ui_xxx_set/show */
    break;
```

⚠ 照抄 `ui_action/` 的范式时注意：那几个参考文件里只有两处 `FIRST_SHOW`
带了这个 `@note`，其余是光秃秃的 `TODO: 刷xxx到界面上` —— 照着那种 TODO
直接写就会踩坑。详见 `reference/app.md`。

### 5. 应用层不要用 `REGISTER_UI_EVENT_HANDLER`

杰理原生 SDK（`apps/soundbox/ui/lcd/STYLE_02` 那套）用链接段收集注册事件，
**本框架不支持** —— `sec()` 是空宏，照抄会编译过、链接过、就是不响应。

本框架用显式表：`ui_action/<页面>_action.c` 定义 `ui_handlers_<页面>`，
再到 `config/ui_port_registry.c` 登记一行。详见 `reference/app.md`。

> STYLE_02 仍是很好的**业务逻辑和控件 API 用法**参考，只是注册骨架不能照抄。

### 6. `--gen --run-resbuilder` 会直接改固件树

默认会跑收尾脚本，覆盖 `JL.sty` / `JL.res` / `JL.str` 和 ID 头。
**试排版时加 `--no-script`**。真要更新固件时再去掉，跑完看 git diff。

⚠ **编辑器工具栏的「资源导出」按钮走的是同一条链，而且点了直接跑、不弹界面、
没有 `--no-script` 可加。** 所以「只是打开工具看一眼」的过程中点到它，
固件树就被这个工程的资源覆盖了。排查"谁改了 `style_jl02.h`"时先想到这个。

另外：编辑器保存工程会把文件名写回 `config/ini/project.ini`，
生成前先确认 `projectfilename` 是你要的那个界面。

### 7. 脚本生成的 json 必须先让工具规范化

工具的 json 格式和 `json.dump()` 不一样（空数组它写 `[\n    ]`）。
自己生成的 json 内容全对也过不了 `--json-roundtrip` —— 别去查那个"bug"。
固定加一步：

```
UITools.exe --json-roundtrip mine.uiproj --out mine_norm.uiproj
```

这一步会报不一致（正常），拿 `mine_norm.uiproj` 替换原文件，
再跑 `--json-roundtrip` **和 `--json-rebuild`**，两个都 rc=0 才算过关。

### 8. `--json-roundtrip` 只验格式，不验语义

caption 改错、ename 重复、rect 超出父节点，它统统照样通过。
**排版必须出图看，类型必须看 `--gen` 的警告。**

---

## 参考资料（按需读，别一次全看）

| 文件 | 什么时候读 |
|---|---|
| `reference/widgets.md` | **选控件**。每个控件什么时候用、怎么摆、代码怎么调；含"两个都能做时选哪个" |
| `reference/authoring.md` | **建工程 / 搭结构**。从零建一个 UI 工程；什么时候建页/图层/布局、弹层怎么做 |
| `reference/layout.md` | **写 json**。树结构、坐标系、类型码规则、不变量清单 |
| `reference/widgets_props.md` | 查某个控件的属性和取值范围（**自动生成，勿手改**） |
| `reference/app.md` | **写代码**。事件表注册、控件生命周期、各控件 API |
| `reference/export.md` | 导出资源、出图验收、和现有产物比对 |

排一个新界面的顺序：`widgets.md` 选控件 → `authoring.md` 搭结构 →
`layout.md` 落成 json → `app.md` 写回调 → `export.md` 导出。

工具自己的完整命令行表在 `tools/LCD_UI工程/UIProject/tool/README.md`。

## 随 skill 带的三个脚本

```
tools/locate_frames.py   坐标怎么来：底图 vs 运行时截图，算出帧集名 + 精确 rect
tools/check_project.py   语义体检，有错返回 1
tools/gen_widgets_ref.py 重生成属性表
```

**复刻既有设备的界面时先跑 `locate_frames.py`** —— 它填的是工作循环最前面
那个洞：坐标从哪来。底图和运行时截图逐像素做差得到"运行时画上去的字段"，
每块再去所有帧集里滑窗找逐像素相等的位置，一次同时得到**帧集名和精确 rect**，
不用一个个估。它的四条局限写在脚本抬头，**用之前看一眼**
（尤其"矢量绘制的东西匹配不上是正常结果，别硬塞"）。

`check_project.py` 拿三个已知正确的工程标定过（都是 0 错 0 疑），
所以**它一旦报错就是真错**，别当噪音跳过。它查的都是
`--json-roundtrip` 放行、要到写代码或上板才发现的东西：
ename 空/重复/非法字符、`property` 里缺 id 项、rect 尺寸为 0、
子数组键名写错、组合控件零件被整体改名；另外会数出
「带控件库默认背景色的控件」和「还在用占位名的控件」当提示。

---

## 可用控件

| caption | -type | 码 | 应用侧头文件 |
|---|---|---|---|
| 布局 | NewLayout | 3 | —— |
| 图层 | NewLayer | 4 | ——（结构节点，可注册回调） |
| 垂直列表 / 水平列表 / 表格控件 | VerticalList / HorizontalList / NewGrid | 5 | `ui/ui_grid.h` |
| 图片 | ImageList | 8 | `ui/ui_pic.h` |
| 电池电量 | Battery | 9 | `ui/ui_battery.h` |
| 时间 | Time | 10 | `ui/ui_time.h` |
| 文字 | Text | 12 | `ui/ui_text.h` |
| 数字 | number | 15 | `ui/ui_number.h` |
| slider / vslider | （组合控件） | 28 / 33 | `ui/ui_slider.h` |

`typecodes.ini` 里还有 `Button` `Camera` `animation` `player` `progressbar`
`watch` 等，**控件库里没有、设备端也没实现，不要用**。

---

## 属性表怎么保持不过时

`reference/widgets_props.md` 由脚本从工具实际读的那份控件库导出。
控件库变了就重跑：

```
python .claude/skills/jl-dot-ui/tools/gen_widgets_ref.py > .claude/skills/jl-dot-ui/reference/widgets_props.md
```
