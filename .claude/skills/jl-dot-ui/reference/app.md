# 应用层怎么用这些控件

## 1. 控件和代码是怎么对上的

```
工程 json                     生成                  应用代码
property[id].ename = "BT_BAT"  ──--gen──►  ename.h:                { .id = BT_BAT, ... }
                                           #define BT_BAT 0X...
```

`ename.h` 会被拷成应用侧的 ID 头（本仓库是
`User/ui_framework/include/common/style_jl02.h`），由 `include/common/ui_style.h` 引入。

- **ID 是哈希不是序号**（`PAGE_1 = 0x420001`）。不要假设连号，也不要硬编码数值。
- 那个 ID 头**自动生成，不要手改** —— 每次改界面都会被覆盖。
  业务语义的别名写在 `ui_style.h`，例如 `#define ID_WINDOW_BT  PAGE_0`。
- 页面的 ID 是 `PAGE_0` `PAGE_1` …，按页在工程里的顺序生成。

---

## 2. 事件回调表（本框架的注册方式）

> ⚠ **不要用 `REGISTER_UI_EVENT_HANDLER`。** 杰理原生 SDK（如 soundbox 的
> `ui/lcd/STYLE_02`）用的是链接段收集，靠链接脚本给出段边界符号。本框架
> `sec()` 是空宏、armlink 也造不出那类符号，照那套写会**编译过、链接过、
> 就是注册不上** —— 界面画得出来但一个事件都不响应，最难查的静默故障。
>
> STYLE_02 可以拿来参考**业务逻辑和控件 API 用法**，注册骨架必须照下面写。

一页一个 `<页面>_action.c`，文件尾定义本页的表：

```c
#include "ui_action.h"

static int bt_win_onchange(void *ctrl, enum element_change_event event, void *arg)
{
    switch (event) {
    case ON_CHANGE_INIT:        /* 申请页面私有状态、起定时器 */        break;
    case ON_CHANGE_FIRST_SHOW:  /* 拉一次数据刷到界面上 */              break;
    case ON_CHANGE_RELEASE:     /* 删定时器、释放 */                    break;
    default: return false;
    }
    return false;
}

static const struct element_event_handler bt_handlers[] = {
    { .id = ID_WINDOW_BT, .ontouch = NULL, .onkey = NULL,            .onchange = bt_win_onchange },
    { .id = BT_LAYOUT,    .ontouch = NULL, .onkey = bt_layout_onkey, .onchange = NULL },
    { .id = BT_BAT,       .ontouch = NULL, .onkey = NULL,            .onchange = bt_bat_onchange },
};

const struct ui_handler_group ui_handlers_bt = {
    .begin = bt_handlers,
    .end   = bt_handlers + ARRAY_SIZE(bt_handlers),
};
```

再到 `config/ui_port_registry.c` 的 `g_ui_handler_table` 里加一行 `&ui_handlers_bt`。
**漏登记是编译期未定义符号**，不会静默失效 —— 这正是改成显式表的目的。

三个口子：

```c
struct element_event_handler {
    int id;
    int (*ontouch)(void *, struct element_touch_event *);
    int (*onkey)(void *, struct element_key_event *);
    int (*onchange)(void *, enum element_change_event, void *);
};
```

返回值约定：**返回真 = 我消费了这个事件**，框架不再往下传；返回假 = 继续。

---

## 3. 控件生命周期

枚举名容易望文生义，下面是**实际触发顺序和语义**（源码见
`liba/ui_dot/ui_core_dot.c`，绘制那段在 `ui_core_show_rect()`）。

### 创建：只发一次

控件构造函数 `new_ui_xxx()` 里找到回调表之后立刻发：

```
ON_CHANGE_INIT          arg = NULL      控件结构已建好，还没画
```

grid 特殊：在子条目初始化**之前**发的是 `ON_CHANGE_INIT_PROBE`。

### 显示：每次重画都走一遍

```
1. ON_CHANGE_SHOW_PROBE    arg = NULL     给控件调整内容的机会
2. （框架组 draw_context）
3. ON_CHANGE_SHOW          arg = &dc      ★ 返回 0 = 我自己画完了，后面全跳过
4. 框架画背景色 / 背景图 / 边框
5. ON_CHANGE_SHOW_POST     arg = &dc
6. ON_CHANGE_FIRST_SHOW    arg = &dc      仅当 elm->state != 2，发完置 2
```

子元素全画完后，父元素再收一次：

```
ON_CHANGE_SHOW_COMPLETED   arg = NULL
```

开绘制上下文时另有一记，给控件调整 dc 的机会（grid 用它改可视区）：

