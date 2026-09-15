# 建工程 · 组织界面

---

# 一、从零建一个 UI 工程

## 工程目录长什么样

一个 UI 工程目录是**自足**的 —— 工程本身 + 它用的那套工具，删掉整个目录就干净了：

```
<工程名>\
    注册文件关联.bat  取消文件关联.bat  新建工程.bat
    project\
        <工程名>.uiproj                 工程脚本（就是 json）
        config\ini\project.ini          工程配置
        config\pic_lcd\                 图片放这儿
        Application Data\ui-config      编辑器设置
        copy_file.bat                   把产物拷进固件工程
        *.xls                           多国语言表（自己放）
    tool\                               UITools.exe + template
```

控件库和类型码表**编在 exe 里**，所以新工程里看不到它们，不用管。

## 有界面的做法

```
新建工程.bat <工程名>
```

在旁边拉出一份骨架 + 一套工具，然后照它打印的五步走
（放图片 → 放 xls → 开工具建页面 → 资源导出 → 改 copy_file.bat 的路径）。

## AI 的做法（不开界面）

**别手写空工程的 json** —— `property[]` 少一项工具就不认，那些默认值和顺序
都散在控件库里。两个种子来源：

| 来源 | 怎么用 | 适合 |
|---|---|---|
| **控件库**（推荐） | `deepcopy` `tool_src/resources/assets/widgets.json` 的 `compoents[]` 条目 | 干净，property 项数/顺序/默认值都是全的，只改要改的 |
| `--make-sample seed.json` | 造一份 3 页、每种控件各一个的完整工程（约 790 KB），从里面抠节点 | 想看"真实工程里长什么样"时参考 |

deepcopy 控件库之后**必须改这三样**：

1. `property` 里 `id.ename` —— 起业务名，全工程唯一（**这是应用代码唯一的抓手**）
2. `element_css.rect` —— 坐标相对父节点
3. `element_css.background_color` —— **清成空串**，否则是实心色块（见 SKILL.md 铁律 2）

### 用表驱动的生成脚本，别一个个手写

实战验证有效的写法：每个控件一行，`ename` 是**必填的位置参数**，
漏写直接 TypeError —— 比"记得填 ename"这种提醒硬得多。

```python
# (控件名, ename, rect, 图集, 说明)
WIDGETS = [
    ("电池",   "BT_BAT",    (89, 0, 39, 16), BATT_FRAMES, "右上角电量"),
    ("状态图", "BT_STA_PIC", (0, 0, 16, 16), STATUS_FRAMES, "连接/断开"),
]
def make(kind, ename, rect, frames, note):   # ename 漏了就报错，不会静默
    node = copy.deepcopy(LIB[kind])
    ...
```

### 步骤

1. `robocopy tool\template <目标> /E` 拉骨架（或手工照上面的结构建）
2. 表驱动脚本从控件库造节点，拼出工程
3. **用工具规范化 json**（见下，这一步不能省）
4. `python .claude/skills/jl-dot-ui/tools/check_project.py <工程>` 体检
5. 填 `config\ini\project.ini` 的 `projectfilename=<工程名>.uiproj`
6. 图片放 `config\pic_lcd\`，多国语言表放工程目录
7. 改 `copy_file.bat` 里的两个路径，指向你的固件树
8. `--gen --no-script` 跑一遍，看有没有警告（输出是 GBK，要 `| iconv -f GBK -t UTF-8`）

> 写生成脚本时**用 Write 工具落盘再跑**，不要用 bash heredoc ——
> heredoc 会吃掉反斜杠、被脚本里的引号绊住（报 `unexpected EOF`），
> 而且失败得很隐蔽（正则不匹配、路径变成乱码，却不报错）。

## ⚠ json 必须由工具规范化

工具写 json 有自己的固定格式，和 Python `json.dump()` 的输出**不一样**
（比如空数组：工具写 `[\n    ]`，Python 写 `[]`）。
自己生成的 json 内容就算完全正确，`--json-roundtrip` 也会失败。

所以生成 json 之后**固定加一步**：

```
UITools.exe --json-roundtrip mine.uiproj --out mine_norm.uiproj
```

这一步会报"不一致"（意料之中），但 `mine_norm.uiproj` 就是工具规范化后的版本。
**以它为准**，替换掉原文件。之后再跑：

```
UITools.exe --json-roundtrip mine_norm.uiproj    # rc=0
UITools.exe --json-rebuild   mine_norm.uiproj    # rc=0
```

两个都 0 才算格式过关。

## 最小可用工程

一页 / 一图层 / 一布局 / 一个控件，实测能加载、能渲染、roundtrip 和 rebuild 都过：

```
project
└─ pages[0]        ScenesScreen / page      caption=页面_0
   └─ layer[0]     NewLayer / NewLayer      caption=图层
      └─ layout[0] NewLayout / NewLayout    caption=布局
         └─ layout[0]  NewFrame / Text      caption=文字
