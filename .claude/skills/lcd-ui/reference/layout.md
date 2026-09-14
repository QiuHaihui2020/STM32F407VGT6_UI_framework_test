# 工程脚本（.uiproj）的写法

`.uiproj` 是 UTF-8 的 JSON，行尾 LF。`.json` 后缀同样被接受，两者内容格式完全一样。

---

## 1. 树的形状

节点之间的父子关系靠**不同的键名**表达，每一层的键名不一样：

```
project
└── pages[]        → page          （ScenesScreen）
    └── layer[]    → NewLayer      图层
        └── layout[]   → NewLayout 布局
            ├── layout[]     → 叶子控件 / 嵌套 NewLayout / 列表
            └── ...
                VerticalList / HorizontalList
                └── listwidget[] → NewLayout（每个条目是一个布局）
```

用哪个键是有规矩的，写错了工具读不到子节点：

| 父 | 子数组的键 | 允许的子节点 |
|---|---|---|
| project | `pages` | page |
| page | `layer` | NewLayer |
| NewLayer | `layout` | NewLayout |
| NewLayout | `layout` | 任意叶子控件、NewLayout、VerticalList、HorizontalList |
| VerticalList / HorizontalList | `listwidget` | NewLayout（一个条目一个） |

叶子控件（Text / ImageList / Battery / Time / Number）**不再有子节点**。

---

## 2. `-class` / `-type` / `caption` 各管什么

三个字段职责完全不同，是这个格式最容易搞混的地方。

| 字段 | 管什么 | 取值 |
|---|---|---|
| `-class` | **结构角色**，决定它在树里是什么位置 | `ScenesScreen` `NewLayer` `NewLayout` `NewList` `NewFrame` `NewGrid` |
| `-type` | 编辑器里的控件种类 | `page` `NewLayer` `NewLayout` `Text` `ImageList` `Battery` `Time` `number` `VerticalList` `HorizontalList` |
| `caption` | **决定类型码**（见下），同时是编辑器里显示的名字 | 见 `typecodes.ini` |

`-class` 只有 6 个值，**它不是控件类型**。所有叶子控件的 `-class` 都是 `NewFrame`。

### ⚠ 类型码由 `caption` 决定，不是 `-type`

生成器查表的规则（`StyBuilder.cpp:446`）是 **`caption` 优先，查不到才退回 `-type`**：

```
caption 在 typecodes.ini 里？  → 用它
否则 -type 在 typecodes.ini 里？→ 用它
都不在                        → 警告「认不出控件类型」，该节点类型码为 0
```

后果：

- **不要"顺手整理" `caption`**。slider 的四个零件 `-type` 都是 `ImageList`，
  全靠 `caption` 是 `right_pic` / `left_pic` / `slider_pic` / `slider_text` 才被认成滑条零件。
  把 caption 改成「右边的图」，它立刻退化成一张普通图片，而且**不报错**。
- 新建节点时 `caption` 要照抄控件库里的值。

### 类型码 ↔ 设备端

`typecodes.ini` 里的数字就是设备端 `include/ui/control.h` 里的 `CTRL_TYPE_*`：

| caption | 码 | 设备端 |
|---|---|---|
| 页面 / page / ScenesScreen | 2 | `CTRL_TYPE_WINDOW` |
| 布局 / NewLayout | 3 | `CTRL_TYPE_LAYOUT` |
| 图层 / NewLayer | 4 | `CTRL_TYPE_LAYER` |
| 表格控件 / 垂直列表 / 水平列表 | 5 | `CTRL_TYPE_GRID` |
| 图片 / ImageList | 8 | `CTRL_TYPE_PIC` |
| 电池电量 / Battery | 9 | `CTRL_TYPE_BATTERY` |
| 时间 / Time | 10 | `CTRL_TYPE_TIME` |
| 文字 / Text | 12 | `CTRL_TYPE_TEXT` |
| 数字 / number | 15 | `CTRL_TYPE_NUMBER` |
| slider | 28 | `CTRL_TYPE_SLIDER` |
| vslider | 33 | `CTRL_TYPE_VSLIDER` |

⚠ 工具侧的 `window=1` 是**另一回事**，别拿它对应 `CTRL_TYPE_WINDOW`。