```
ON_CHANGE_TRY_OPEN_DC      arg = dc
```

### 隐藏：不销毁

```
ON_CHANGE_HIDE             arg = NULL     置 state=1、invisible=1
```

控件还在，数据还在。**再显示时不会重发 `ON_CHANGE_INIT`，
也不会重发 `ON_CHANGE_FIRST_SHOW`**（state 已经是 2）。

### 销毁

```
1. ON_CHANGE_RELEASE_PROBE  arg = NULL    深度优先遍历整棵子树，一定会发
2. ON_CHANGE_RELEASE        arg = NULL    引用计数减到 0 才发
```

### 其他

```
ON_CHANGE_HIGHLIGHT        arg = (void *)yes   递归整棵子树，高亮位变化
ON_CHANGE_UPDATE_ITEM      arg 见 ui_grid.c    动态列表刷条目
ON_CHANGE_ANIMATION_END
```

### ⚠⚠ 头号大坑：绘制期回调里**不能**调 UI 更新 API

**这条排第一，因为它的症状是整页崩掉，而且看着完全像资源出问题。**

下面这些事件是**在绘制过程中**发出来的（`ui_core_show_rect()` 内部）：

```
ON_CHANGE_SHOW_PROBE   ON_CHANGE_SHOW   ON_CHANGE_SHOW_POST
ON_CHANGE_FIRST_SHOW   ON_CHANGE_SHOW_COMPLETED
```

在这些时机里**不要**调 `ui_pic_show_image_by_id()` / `ui_text_set_*` /
`ui_number_update_by_id()` 这类会更新控件的 API。它们会两头出事：

```
ui_pic_show_image_by_id()
  └ ui_pic_show_image()
      ├ ui_pic_set_image_index()
      │    └ platform_api->load_widget_info(...)   ← 覆盖那个唯一的 static 缓冲
      └ ui_core_redraw() / ui_core_show()
           └ __ui_core_show()                       ← 重入整套绘制递归
```

`load_widget_info()` 返回的是资源管理器里**唯一一份** `static union ui_control_info`
的地址，每次调用整块覆盖（框架在 `layer_init` / `layout_init` 的注释里写了两遍）。
外层遍历正读着它，被内层冲掉 → 后续记录读成垃圾 → **整棵子树的遍历散架**。

真实案例：某页窗口的 `FIRST_SHOW` 里刷了 10 个图片控件，结果那一页
**只剩第一个和最后一个控件画出来**，布局和背景全没了 —— 查了很久资源，
`.sty` 的 ctrl_num / 子指针 / invisible / 几何逐字段核过全是对的，问题根本不在资源。

**怎么写才对：框架自带 `ui_set_call()`**

```c
int refresh_cb(int param) { ...在这里随便调 ui_xxx_*... return 0; }

case ON_CHANGE_FIRST_SHOW:
    ui_set_call(refresh_cb, 0);    /* 推迟到本轮事件分发结束再执行 */
    break;
```

`ui_set_call(func, param)`（`ui/ui.h`）把调用排进队列，等本轮分发结束
（`handl.count` 归 0）由 `__do_wait_call()` 统一执行 —— 那时绘制已经走完，
再改界面就安全了。

### `ui_set_call()` 和直接调，是**两个方向都会错**的选择题

`ui_set_call()` 开头是 `if (!handl.count) return 0;` —— 它依赖一个只在
**控件 onchange 回调期间**才有效的上下文。用错地方两边都是**静默失效**：

| 你在哪 | 怎么刷界面 | 用错了会怎样 |
|---|---|---|
| 控件的 `onchange` 里（尤其绘制期那 5 个事件） | **`ui_set_call(cb, 0)`** | 直接调 → 重入绘制递归 + 冲掉 static 缓冲 → **整棵子树画不出来** |
| app 消息回调 / 定时器回调 / 按键处理 | **直接调 `ui_xxx_*`** | 用 `ui_set_call` → 句柄是空的，**调用被静默丢弃**，界面不刷新 |

两条都不报错，都是"代码看着对、就是没反应"。

杰理原生 SDK 的 `STYLE_02` 里两种写法都有，而且把理由都注释出来了：

```c
/* bt_action.c —— 在 onchange 里：推迟 */
case ON_CHANGE_FIRST_SHOW:
    ui_set_call(vol_init, 0);
    break;

/* bt_action.c —— 在消息回调里：必须直接调 */
static int music_vol_handler(const char *type, u32 arg)
{
    /* 这里必须直接调用，不能用 ui_set_call()：ui_set_call() 内部依赖一个
     * 上下文句柄，只在控件的 onchange 回调里有效；在本消息回调里那个句柄
     * 是空的，调用会被静默丢弃，表现为长按音量键时数字不刷新。 */
    vol_init(0);
```