```

根节点：

```json
{
  "-name": "MiniDemo",
  "-type": "project",
  "activePage": 0,
  "lang_excel": "",
  "language": [],
  "pages": [ ... ]
}
```

> 文字控件不挂 `str` 列表、`lang_excel` 又是空的话，画面上什么都不显示 ——
> 这是对的，不是坏了。文字内容在多国语言表里。

## 已知的两处旧路径

新建出来的工程会带上这两处重组前的路径，暂时不影响使用（工具有兜底），
但看到了不要当成对的抄：

- `template\project\Application Data\ui-config` 里的
  `TemplateJson=../../../UIToolkit/control/control.json`、
  `CustomTemplateDir=../../../UIToolkit/control/ex`
- `--make-sample` 产物里的 `lang_excel=../../../UITools/多国语言_128_64.xls`

新工程的 `lang_excel` 应指向自己 `project\` 下的那份 xls。

---

# 二、界面怎么组织：什么时候建页 / 图层 / 布局

下面的规律不是约定俗成，是把样本工程（`SmallColor_oled.uiproj`，11 页 499 个控件）
逐页统计出来的。新建界面照这个来，和既有工程、应用代码的结构才对得上。

---

## 一句话决策

| 你要做的 | 建什么 |
|---|---|
| 一个新的**工作模式**（蓝牙、FM、时钟、设置…），和别的模式互斥 | **新建一页** |
| 同一功能键下的**翻页**（标题栏写着 1/5、2/5 那种） | **不要新建页**！在本页图层下新建整屏布局，靠 `invisible` 切 |
| 同一模式里的**弹层**（音量条、菜单、来电提示） | 在本页图层下**新建一个顶层布局**，默认 `invisible` |
| 把几个控件**当一组**摆放/整体显示隐藏 | **嵌套一个布局** |
| 列表的**一行/一格**长什么样 | 列表下的 `listwidget[]` 里放**一个布局** |
| 别的 | **不要**建新图层 —— 见下 |

---

## 页（`ScenesScreen`）= 一个工作模式

样本工程 11 页，一页一个模式：

```
页0  菜单        页1  蓝牙       页2  FM        页3  时钟
页4  音乐        页5  开机       页6  关机      页7  系统设置
页8  PC(声卡)    页9  LINE-IN    页10 音频输入
```

- 页之间**互斥**，同一时刻只显示一页。
- 页 ID 是 `PAGE_0` `PAGE_1` …，**按页在工程里的顺序生成**。
  所以**插入或调整页顺序会改变所有后续页的编号** —— 应用代码里
  `#define ID_WINDOW_BT PAGE_0` 这类映射要跟着改。**尽量往后追加，不要往中间插。**
- 切页：`UI_SHOW_WINDOW(ID_WINDOW_BT)` / `UI_HIDE_CURR_WINDOW()`。
- 一页对应一个 `ui_action/<页面>_action.c`。