⚠ `typecodes.ini` 里还有 `Button(7)` `Camera(11)` `animation(13)` `player(14)`
`progressbar(20)` `multiprogress(22)` `watch(24)` —— 这些**控件库里没有、设备端也没实现**，
不要用。可用的就是上表这些。

---

## 3. 坐标

- 屏幕 **128 × 64**，单位**像素**。
- `rect` 在 `element_css` 里，形如 `{"x":0, "y":0, "width":128, "height":64}`。
- **坐标相对父节点**，不是绝对坐标。

实例（水平列表铺四个条目）：

```
NewLayer          x=0  y=0   w=128 h=64      整屏
└ NewLayout       x=0  y=0   w=128 h=64      整屏
  └ HorizontalList x=0 y=24  w=128 h=40      在屏幕的 y=24 处
    ├ NewLayout   x=0  y=0   w=32  h=40      条目相对列表
    │ └ ImageList x=0  y=0   w=32  h=39      图片相对条目
    ├ NewLayout   x=32 y=0   w=32  h=40
    ├ NewLayout   x=64 y=0   w=32  h=40
    └ NewLayout   x=96 y=0   w=32  h=40
```

### ⚠ 页节点的 rect 是个裸对象

页（`ScenesScreen`）自身的 rect 只有 `rect` 一个键，**不能带 `-name`**。
`StyBuilder::rectOf()` 的兜底分支写死了 `!o.contains("-name")`，带了就读不到，
后果是**那一页所有控件的几何全变 0**，而且不报错。

---

## 4. 节点里的其他字段

一个真实的叶子节点长这样：

```json
{
  "-class": "NewFrame",
  "-name": "文字_12",
  "-type": "Text",
  "caption": "文字",
  "icon": "",
  "tip": "",
  "version": "1",
  "widget": [],
  "property": [ ... ]
}
```

- `-name` 是编辑器里显示的名字，可以用中文，**不影响生成**。
  自动命名的前缀是 `BaseForm_<n>` 这类。
- `property[]` 装所有属性，每项 `{-name, -type, caption, ...}`，
  值放在哪个键取决于 `-type`（见 `widgets_props.md`）。
- 属性的 `-name` **可能重复**：`Text` 有两个 `-name: "color"`，
  靠 `caption`（文字颜色 / 高亮颜色）和**出现顺序**区分。按顺序读写，别去重。

---

## 5. 几个关键属性

### `id` —— 应用代码认控件全靠它

```json
{"-name":"id", "-type":"id", "caption":"唯一ID号", "ename":"MENU_TEXT", "id":0}
```

`ename` 会被 `--gen` 生成成 `ename.h` 里的宏（`#define MENU_TEXT 0X8CF3C3`，
值是哈希不是序号），应用代码就用这个宏挂事件回调。

规矩：
- 要让应用代码操作的控件，**必须起一个有意义的 `ename`**，全工程唯一。
- `ename` 会被转成大写作宏名，所以只用 `[A-Za-z0-9_]`。
- 改了 `ename` 就等于换了 ID，应用代码里对应的那一项要一起改。
- 纯装饰、代码不碰的控件可以留自动名。

### `element_css` —— 每个控件都有

固定 7 项：`align` `invisible` `flags` `rect` `background_color` `background_image` `border`。

- `invisible: true` 的控件默认不显示，而且**整棵子树都不参与按键分发** ——
  弹层就是这么做的。
- `flags` 取 `ELM_FLAG_NORMAL` / `ELM_FLAG_HEAD`。

### `str` —— 文字控件的内容

```json
{"-name":"str", "-type":"text-pic", "caption":"文字列表",
 "default":"m1", "list":["m1","m2","m42","m6"], "maxlength":100}
```

`list` 里放的是**多国语言表里的条目 id**，不是字面文字。真正的文字在工程目录的
`.xls` 里（`Resbuilder.xml` 的 `<excel_path>` 指向它）。一个 Text 控件可以挂多条，
运行时用 `ui_text_show_index_by_id()` 切换显示第几条。

### `action` —— 不写代码的联动

```json
{"-name":"action", "-type":"action", "action":[
  {"-name":"event",  "-type":"enum", "default":"KEY_OK", "enum":[{"KEY_OK":13}, ...]},
  {"-name":"action", "-type":"enum", "default":"SHOW",   "enum":[{"SHOW":0},{"HIDE":1}]},
  {"-name":"object", ...}
]}
```