本框架的签名是 `int ui_set_call(int (*func)(int), int param)` —— 回调返回 `int`。

### 弹层/子屏反复显示：用 `SHOW_POST`，不是 `FIRST_SHOW`

`FIRST_SHOW` **一辈子只发一次**（靠 `elm->state != 2` 判断）。弹层被收起后再弹出、
翻页切回某一屏，都不会再发 —— 数据会停在上次的值上。

`STYLE_02` 的原话（`bt_action.c:542`）：

```c
case ON_CHANGE_SHOW_POST:
    /* 每次显示都重新读一次当前音量：ON_CHANGE_FIRST_SHOW 只在控件第一次
     * 显示时触发，音量界面被 3 秒超时收起后再次弹出不会再走那里，
     * 数字会停在上一次的值上，看起来就像"音量变了但数字不动"。 */
    ui_set_call(vol_init, 0);
    break;
```

所以：**一次性的初始化放 `FIRST_SHOW`，每次显示都要刷的放 `SHOW_POST`**，
两者都用 `ui_set_call()` 推迟。

⚠ 不要为了解决"隐藏的屏不会重发 FIRST_SHOW"，就在**某一屏的回调里替所有屏**
刷数据 —— 那等于在一次绘制里触发十几次重入。每屏管自己的 `SHOW_POST`。

| 想做的事 | 放哪 |
|---|---|
| 首次显示时把数据刷上去 | `ON_CHANGE_FIRST_SHOW` 里 **`ui_set_call(cb, 0)`** |
| 让第一次绘制就显示对的帧 | 控件自己的 `ON_CHANGE_INIT` 里用 `ui_pic_set_image_index()`（它**不**触发重绘） |
| 给当前隐藏的那几屏补数据 | 在**那一屏被显示时**刷，不要在别的屏的回调里替它刷 |

`ON_CHANGE_INIT` / `ON_CHANGE_RELEASE` 不在绘制期，调 API 是安全的
（但 INIT 仍在创建遍历中，只设值、别触发重绘）。

> **⚠ 照抄 `ui_action/` 的范式时注意**：那三个参考文件里，只有布局那两处
> `ON_CHANGE_FIRST_SHOW` 带了「要改界面用 `ui_set_call()` 推迟」的 `@note`，
> 窗口和弹层那几处是**光秃秃的 `TODO: 刷xxx到界面上`**。照着那种 TODO 直接写
> `ui_xxx_set()` 就会踩上面这个坑 —— 已经踩过一次，整页只剩两个控件。

### ⚠ 另外四个容易踩的点

**一、`ON_CHANGE_SHOW` 返回 0 会跳过后面全部。** 包括背景、边框、
`SHOW_POST` 和 `FIRST_SHOW`。自绘控件（grid/text）就是靠返回 0 接管绘制的 ——
它们**收不到 `FIRST_SHOW`**。要在首次显示时做事，别指望自绘控件的 FIRST_SHOW。

**二、`SHOW_PROBE` / `SHOW` / `SHOW_POST` 每次重画都发。**
不要在里面申请资源、起定时器、做重计算 —— 一秒可能几十次。
「只做一次」的事放 `ON_CHANGE_INIT`（建控件时）或 `ON_CHANGE_FIRST_SHOW`（首次显示时）。

**三、隐藏 ≠ 销毁。** 弹层反复弹出收起走的是 `HIDE`/`SHOW`，
不会重新 `INIT`。所以「每次弹出都要刷新的数据」不能只在 INIT 里拉，
要在 `SHOW_PROBE` 或显示弹层的那段代码里主动刷。

**四、停定时器放哪。** `RELEASE_PROBE` 一定会发（整棵子树深度优先），
`RELEASE` 要等引用计数归零。顶层控件两者都会到，样本代码放在 `RELEASE`；
**拿不准就放 `RELEASE_PROBE`，那是框架注释里指定"停定时器、放外部资源"的时机。**

还有一条和时序无关但同样致命：定时器回调是**消息式投递**的，
`sys_timer_del()` 之后仍可能有一拍已经投出去在路上。
回调里必须先判页面私有状态是否已置空再用。

### 实际写页面用得最多的三个

| 时机 | 干什么 |
|---|---|
| `ON_CHANGE_INIT` | 申请页面私有状态、注册消息、起刷新定时器 |
| `ON_CHANGE_FIRST_SHOW` | 拉一次当前数据刷到界面 |
| `ON_CHANGE_RELEASE` | 删定时器、注销消息、释放私有状态 |