**判据：用户按哪个键进来的？** 同一个功能键进入、再靠左右键翻页的那几屏，
是**一页**，不是几页。

### ⚠ 别按"长得像不像"分页

这是最容易做错的地方，尤其是仪器类界面 —— 同一个功能下的几屏底图不同、
字段也不同，看着像几个独立模式，其实是一个。

**可操作的判据：看标题栏有没有 `n/m`。** 写着 `1/5` `2/5` 的那几屏必然是
同一模式的翻页 → **合成一页**，每屏一个整屏布局，靠 `invisible` 切。

实际例子（某音频处理器，IN 键下 5 屏）：

```
✗ 错：5 个 page —— GAIN / DELAY / EQ / D-EQ FILTER / D-EQ DYNAMICS
✓ 对：1 个 page「输入」
      INPUT_LAYER
        ├─ IN_GAIN_LAYOUT   invisible=false
        ├─ IN_DLY_LAYOUT    invisible=true
        ├─ IN_EQ_LAYOUT     invisible=true
        ├─ IN_DEQF_LAYOUT   invisible=true
        └─ IN_DEQD_LAYOUT   invisible=true
```

这条判据在一个 8 页 155 控件的实际工程里逐页验过，一次都没失手
（SETUP 1/6~6/6、SUM 1/5~5/5、MUTE 1/1、RECALL 1/1、STORE 1/4~4/4）。

拆成多页的两个后果：

1. 翻页要走 `UI_SHOW_WINDOW`，**整页重建**；合成一页后翻页只是切
   `invisible`，图层的 `draw_context` 不动，快得多
2. **共享状态没地方放** —— 上例里"当前通道 IN A/B/C"和底部
   `INPUT:x` 状态栏是 5 屏共用的，分成 5 页就得在页间传递

### 同一批屏在两个功能键下都出现，怎么办

会遇到：老设备里"求和(SUM)"其实就是第 4 个输入通道，SUM 的 2/5~5/5
和输入的 2/5~5/5 **用的是同一批底图**，只有标题栏的通道帧不同。

**框架里布局属于某一页，不能跨页共用**，所以只有两条路：

| 做法 | 代价 |
|---|---|
| 各建一份（SUM 独立 5 个布局，ename 前缀 `SUM_`） | 简单、直接；多几十个控件的冗余 |
| 合成一页，用"当前通道"状态区分 | 省资源；翻页/切通道的逻辑要自己管，事件回调复杂 |

上面那个实际工程选了前者，多了 35 个控件的冗余。**没有标准答案** ——
屏数少就各建一份，屏数多且完全同构才值得合并。

---

## 图层（`NewLayer`）= 一页恰好一个

样本工程 11 页，**全部都只有 1 个图层**，无一例外。

图层是页的唯一子节点，**整屏**（本仓库样例是 `128×64 @ 0,0`；尺寸看工程，
见 `layout.md` 第 3 节）。设备端 `struct layer` 自带
`draw_context`，它是绘制缓冲的单位 —— 不是用来做"图层叠加"的设计元素。

**不要一页建多个图层。** 想要"叠一层上去"的效果，用**布局 + `invisible`**，
见下面「弹层」。

图层命名照 `<模式>_LAYER`：`BT_LAYER` `MUSIC_LAYER` `SYSTEM_LAYER`。
图层可以注册事件回调（`struct layer` 有 `handler` 字段），但没有 `ui_layer_xxx()` API。

---

## 布局（`NewLayout`）= 两种用途

### 用途一：顶层布局 —— 主界面和弹层

图层下面直接挂的布局，**每一个都是整屏 `@ 0,0`**（样例里是 `128×64`），靠 `invisible` 区分：

```
BT_LAYER
├─ BT_LAYOUT           invisible=false   ← 主界面，常显
├─ BT_VOL_LAYOUT       invisible=true    ← 音量弹层
├─ BT_MENU_LAYOUT      invisible=true    ← 菜单弹层
├─ BT_MENU_EQ_LAYOUT   invisible=true    ← EQ 弹层
└─ BT_LAYOUT_CALL      invisible=true    ← 来电/通话层
```

