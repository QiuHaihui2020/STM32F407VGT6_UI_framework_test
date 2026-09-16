# 各控件：什么时候用 · 怎么摆 · 代码怎么调

属性的完整取值见 `widgets_props.md`（自动生成）。这里讲**选型和用法**。

代码片段取自杰理原生 SDK 的 `apps/soundbox/ui/lcd/STYLE_02`（真实业务代码）。
⚠ 那边的**事件注册方式**在本框架不适用，只抄 API 调用，骨架照 `app.md`。

---

## 选型速查

| 要显示的东西 | 用 | 为什么 |
|---|---|---|
| 固定文案、菜单项名、状态词（"已连接"/"未连接"） | **Text** + 多条 `str` | 走多国语言表，切语言自动跟随 |
| 运行时才知道的字符串（歌名、设备名） | **Text** + `set_text` 系列 | |
| 状态图标（蓝牙连/断、播放/暂停） | **ImageList** 摆多张，切 index | 一个控件搞定一个状态机 |
| 单张静态图（logo、背景装饰） | **ImageList** 只摆一张 | 没有单独的"图片"控件 |
| 电量 | **Battery** | 自带分档和充电图逻辑 |
| 时:分:秒、播放进度 | **Time** | 自带分隔符和补零 |
| 音量值、频道号、纯数字 | **Number** | 自带位数和对齐 |
| 进度条、音量条、频率条 | **slider / vslider** | 组合控件，一次插一棵子树 |
| 可滚动的菜单、文件列表、EQ 选项 | **VerticalList / HorizontalList** | 滚动高亮框架包了 |
| 一组要整体显示隐藏的东西 | **NewLayout** | 容器，不画东西 |

---

## 两个都能做的时候选哪个

真正容易选错的不是"没有合适的控件"，是"有两个看起来都行"。下面每一组
都是实际会犹豫的，选错了不会报错，只是后面很难受。

### 数字：用 Number 还是 Text？

**用 Number。** 音量、频道号、序号都是。Text 也能显示数字，但你得自己
把整数转成字符串、自己补零、自己对齐，而且改一次刷一次字符串。
Number 直接塞 `struct unumber`，位数和对齐是属性配的。

例外：数字和文字混在一句话里（"第 3 首/共 12 首"）才用 Text。

### 时:分:秒：用 Time 还是两个 Number？

**用 Time。** 播放进度也是。Time 自带分隔符和补零，
一次 `ui_time_update_by_id()` 全刷完。拿两个 Number 拼，
分隔符要额外摆一个 Text，还得自己处理 `09` 这种补零。

### 一张固定的图：用 ImageList 还是布局的背景图？

- **要代码控制显示/隐藏/换图** → ImageList（有 ename，能调 API）
- **纯装饰，永远不变**（分隔线、边框花纹、整屏底图）→ 布局的 `background_image`

背景图不占一个控件，省资源；但它没有 ID，代码碰不到。

### 状态图标：一个 ImageList 摆多张，还是多个 ImageList 各摆一张？

**一个控件摆多张，切 index。** 蓝牙连/断、播放/暂停都是。
摆多个控件的话，切状态要"藏一个显一个"，两个都藏或都显的 bug 迟早出现。
一个控件切 index 是原子的。

### 电量：用 Battery 还是 ImageList 摆 5 张？

**用 Battery。** 表面上都是"按值选一张图"，但 Battery 自带
分档换算和充电态（`charge_image` 是独立一组图）。
用 ImageList 你得自己写 `percent → index` 的换算，还得自己处理充电动画。

### 进度/音量条：用 slider 还是 ImageList 摆很多张？

**用 slider。** 传百分比就行。ImageList 摆 21 张表示 0/5/10…% 这种做法
资源占用大、粒度粗，而且改范围要重摆图。

### 变化的数值/文字：用 Number/Text，还是预渲染图集？

**先看字体够不够用，这一条决定一切。**

Text/Number 走框架的 strpic，字体是工程 `Resbuilder.xml` 里配的
（参考工程是 `lfHeight="-16"` 宋体 —— 128 宽一行只放得下 8 个全角字）。

| 情况 | 选 |
|---|---|
| 配置的字体和设计稿一致，放得下 | **Number / Text / slider**。一次调用的事，资源小一个数量级 |
| 设计稿用的是 5×7 这类小点阵字（一行 20+ 字符），或界面**所有字符本来就是预渲染位图** | **ImageList 图集**。这是正确选型，不是偷懒 |

实测过的反面例子：某仪器界面参考稿是 `-15.0 dB`，改用 Number 后
16px 宋体画出来是 `0    dB` —— 字号和字宽都对不上，整屏没法看。

选了图集就要接受 30 张上限，超了得设备端按索引取图（见上面 ImageList 一节）。

