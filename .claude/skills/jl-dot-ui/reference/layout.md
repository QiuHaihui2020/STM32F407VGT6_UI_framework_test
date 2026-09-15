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

- 单位**像素**。
- **屏幕尺寸不是固定值，每个工程自己定**：编辑器里在 ui-config 的 `Size=宽*高`，
  json 里就是**页节点的 `rect`**（`ProjectModel::pageSize()` 读的就是它），
  设备端按面板的 `info.width/height` 画。
  工具里那个 `QRect(1,1,128,64)` 只是"页面缺 rect"时的兜底，不是规定。
  下面的例子都按本仓库样例工程的 128×64 写。
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

---

## 8. 同一个布局里两个控件重叠，谁盖谁

### 谁在上面：json 里靠后的

设备端画完自己再**正向**遍历子链表：

```c
/* __ui_core_show(), ui_core_dot.c */
for (p = elm->child.next; p != &elm->child; p = n) { ... __ui_core_show(q, rect); }
```

挂子节点时是按 `z_order` **升序**插入的，但两条规则让它在实际工程里退化成
纯 json 顺序：

- `z_order` 是 5 位，最大 31
- `ui_core_element_init()`：**`z_order == 0` 会被改成 31**（0 = 资源里没填，
  按"最上层"处理）

实测两个工程：只有图层节点带 `z_order: 0`，其余节点**根本没有这一项** ——
都归 31，谁也不比谁大，于是一律挂到链表尾部。

> **结论：`layout[]` 数组里靠后的控件画在上面。** 想调层次就调数组顺序。
> ⚠ 和按键分发**方向相反**：画是从头到尾，按键是从尾往前（见第 5 节弹层）。

### 盖不盖得住：还是看背景填充（第 7 节那三态）

| 上面那个控件的背景填充 | 重叠部分的结果 |
|---|---|
| 不填充（空串） | **叠加** —— 两个都看得见，暗像素不擦下面的 |
| 擦除（任意颜色） | **盖住** —— 先把自己整块擦灭，下面那个在这块里被抹掉 |
| 填充（`#ff555aaa`） | 先整块点亮再画自己 |

实测（同一个布局，两个 48×48 的图片控件错位重叠）：

```
A、B 都不填充          → 两个图标都完整          （叠加）
B 设「擦除」           → A 被 B 的矩形切掉一半   （B 盖住 A）
交换 A/B 的 json 顺序  → 和第一种一模一样        （OR 可交换，顺序不影响）
```

最后一条值得记：**只要都是透明的，调顺序不会改变画面** —— 顺序只有在
有人"擦除"或"填充"时才看得出来。所以排层次的时候，光调顺序没反应
不代表顺序没生效，是因为没人遮挡。

编辑器画布和右栏页面视图都按同一套规则画，和设备一致。

### 两个都设「擦除」呢

心智模型就一句：**按 json 顺序，每个控件依次「擦/填自己的矩形 → 画自己的内容」。**
后画的那个总是赢重叠区，因为它擦的时候把前面画好的一起擦了。

实测（底图是整屏的 "Alarm"，A 在前 B 在后，两个 48×48 错位重叠）：

| A / B 的背景填充 | 底图 | A 的图 | B 的图 |
|---|---|---|---|
| 都不填充 | `Alarm` 完整 | 完整 | 完整 |
| **都擦除** | 只剩 `'m` | **重叠部分被 B 切掉** | 完整 |
| 只 A 擦除 | 剩 `arm` | 完整 | 完整 |
| 只 B 擦除 | 剩 `A` + `'m` | 重叠部分被 B 切掉 | 完整 |

两条结论：

1. **「都擦除」和「只有后者擦除」在重叠区是一样的** —— 都是 B 赢。
   区别在非重叠区：A 的擦除额外把 A 矩形下面的底图也抹掉了。
2. **擦除只向下作用，不向后作用。** 第三行是证据：A 擦除了，但后画的 B
   完好无损，连 A 自己的图也完整 —— 因为 B 是透明的，没擦任何东西。

所以「我想让这个控件盖住下面」要设的是**这个控件自己**的擦除，
而不是去改被盖那个。