样本里各页的顶层布局数：菜单 1、蓝牙 5、FM 3、时钟 5、音乐 6、
开机 1、关机 1、系统 9、PC 4、LINE-IN 4。**弹层多就是布局多，图层永远是 1 个。**

命名约定（应用代码按这个找控件）：

| 用途 | 命名 |
|---|---|
| 主界面 | `<模式>_LAYOUT` |
| 音量弹层 | `<模式>_VOL_LAYOUT` |
| 菜单弹层 | `<模式>_MENU_LAYOUT` |
| 其他弹层 | `<模式>_<用途>_LAYOUT` |

### ⚠ 弹层要排在主界面之后

按键分发规则是「**子元素从尾往前**，都不消费才轮到父节点自己」，
并且 `invisible` 的整棵子树**直接跳过**。

所以弹层只要满足两条，就自动获得"弹出时抢占按键"的效果：

1. 默认 `invisible = true`
2. 在 `layout[]` 数组里**排在主界面布局之后**

不需要在主界面的 `onkey` 里写「如果菜单开着就不处理」。
**顺序决定行为，摆的时候就要想好。**

> 样本工程里系统页有几个弹层排在 `SYSTEM_LAYOUT` 之前，那几个不享受这个自动优先，
> 得自己处理。新做界面不要学这一处。

### 用途二：嵌套布局 —— 分组和定位

布局里可以再套布局，用来：

- 把几个控件当一组整体显示/隐藏（给这一组一个 ename，`invisible` 一起切）
- 定位：子控件坐标**相对父布局**，把一组东西整体挪动只需改父布局的 `rect`
- 列表条目：`listwidget[]` 里每一项就是一个布局，一个条目长什么样由它决定

布局本身不画东西（除非设了背景色/背景图/边框），是纯容器。

---

## 列表（`VerticalList` / `HorizontalList`）

条目放在 `listwidget[]` 里，**每个条目是一个 `NewLayout`**，
条目内部再放图片、文字。

```
MUSIC_MENU_LIST  (VerticalList)  128x40 @ 0,24
├─ listwidget[0]  NewLayout  128x20 @ 0,0     ← 第一行
│    ├─ ImageList   图标
│    └─ Text        文字
├─ listwidget[1]  NewLayout  128x20 @ 0,20    ← 第二行
└─ ...
```

- 条目的坐标**相对列表**，自己按行高/列宽累加铺开。
- `scroll_mode`：`SCROLL`（连续滚动）/ `PAGE`（整页翻）。
- `highlight_index` 是默认高亮的行号。
- 设备端垂直列表、水平列表、表格**都是 grid**（类型码 5），API 也是 `ui_grid_*`。

⚠ **滚动和高亮别自己算**。grid 自带上下键滚动，写法是
「先调本页注册的 `onkey`，返回假才走自己那段」，所以列表的 `onkey` 里
把键值改写成 `UI_KEY_UP` / `UI_KEY_DOWN` 再 `return false` 就行。

---

## 建一页的完整清单

1. 在 `pages[]` **末尾**追加一页（别往中间插，会改变后续页的 `PAGE_n` 编号）
2. 页下建 1 个图层，`ename = <模式>_LAYER`，整屏
3. 图层下建主界面布局 `<模式>_LAYOUT`，整屏，`invisible=false`
4. 需要弹层就继续追加顶层布局，整屏，`invisible=true`，**排在主界面之后**
5. 往布局里摆控件，需要代码操作的**都要起 ename**
6. 应用侧：新建 `ui_action/<模式>_action.c`，定义 `ui_handlers_<模式>`，
   到 `config/ui_port_registry.c` 登记一行
7. `ui_style.h` 里加 `#define ID_WINDOW_<模式>  PAGE_n`