**⚠ 别想着"整串图集放不下，那就逐字符摆等宽 ImageList"。**
老设备的点阵字多半是**比例间距**的（`1` 和 `.` 比字母窄），
按等宽格子摆出来的字距和参考稿对不上，一眼就看得出来。
实测方法：用工程自带的字库帧在参考截图上做模板匹配，把每个字符的
精确 x 读出来 —— 字宽不一致就说明是比例字体，这条路直接排除。

### 多个条目：用列表还是手摆几个布局？

- **条目数固定且很少（2~3 个）、不需要滚动** → 手摆布局更简单，
  高亮自己控
- **会滚动、条目多、或条目数运行时才知道** → 用列表

界线是**要不要滚动**。一旦需要滚动，自己实现滚动+高亮+翻页
是纯粹的重复劳动，grid 已经包好了。

### 一组东西要整体显示隐藏：套一层布局还是逐个切？

**套一层布局，切布局的 `invisible`。** 逐个切会漏，而且新增控件时
容易忘了加进切换列表。

### 想"叠一层上去"：新建图层还是新建布局？

**新建布局。** 图层一页只有一个（见 `authoring.md`）。
弹层 = 整屏布局 + `invisible=true` + 排在主界面之后。

---

## Text —— 文字

**什么时候用**：所有文字。分两种用法，选错了语言切换就失效。

**摆法**
- `str` 属性是个**列表**，里面放多国语言表里的条目 id（`m1` `m2` …），不是字面文字。
- 一个 Text 可以挂多条，运行时切"显示第几条" —— 状态词（已连接/未连接）
  就是这么做的，一个控件两条，不用两个控件。
- `source` 数据源、`code` 编码格式一般保持默认。
- 文字颜色和高亮颜色是两个 `-name` 都叫 `color` 的属性，靠顺序区分（见 layout.md）。

**代码怎么调**

```c
/* 一、切到 str 列表里的第 index 条（走多国语言表，跟随语言切换）*/
ui_text_show_index_by_id(BT_STATUS_TEXT, 1);   /* 显示"已连接" */
ui_text_show_index_by_id(BT_STATUS_TEXT, 0);   /* 显示"未连接" */

/* 二、塞运行时才知道的内容 */
ui_text_set_text_by_id(BT_MUSIC_NAME, "", 16, FONT_DEFAULT);
ui_text_set_textu_by_id(id, utf8_str, len, FONT_DEFAULT);   /* UTF-8 */
ui_text_set_textw_by_id(id, wide_str, len, endian, FONT_DEFAULT); /* 宽字符 */
```

⚠ STYLE_02 里有一条注释提醒：长歌名直接用 `ui_text_set_textu_by_id`
**不滚动、会被截断**。要滚动效果得走别的路子。

---

## ImageList —— 图片

**什么时候用**：任何图。名字叫 List 是因为它天生就是"一组图选一张显示"。

**摆法**
- `normal_image` / `highlight_image` 两个列表，分别是常态和高亮态。
- 状态图标就摆多张，代码切 index；静态图摆一张。
- 图片放工程目录 `config/pic_lcd/` 下，单色屏用 BMP。
- `cent_x` / `cent_y` 控制在框内怎么居中。
- ⚠ **`normal_image` 最多 30 张**（控件库里 `maxlength=30`）。
  对比：Battery 的 `image` / `charge_image` 是 0 = 不限，Text 的 `str` 是 100。

**⚠ 30 张上限是硬的，用图集表示数值时先算够不够**

有些设备的界面**所有字符本来就是预渲染位图**（整串 `-15.0 dB` 是一帧，
增益 301 帧、频率 321 帧这种图集）。这种情况下全用 ImageList 是正确选型，
不是偷懒 —— 见下面「选型：Number 还是图集」。

但 `normal_image` 只放得下 30 张。**超了在工程里就只能列首帧占位，
运行时切不动，等于一张死图**，而且不报错。

超过 30 帧的图集，正确解法不是换控件，是**设备端按索引从资源包取图**
（控件存图集基址 + 索引，老设备就是这么干的）。这需要改 `ui_pic` 侧，
属于固件活。工程里放占位是预期做法，但要**在动手前就知道**，
别列到第 30 张才发现。

**代码怎么调**

```c
ui_pic_show_image_by_id(BT_STATUS_PIC, 1);   /* 切到第 1 张：已连接图标 */
ui_pic_show_image_by_id(BT_STATUS_PIC, 0);   /* 第 0 张：未连接 */

ui_pic_set_hide_by_id(id, 1);                        /* 单独藏一张图 */
int n = ui_pic_get_normal_image_number_by_id(id);    /* 列表里有几张 */
```

---

## Battery —— 电池电量

**什么时候用**：电量图标。别用 ImageList 自己做，它自带分档和充电动画逻辑。