声明式的「某事件发生时，显示/隐藏某个对象」，编进资源由框架执行，不需要应用代码。
适合纯界面联动（按 MENU 弹出菜单层）。要跑业务逻辑还是得写回调。

---

## 6. 不变量清单（改完自查）

1. 子数组键名对：`pages` / `layer` / `layout` / `listwidget`
2. `caption` 是控件库里的原值（决定类型码）
3. 页节点 rect 是裸对象，无 `-name`
4. `ename` 全工程唯一，只含 `[A-Za-z0-9_]`
5. `rect` 相对父节点，不超出父节点范围
6. `property[]` 的项数和顺序照控件库来，重复的 `-name` 不要合并
7. 引用的图片路径存在（相对工程目录，通常在 `config/pic_lcd/`）
8. `str.list` 里的 id 在多国语言表里有

前 6 条 `--json-roundtrip` 能兜住一部分，但**它只验「读得进、写得回」，
不验语义**。所以改完一定要 `--gen` 跑一遍看有没有「认不出控件类型」之类的警告。

---

## 7. 图层的 `color_format` 与绘制语义

图层属性 `color_format` 决定**整个图层怎么往显存里画**。这是点阵屏框架，
**只能用 `OSD1`**。

| 值 | 设备端 `dc->data_format` | 画图行为 |
|---|---|---|
| `OSD1` = 4 | `DC_DATA_FORMAT_MONO` | 只写亮点，暗点不动背景 |
| `OSD16` = 2 | 彩屏 16bpp | 无 alpha 时整行 `memcpy`，覆盖 |

### ⚠ 叠加还是覆盖，由控件的「背景填充」决定 —— 三态，不是两态

这是这套格式最容易踩的一处。画一个控件时固件做两步：

```c
/* ui_core_show_rect() */
if (elm->css.background_color != 0xffffff) {   /* 空串才等于 0xffffff */
    fill_rect(&dc, elm->css.background_color);  /* ← 先填/擦这一整块 */
}
draw_image(...);                                /* 再画自己的图 */

/* jlui_fill_rect() 的 MONO 分支 */
color = (color == UI_RGB565(BGC_MONO_SET)) ? 0xffff : 0x55aa;
/* draw_point: 0x55aa -> &= ~BIT（擦灭）；非 0 -> |= BIT（点亮） */

/* jlui_draw_image() 的 MONO 分支 —— 只画亮点 */
if (color) { draw_point(...); }   /* 0 像素直接跳过，不动底下 */
```

合起来是三种效果：

| 背景填充 | json 里的值 | 效果 |
|---|---|---|
| 不填充 | 空串 `""` | 透明。图**叠加**在底下的背景图上，暗像素不擦 |
| 填充（亮） | `#ff555aaa`（BGC_MONO_SET） | 整块点亮，再画图 |
| **擦除（灭）** | 其它任何颜色值 | 整块擦灭，再画图 = **盖住**底下的背景图 |

⚠ `StyBuilder::argbTo565()` 只有**空串**才输出 `0xFFFFFF`；任何有效颜色都会被
降成 565（≤0xFFFF）。所以「设了颜色」= 一定会 fill，区别只在亮还是灭。
换句话说：**json 里那些 `#D9EE94`、`#D2EE45` 之类的控件库默认色不是"没效果"，
它们等于"擦除"。**

### 复刻老设备时：字段框要用「擦除」

老设备的界面通常是「整屏底图 + 若干字段框」，字段框**盖住**底图对应位置。
用「不填充」的话底图那块内容会透出来，和帧糊在一起：

```
不填充：  → GAIN            CONFIG: SUB ◨ Lm ◨ Hm H     ← 糊了
擦除：    IN A → GAIN       CONFIG: SUB L LM M HM H      ← 对
```

编辑器的属性面板给了这三个选项（单色屏下不给取色器）；手写 json 就是上表那三种值。

### 亮/暗怎么定的：透明色键

BMP → 1bpp 的判据是**透明色键**，不是亮度阈值：

```c
/* ImageMono.cpp: rgb != 透明色键 -> 置位（亮） */
```

编辑器预览用的是**同一条判据**（`Preview::litPixmap()`），
所以只要图层是 OSD1，**预览和实机一致**。
一旦改成 OSD16，预览仍按点阵屏叠加画、屏上却是覆盖，两边就对不上了。