---

## 4. 各控件的 API

所有 `*_by_id()` 都用 `ename.h` 里的宏当 `id`，不需要先拿到控件指针 ——
**页面代码优先用 `_by_id` 版本**。带结构体指针的版本用在回调里（`ctrl` 就是那个指针）。

### 文字 Text（`ui/ui_text.h`）

```c
int  ui_text_show_index_by_id(int id, int index);   /* 切到 str 列表里的第 index 条（多国语言）*/
int  ui_text_set_str_by_id(int id, const char *format, const char *str);
int  ui_text_set_text_by_id(int id, const char *str, int strlen, u32 flags);
int  ui_text_set_textw_by_id(int id, const char *str, int strlen, int endian, u32 flags);  /* 宽字符 */
int  ui_text_set_textu_by_id(int id, const char *str, int strlen, u32 flags);              /* UTF-8 */
```

界面上摆的多条文字用 `show_index`（走多国语言表，跟随语言切换）；
运行时才知道的内容（歌名、设备名）用 `set_text`。

### 图片 ImageList（`ui/ui_pic.h`）

```c
int ui_pic_show_image_by_id(int id, int index);        /* 切到图片列表第 index 张 */
int ui_pic_set_hide_by_id(int id, int hide);
int ui_pic_get_normal_image_number_by_id(int id);      /* 列表里有几张 */
int ui_pic_get_highlgiht_image_number_by_id(int id);
```

图标状态机（蓝牙已连/未连、播放/暂停）就是在一个 ImageList 里摆好几张，
运行时切 index。

### 数字 Number（`ui/ui_number.h`）

```c
struct unumber { u8 numbs; u8 type; u32 number[2]; u8 *num_str; };
int ui_number_update_by_id(int id, struct unumber *n);
```

### 时间 Time（`ui/ui_time.h`）

```c
struct utime { u16 year; u8 month, day, hour, min, sec; };
int ui_time_update_by_id(int id, struct utime *time);
```

播放进度这类「分:秒」也用 Time 控件，把 min/sec 填进去即可。

### 电池 Battery（`ui/ui_battery.h`）

```c
int  ui_battery_set_level_by_id(int id, int persent, int incharge);
void ui_battery_level_change(int persent, int incharge);   /* 改所有电池控件 */
```

`incharge` 非 0 时显示充电图（用工程里的「充电图片列表」）。

### 滑条 slider / vslider（`ui/ui_slider.h`）

```c
int ui_slider_set_persent_by_id(int id, int persent);   /* 0..100 */
int slider_get_percent(struct ui_slider *slider);
```

### 列表 / 表格（`ui/ui_grid.h`）

垂直列表、水平列表、表格在设备端都是 grid（类型码 5）。

```c
int  ui_grid_highlight_item_by_id(int id, int item, bool yes);
int  ui_grid_slide(struct ui_grid *grid, int direction, int steps);
int  ui_grid_set_item_num(struct ui_grid *grid, int item_num);
void ui_grid_on_focus(struct ui_grid *grid);
void ui_grid_lose_focus(struct ui_grid *grid);
```

⚠ **别自己算高亮 index 和滚动**。grid 自带上下键滚动（`grid_onkey`），
它的写法是「先调本页注册的 `onkey`，返回假才走自己那段」——
所以列表的 `onkey` 里把键值改写成 `UI_KEY_UP/DOWN` 再 `return false`，
滚动、高亮、翻页就全交给框架了。

### 图层 NewLayer

图层和窗口一样是**结构节点，不是控件** —— 没有 `ui_layer_xxx()` API。
但它可以注册事件回调（`struct layer` 自带 `handler` 字段），常用来做弹层：
整层 `invisible`，需要时显示。

### 窗口

```c
UI_SHOW_WINDOW(ID_WINDOW_BT);
UI_HIDE_WINDOW(ID_WINDOW_BT);
UI_HIDE_CURR_WINDOW();
```

---

## 5. 弹层的做法（不用写状态判断）

按键分发规则：**子元素从尾往前，都不消费才轮到父节点自己**；
`invisible` 的整棵子树直接跳过。

所以弹层只要在工程里排在主布局**之后**，并且默认 `invisible`：

- 隐藏时：整棵跳过，按键直接落到主布局
- 显示时：排在后面，先拿到按键

不需要在主布局的 `onkey` 里写「如果菜单开着就不处理」这类判断。
这是布局顺序决定行为的典型例子 —— **摆位置的时候就要想好**。