**摆法**
- `image` 是电量图片列表，**按电量从低到高摆**（样本摆了 5 张 `BATTLVL1..5.BMP`）。
- `charge_image` 是充电时用的图片列表，可以留空。

**代码怎么调**

```c
ui_battery_set_level_by_id(id, percent, incharge);  /* percent 0..100 */
ui_battery_level_change(percent, incharge);         /* 改所有电池控件 */

/* 回调里已经有控件指针，直接用带指针的版本 */
static int battery_onchange(void *ctr, enum element_change_event e, void *arg)
{
    struct ui_battery *battery = (struct ui_battery *)ctr;
    if (e == ON_CHANGE_INIT) {
        ui_battery_set_level(battery, get_vbat_percent(), 0);
    }
    return false;
}
```

`incharge` 非 0 时用充电图片列表。电量刷新一般挂个 1 秒定时器。

---

## Time —— 时间

**什么时候用**：时:分:秒。**播放进度也用它**（把 min/sec 填进去），
不要用两个 Number 拼。

**摆法**：`format` 定格式，`delimiter` 定分隔符，`auto_cnt` 是否自动走秒。

**代码怎么调**

```c
struct utime tm;                    /* u16 year; u8 month, day, hour, min, sec; */

sec = cur_ms / 1000;
tm.hour = sec / 60 / 60;
tm.min  = sec / 60 % 60;
tm.sec  = sec % 60;
ui_time_update_by_id(BT_MUSIC_CUR_TIME, &tm);
```

---

## Number —— 数字

**什么时候用**：音量、频道号、序号这类纯数字。

**摆法**：`format` 位数格式，`space` 字间距，`delimiter` 分隔符。

**代码怎么调**

```c
struct unumber num;                 /* u8 numbs; u8 type; u32 number[2]; u8 *num_str; */

num.type      = TYPE_NUM;
num.numbs     = 1;                  /* 几个数 */
num.number[0] = app_audio_get_volume(APP_AUDIO_CURRENT_STATE);
ui_number_update_by_id(BT_VOL_NUM, &num);
```

---

## slider / vslider —— 滑条

**什么时候用**：进度条、音量条、频率条。横向用 `slider`，纵向 `vslider`。

**摆法**
- 是**组合控件**：库里就是一棵子树，插入时整棵进工程
  （`right_pic` / `left_pic` / `slider_pic` / `slider_text` 四个零件）。
- ⚠ **零件的 caption 不能改** —— 类型码由 caption 决定，改了就退化成普通图片，
  而且不报错。详见 layout.md。
- `step` 是步进。

**代码怎么调**

```c
ui_slider_set_persent_by_id(FM_SLIDER, (fre - 8700) * 100 / (REAL_FREQ_MAX - 8700));
int p = slider_get_percent(slider);
```

传的是**百分比 0..100**，不是原始值 —— 上面这行就是把频率换算成百分比。

---

## VerticalList / HorizontalList —— 列表

**什么时候用**：菜单、文件列表、EQ 选项这类可滚动的多条目。

**摆法**
- 条目放 `listwidget[]`，**每条是一个 NewLayout**，里面再放图标和文字。
- 条目坐标相对列表，自己按行高累加。
- `scroll_mode`：`SCROLL` 连续滚 / `PAGE` 整页翻。
- `highlight_index` 默认高亮行。

**代码怎么调**

```c
ui_grid_highlight_item_by_id(id, item, true);
ui_grid_set_item_num(grid, n);          /* 动态条数 */
ui_grid_slide(grid, direction, steps);
```

⚠ **滚动和高亮不要自己算**。grid 的 `grid_onkey` 是「先调本页注册的 `onkey`，
返回假才走自己那段」，所以：

```c
static int menu_list_onkey(void *ctrl, struct element_key_event *e)
{
    if (ui_action_list_nav_key(e)) {   /* 把翻页键改写成 UI_KEY_UP/DOWN */
        return false;                  /* 交给 grid 内置滚动 */
    }
    /* 这里只处理"确认"这类自己的键 */
    return false;
}
```

---

## NewLayout —— 布局

**什么时候用**：容器。三种场合，见 `authoring.md`：
主界面/弹层（整屏 + `invisible`）、分组、列表条目。

不画东西，除非设了背景色/背景图/边框。没有 `ui_layout_*` API，
但可以注册 `onkey` —— 弹层的按键就挂在它身上。

---

## NewLayer —— 图层

**一页恰好一个**，整屏。是绘制缓冲的单位，不是设计元素。
想要"叠一层"用布局 + `invisible`。没有 `ui_layer_*` API，可以注册回调。

---

## 不可用的

`typecodes.ini` 里还列着 `Button(7)` `Camera(11)` `animation(13)` `player(14)`
`progressbar(20)` `multiprogress(22)` `watch(24)` —— **控件库里没有、设备端也没实现**。
需要按钮效果就用 ImageList + 高亮图；需要进度条用 slider。
