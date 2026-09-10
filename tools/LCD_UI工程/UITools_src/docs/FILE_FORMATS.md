# 杰理点阵屏 UI 工具链 —— 文件格式规范（逆向结果）

本文所有结构都不是猜的，来源有三，互相印证：

1. **固件里的解析器**（`User/ui_framework/`）—— 它就是这些格式的权威读者；
2. **原厂工具自己吐的结构转储** `project/debug.txt`；
3. **真实文件比对** —— `re/sty_dump.py` / `re/sty_roundtrip.py` 在
   `tools/JL/JL.sty` 上跑通，往返 24498 字节逐字节一致。

---

## 0. 工具链全景

```
                UITools/ui-tools.exe            (Qt5 布局编辑器)
工程 json  ──────────────┐
(SmallColorTFT.json)     │  编辑/保存
                         ▼
                  uitoolbin.bin  (其实是 JSON 文本，不是二进制)
                         │
                UITools/QTToolJson 1.1.exe
                UITools/QtToolBin.exe
                         ▼
                  Resbuilder.xml + config/*.bmp
                         │
                UITools/ResBuilder.exe
                         ▼
   project.bin   result.bin   result.str   ename.h   res_ver.h   debug.txt
        │             │            │           │
   copy_file.bat 按固定映射拷进固件工程：
        │             │            │           └─► User/ui_framework/include/common/style_jl02.h
        │             │            └──────────────► tools/JL/JL.str   字符串图片
        │             └───────────────────────────► tools/JL/JL.res   图片资源
        └─────────────────────────────────────────► tools/JL/JL.sty   窗口/控件布局
```

---

## 1. 工程文件 `*.json`（ui-tools 的原生格式）

顶层：

| 键 | 类型 | 说明 |
|---|---|---|
| `-name` | string | 工程名 |
| `-type` | string | 固定 `"project"` |
| `activePage` | int | 上次打开的页 |
| `lang_excel` | string | 多国语言表路径（相对工程目录） |
| `language` | array | 语言列表 |
| `pages` | array | 页面 |

节点（页/图层/布局/控件结构相同，只是层级不同）：

| 键 | 说明 |
|---|---|
| `-class` | 编辑器里的 Qt 类：`ScenesScreen` / `NewLayer` / `NewLayout` / `NewFrame` / `NewList` / `NewGrid` |
| `-type` | 业务类型：`page` / `NewLayer` / `Battery` / `Text` / `ImageList` / `Number` … |
| `-name` | 工程内唯一名（中文，如 `电池电量_2`） |
| `caption` / `icon` / `tip` / `version` | 显示用 |
| `property[]` | 属性数组，见下 |
| `layer[]` / `layout[]` / `widget[]` / `listwidget[]` | 子节点，按层级用不同的键 |

`property[]` 每项：`-name` / `-type` / `caption` / `ename` / `id` / `info`，
再加类型特有的 `default` / `enum[]` / `list[]` / `struct[]` / `min` / `max` / `maxlength`。
`-name == "rect"` 的那项里嵌 `rect:{x,y,width,height}`。

> `ename` 就是生成到 `ename.h` 里的宏名（如 `BT_LAYOUT`），固件靠它引用控件。

嵌套关系（实测 `SmallColorTFT.json`）：
```
project
 └─ pages[]            -class=ScenesScreen  -type=page
     └─ layer[]        -class=NewLayer
         └─ layout[]   -class=NewLayout
             ├─ layout[]      嵌套布局
             └─ widget[]      -class=NewFrame，-type 才是真正的控件类型
                 └─ listwidget[]   列表项
```

## 2. 控件库 `control/control.json`

**不在 exe 里，是外部数据文件**，所以这部分不需要逆向：

```json
{ "compoents": [ { "-class": "NewFrame", "-type": "Battery",
                   "caption": "电池电量", "icon": "config\\images\\POR_dark4.png",
                   "property": [ ... ], "widget": [], "tip": "", "version": "1" } ] }
```

顶层键名是拼错的 `compoents`（不是 components），照抄即可。
`control/ex/*.json` 是扩展控件（slider / vslider），`backgrounds/*.json` 是背景模板。

---

## 3. `JL.sty`（= `project.bin`）窗口/控件布局 —— **已完全验证**

### 3.1 文件头（24 B，固件 `struct ui_file_head`）

| 偏移 | 类型 | 名称 | 实测值（JL.sty） |
|---|---|---|---|
| 0x00 | u32 | `ui_version` | `0x240A90DD`，与 `ename.h` 的 `UI_VERSION` 宏一致 |
| 0x04 | u32 | magic2 | `0x6A978292`（固定） |
| 0x08 | u32 | hdr_ptr | `16`（固定） |
| 0x0C | u32 | total_size | `24414` = 文件大小 − (24 + 20×window_num) |
| 0x10 | u8  | type | 1 |
| 0x11 | u8  | window_num | 3（= 工程里的页数） |
| 0x12 | u16 | prop_len | 68 |
| 0x14 | u8  | rotate | 0/1/2/3 → 0/90/180/270 |
| 0x15 | u8[3] | rev | `FF FF FF` |

固件读的是 `head.res[16]` 的前 4 字节做版本比对
（`ui_resources_manager.c:169`），对不上直接 `ASSERT(0)`。

### 3.2 页表（每页 20 B，固件 `struct window_head`）

紧跟文件头，`window_num` 项：

| 偏移 | 类型 | 名称 |
|---|---|---|
| +0 | u32 | `offset` 该页数据块的绝对文件偏移 |
| +4 | u32 | `length` 该页数据块长度 |
| +8 | u32 | `table_ptr` 该页控件索引表的绝对偏移 |
| +12 | u16 | `table_size` |
| +14 | u16×3 | crc1/crc2/crc3 |

**恒等式（三页全部实测成立）**
```
offset + length == table_ptr + table_size == 下一页的 offset（末页 == 文件大小）
```

实测：
```
#  offset  length  t_ptr  t_size  crc
0  54      1166    107C   13E     148A 7A5 EC06
1  11BA    215A    30B2   262     5A84 A70D 4DA2
2  3314    2C9E    5C54   35E     373D 84D9 616E
```
与原厂 `debug.txt` 里那张表逐列相同。

### 3.3 页数据块内部

```
[页 offset]  窗口自身记录（实测 28 B）
             控件记录 × N     ← 靠 head.len 首尾相接
             css / 字符串 / 图片列表等散数据（控件头里的 css 偏移指进这里）
[table_ptr]  控件索引表（table_size 字节）
```

**控件头 16 B，固件 `struct ui_ctrl_info_head`：**

| 偏移 | 类型 | 名称 |
|---|---|---|
| +0 | u8 | `type` 控件类型 |
| +1 | u8 | `ctrl_num` 子控件数 |
| +2 | u8 | `css_num` |
| +3 | u8 | `len` 整条记录长度（含本 16 字节） |
| +4 | u8 | `page` |
| +5 | u8[3] | rev = `FF FF FF` |
| +8 | s32 | `id` |
| +12 | u32 | `css` 文件内偏移（不是内存指针） |

**`id` 的位域：**
```
 bit31..29  pj_id（资源工程号；本工程恒 0）
 bit23..22  页索引        页0=00  页1=01  页2=10
 bit21..16  控件类型      == head.type
 bit15..0   名字哈希      ← 算法未解出，见「未决项」
```
校验用：`(id >> 16) & 0x3F` 必须等于 `head.type` —— 三页共 277 条控件全部满足，
这也是定位第一条控件记录最可靠的锚点。

**控件类型码**（由 `ename.h` 的 id 与工程 json 的 `-type` 对照得出）

| type | 含义 | `-class` / `-type` |
|---|---|---|
| 2 | Window | ScenesScreen |
| 3 | Layout | NewLayout |
| 4 | Layer | NewLayer |
| 5 | List/Grid | NewList / NewGrid |
| 7 | Progress | Progress |
| 8 | ImageList | ImageList |
| 9 | Battery | Battery |
| 10 | Time/Watch | Time / Watch |
| 12 | Text | Text |
| 15 | Number | Number |

**负载布局**直接对应 `User/ui_framework/include/ui/control.h` 里的结构体，
`len` 就是结构体大小。逐字节验证过的例子（Text，len=48）：

```
0C 00 01 30  00 FF FF FF  D2 E6 0C 00  D4 0E 00 00   head：type=12 len=0x30 id=0x000CE6D2 css=0xED4
6E 6F 6E 65  00 00 00 00                             char source[8] = "none"
73 74 72 70  69 63 00 00                             char code[8]   = "strpic"
FF FF 00 64                                          int  color
FF FF 00 64                                          int  highlight_color
F8 0E 00 00                                          struct ui_text_list *str      → 0xEF8
FC 0E 00 00                                          struct element_event_action * → 0xEFC
```
`16 + 8 + 8 + 4 + 4 + 4 + 4 = 48` ✓
（`control.h` 上那句 `// text 40 bytes` 的注释是过时的。）

其余：`ui_battery_info` = 16+4×3 = 28 ✓，`layout_info` = 16+4+4 = 24 ✓，
`layer_info` = 16+4+4+4 = 28 ✓ —— 都与实测 `len` 吻合。

**css 偏移是从高地址往低地址分配的**（0x1000 → 0xFD8 → 0xFB0 → …），
即控件按顺序写、css 块倒着长，两头往中间夹。

### 3.4 验证方式

```bash
python re/sty_dump.py      ../../JL/JL.sty     # 打印结构，等价于原厂 debug.txt
python re/sty_roundtrip.py ../../JL/JL.sty     # 拆开再拼回，逐字节比对
```
当前结果：`往返一致：24498 字节逐字节相同 —— 切分无遗漏`。
C++ 侧同样的逻辑在 `src/core/StyFile.cpp::verifyRoundTrip()`，
也可以直接 `UITools --sty-dump <文件>`。

---

## 4. `JL.res` / `JL.str`（图片资源 / 字符串图片）

魔数 `"RU21"`。固件 `liba/res/resfile.c` 里有完整解析器，头注释已经把布局写清楚：

```
偏移 0            RES_HEAD_T   magic("RU21") / version / bPanelType / totalPage / resver
偏移 16 + n*8     RES_PAGE_T   { u32 pageNum; u32 pageAddr; }
pageAddr          RES_ENTRY_T  { u32 dwOffset; u16 wCount; u8 bItemType; u8 langsum; u32 language; }
dwOffset + id*20  RES_BMP_T / RES_STR_T
                  { u16 head_crc; u16 data_crc; u16 res_type; u16 typeId;
                    u16 wWidth; u16 wHeight; u32 dwLength; u32 dwOffset; }
```

像素数据可能是 RLE 或 QuickLZ 压缩的，解码入口 `image_decode()`，
实现分别在 `liba/res/rle.c` 与 `liba/res/quicklz.c`。

`res_ver.h` 里的 `IMAGE_VERION` / `STRING_VERION` 就是写进
`RES_HEAD_T.resver` 的值，固件用 `res_file_version_compare()` 校验。

空工程的 `result.str` 只有 28 字节，正好是一个头，可当最小样本：
```
52 55 32 31  01 01 01 00  00 00 00 00  AE 27 1B 0C
1C 00 00 00  00 00 53 03  07 00 00 00
'R' 'U' '2' '1'  ver=0x0101 panel=0x0001  totalPage=0  resver=0x0C1B27AE
```

---

## 5. `ename.h` → `style_jl02.h`（控件 ID 表）

由工具生成，`copy_file.bat` 拷成 `User/ui_framework/include/common/style_jl02.h`，
**不要手改**。格式就是一堆 `#define <ENAME> 0X<24位ID>`，外加一条
`#define UI_VERSION 0X240A90DD`（写进 `.sty` 头）。

ID 的位域见 3.3。固件侧对 ID 的用法：
- `ui_core_api.c:286`：`switch (((u32)id >> 16) & 0x3f)` 分派控件类型；
- `ui_style.h`：把 `PAGE_0..PAGE_10` 映射成业务语义的 `ID_WINDOW_*`。

---

## 6. 补充：后续逆向出的三块（原来标"未决"的）

### 6.1 指针都是「页内相对」，不是文件偏移

固件 `jlui_load_css()` 里写得很清楚：

```c
res_fseek(ui_file, offset /* 该页 window_head.offset */ + ((u32)_css & 0xffff), SEEK_SET);
res_fread(ui_file, &css, sizeof(struct element_css1));
```

所以控件里的 `css` / `str` / `action` / `ctrl` 这些"指针"字段：

```
 bit31..29  pj_id       资源工程号（本工程恒 0）
 bit28..22  page        页号，(ptr >> 22) & 0x7f
 bit15..0   offset      **相对该页 window_head.offset** 的偏移
```

实测：三页共 277 个控件的 css 指针，按 `页基址 + (ptr & 0xFFFF)` 解出来
**全部落在"控件区结束 ~ 索引表开始"这段里**，且最小的那个正好等于控件区的
结束位置。`(ptr>>22)&0x7f` 三页全为 0（同页引用）。

按这个解，`element_css1`（36 B，见 `ui_core.h`）就解得通了：

```
css@0x1054  NewLayer  align=1 inv=0 z=0 rev=255 rect=(0,0,10000,10000)
            bg=0xFFFFFF a=100 img=0xFFFFFF q=255 border=0/0/0/0 c=0xFFFFFF
css@0x102C  NewLayout align=1 inv=0 z=0 rev=255 rect=(0,0,10000,10000)
            bg=0x00001F a=100 ...      ← 0x001F = RGB565 的蓝
css@0x1004  NewLayout align=1 inv=1 z=0 rev=255 rect=(0,0,10000,10000)
            bg=0x005540 a=100 ...
```

注意 **rect 是万分比不是像素**（`10000` = 占满父级）。工程 json 里存的是绝对
像素，`QTToolJson` 负责换算：`x 112/128 -> 8750/10000`、`width 16/128 -> 1250`。

css 块 36 字节一条，从高地址往低地址分配（0x1054 → 0x102C → 0x1004 …），
控件记录从低往高写，两头往中间夹。

### 6.2 页尾那张 u16 表是**重定位表**

`window_head.table_ptr/table_size` 指向的那张表，是一串 u16，内容是
**控件区里每一个"指针字段"的页内偏移**，从高到低排列，最后一项是窗口记录里的
那个指针。

实测（页0：159 项 / 页1：305 项 / 页2：431 项）：除每页固定 1 项指向窗口记录外，
其余**全部**能对上某条控件记录里的某个 4 字节字段。逐类型统计出来的指针字段
偏移非常规整，正好就是各 `ui_*_info` 结构体里的指针成员：

| type | 控件 | 指针字段偏移 |
|---|---|---|
| 3 | NewLayout | +12(css) +16(action) +20(ctrl) |
| 4 | NewLayer | +12 +20 +24 |
| 5 | NewList/NewGrid | +12 +20 +24 |
| 8 | ImageList | +12 +24 +28 +32 |
| 9 | Battery | +12 +16 +20 +24 |
| 10 | Time/Watch | +12 +92 |
| 12 | Text | +12 +40 +44 |
| 15 | Number | +12 +92 |

有了这张表就能写 `.sty` 生成器：控件、css、列表数据各自摆好之后，
把所有指针字段的偏移收集起来倒序写进表尾即可。

### 6.3 工程 json 里控件的几何在 `element_css` 里

这点很容易踩：**控件节点没有顶层 `rect` 属性**。`property[]` 长这样：

```
{-name:"id",          -type:"id",           caption:"ID号",   ename:"BT_LAYER", id:0}
{-name:"element_css", -type:"struct",       caption:"CSS元素", info:"", struct:[ [ ... ] ]}
{-name:"color_format",-type:"color-format", caption:"颜色类型", default:..., enum:[...]}
{-name:"action",      -type:"action",       caption:"事件属性", action:[...]}
```

`element_css.struct` 是**每个 CSS 状态一项**的数组（`CSS属性_0` 的下标就是它），
状态内部才是：

```
{-name:"align",            -type:"enum",             caption:"对齐方式", default:"ALIGN_CENTER", enum:[...]}
{-name:"invisible",        -type:"enum",             caption:"默认隐藏", default:"false",        enum:[...]}
{-name:"z_order",          -type:"int8",             caption:"z轴坐标",  default:0, min:0, max:128}
{-name:"rect",             -type:"rect",             caption:"坐标",     rect:{x,y,width,height}}   ← 几何在这
{-name:"background_color", -type:"background-color", caption:"背景颜色"}
{-name:"background_image", -type:"background-image", caption:"背景图片"}
{-name:"border",           -type:"border",           caption:"内边框线"}
```

只有**页节点**是例外：它的 `property[0]` 是个只有 `rect` 键、连 `-name` 都没有的
裸对象。解析时两种都要认。

### 6.4 剩下真正没解的

| 项目 | 状态 | 影响 |
|---|---|---|
| `id` 低 16 位的哈希算法 | **未解出** | 只影响"产出与原厂逐字节相同的 ename.h"；不影响替换 `ui-tools.exe`（ename.h 是 QtToolBin 生成的） |
| `element_event_action` 的二进制布局 | 未逆向 | 事件动作编辑不可用 |
| `.res` / `.str` 的写入（含 RLE / QuickLZ 压缩） | 未实现 | 读的规范齐了（固件里就有解析器）；这是 `ResBuilder.exe` 的活 |

### 关于 ID 哈希（排除清单）

已排除（`re/hash_probe*.py`、`re/hash_brute*.py`，281 条样本全部 0 命中）：

- djb2 / sdbm / BKDR(31,131,1313) / FNV-1a / AP，掩码 20/21/22/23/24/32 位
- CRC16 的 9 种标准变体（XMODEM / CCITT-FALSE / MODBUS / IBM / MAXIM / USB / KERMIT / X25 / DNP）
- **CRC16 全 65536 个多项式** × {init 0x0000, 0xFFFF, 0x1D0F} × {MSB, LSB 两种移位方向}
  × 6 种输入变体（ename 原样 / 补 NUL / 小写 / 小写补 NUL / UTF-16LE / 字节反序）
- 乘法哈希 `h = h*mul + c`，mul 遍历 2..4095 × init ∈ {0,1,5381,0xFFFF,0x1505}
- 输入换成工程 json 里的中文 `-name`（UTF-8 / GBK）、`caption`、`-type`

也确认了 ID **不是**存在工程文件或任何中间文件里的。

继续查的思路：那段哈希在 `QtToolBin.exe` 的 `.text` 里（不在 ui-tools.exe），
可以从写 `ename.h` 的代码路径（找 `"#define %s 0X%X"` 一类格式串的引用）反向定位。

## 7. 复现脚本

```bash
cd re
python sty_dump.py      ../../../JL/JL.sty     # .sty 结构（等价原厂 debug.txt）
python sty_roundtrip.py ../../../JL/JL.sty     # 拆开重拼，逐字节比对
python reloc_probe.py   ../../../JL/JL.sty     # 验证页内相对指针 + 重定位表
python css_probe2.py    ../../../JL/JL.sty     # css 区与索引表的原始字节
python qtjson.py        <工程.json>            # 验证工程 json = Qt toJson(Indented)
python probe_css.py     <工程.json>            # element_css.struct 的结构
python json_fmt.py      <工程.json> <uitoolbin.bin>   # 两者差异（万分比换算）
```


---

## 8. `.sty` 的**排布算法**（已复现，逐字节验证）

第 3 节讲的是"每个字段在哪"，这一节讲"生成时怎么摆" —— 这是写生成器真正需要的。
验证方式：`re/sty_gen.py` 把 `JL.sty` 解析成**语义模型**（每个控件有哪些 css /
列表 / 动作，只留值不留地址），然后丢掉所有偏移，自己重新分配地址、重算全部
指针、重建页尾重定位表，再和原文件比。结果：

```
重定位表重建一致: 3/3 页
原文件 24498 字节，重排后 24498 字节
★ 逐字节相同 —— 排布算法复现成功
```

### 8.1 数据块的种类与大小

| 块 | 结构 | 大小 |
|---|---|---|
| css | `element_css1` | 36 B 定长 |
| image_list | `u16 num; u16 image[num]` | 2 + 2n，条目是 ResBuilder 分配的图片 ID |
| text_list | `u16 num; u16 str[num]` | 2 + 2n，条目是字符串资源 ID |
| action | `u16 num; { u16 event; u16 action; s32 id; u8 argc; char argv[argc] }[num]` | 每条按 4 字节对齐 |

每块之后按 **4 字节对齐**，填充字节是 **0xFF**。

### 8.2 分配顺序

- **组间**：一个控件一组。**控件 0 的组在最高地址**，往后依次向低地址排。
- **组内**：按**结构体字段偏移升序**摆（css 在最低，action 在最高）。
  - type 3 NewLayout：`css(+12) < action(+16)`
  - type 4 NewLayer： `css(+12) < action(+20)`
  - type 8 Pic：      `css(+12) < img_normal(+24) < img_high(+28) < action(+32)`
  - type 9 Battery：  `css(+12) < img_normal(+16) < img_charge(+20) < action(+24)`
  - type 12 Text：    `css(+12) < strlist(+40) < action(+44)`
- **空列表照样占位**：列表字段的指针即使是 null，也**仍然写一个 `0x0000` 块**
  （占 4 字节，含对齐填充），指针保持 null。三页合计 59 个这样的空块。
- 数据区正好从"控件区结束"排到"索引表开始"，不留额外尾巴。

### 8.3 重定位表的生成

```
从最后一个控件往前遍历：
    该控件的所有指针字段（css / 列表 / action / ctrl / layout / info），
    按字段偏移升序，写入各自的**页内偏移**（u16）
最后补一项：窗口记录里那个指向第一个控件的指针的页内偏移（实测 0x18）
```
三页重建结果与原文件完全一致。

### 8.4 页表里的 3 个 CRC（全部确证，9/9 验证通过）

字段名直接来自固件 `ui_resources_manager.c` —— 这张表原来是有名字的：

```c
struct window_head {
    u32 offset;            // +0   该页数据块的绝对偏移
    u32 len;               // +4   该页数据块长度
    u32 ptr_table_offset;  // +8   指针表（= 重定位表）绝对偏移
    u16 ptr_table_len;     // +12
    u16 crc_data;          // +14  CRC16(页数据 [offset, offset+len))
    u16 crc_table;         // +16  CRC16(指针表)
    u16 crc_head;          // +18  CRC16(本表项**前 14 字节**)
};
```

三个都是 **CRC16-XMODEM，init = 0**（固件的 `CRC16()` 就是
`crc16_xmodem(ptr, len, 0)`，见 `liba/common/jl_crc.c`）。

之前扫遍页内所有区段都找不到 `crc_head`，是因为它根本不是对页数据算的，
而是对**页表项自己**算的。

**固件是真校验的**：

```c
u16 crc = CRC16(&window, offsetof(struct window_head, crc_data));  // 前 14 字节
if (crc == window.crc_head) { ... } else return NULL;              // 对不上直接失败
...
u16 crc = CRC16(ui, window.len);
if (crc == window.crc_data) { ... }
```

所以生成器必须算对，不能糊弄。验证脚本 `re/verify_crc.py`：

```
页0/1/2 × crc_head/crc_data/crc_table  ->  一致 9 / 不一致 0
```

`re/sty_gen.py` 现在自己算这三个 CRC（不再照抄），重排结果仍与原文件逐字节相同。

### 8.5 还差什么才能"从工程 json 全新生成 .sty"

| 缺口 | 说明 |
|---|---|
| 窗口记录 28 字节 | 字段含义未拆（`window_info` + 4 个未知 dword，其中末尾是首控件偏移） |
| 各控件负载字段 | `source[8]` / `code[8]` / `format[16]` / `color` / `number[10]` … 与 json 属性的对应关系 |
| css 内容 | `element_css1` 各字段 ← `element_css.struct[N]`，坐标要换算成万分比 |
| 列表内容 | 图片/字符串 ID ← **依赖 ResBuilder** 先产出 `result_pic_index.h` / `result_str_index.h` |
| action 内容 | ← json 的 `action` 属性 |
| id 低 16 位 | 见 `QTTOOLBIN.md`，不可复现，用自己的确定性方案即可 |


---

## 9. 工程 json → `.sty` 的**内容映射**（本轮打通）

第 8 节解决"字节摆在哪"，这一节解决"字节里写什么"。全部在真实工程上逐条对拍过。

### 9.1 窗口记录（页头 28 字节）—— `re/verify_window.py`，18/18 一致

```c
u8  type;        // = option.ini 里 page/ScenesScreen = 2
u8  ctrl_num;    // 该页的图层数（json pages[i].layer 的长度）
u8  css_num;     // 0
u8  len;         // 0
u8  rev[4];      // 00 FF FF FF
int left, top, width, height;   // 万分比，整页固定 (0, 0, 10000, 10000)
u32 layer;       // 页内相对偏移，指向第一个图层控件（实测恒为 0x1C）
```

### 9.2 `element_css1` 36 字节 —— `re/verify_css.py`，各字段 273/273 一致

| 字段 | 来源 | 规则 |
|---|---|---|
| `align` | `element_css.struct[N].align` | 取 enum 里 default 对应的整数 |
| `invisible` | 同上 `.invisible` | 同上 |
| `z_order` | 同上 `.z_order` | `default` 直接用 |
| `rev` | — | 恒 `0xFF` |
| `left/top/width/height` | 同上 `.rect` | **`ceil(v × 10000 / 父节点对应边长)`** |
| `background_color:24 / alpha:8` | 同上 `.background_color` | 见下 |
| `background_image:24 / quadrant:8` | 同上 `.background_image` | 空 → `0xFFFFFFFF` |
| `border.left/top/right/bottom` | 同上 `.border.border` | 直接取 |
| `border.color` | 同上 | 同颜色规则 |

**坐标是"相对父节点"的万分比，而且是向上取整**，不是四舍五入：
`95×10000/128 = 7421.875 → 7422`、`17×10000/128 = 1328.125 → 1329`、
`3×10000/128 = 234.375 → 235`。用 round 会有一半对不上。

**颜色规则**（css 与控件负载里的 color 字段共用一套）：

```
json 里是 "#AARRGGBB" 字符串：
    RGB565 = (R>>3)<<11 | (G>>2)<<5 | (B>>3)      放低 24 位
    alpha  = round(A × 100 / 255)                 放高 8 位
空串表示"不设置" -> 低 24 位写 0xFFFFFF，alpha 仍为 100
```
实测：`#ff0000ff → 0x6400001F`、`#ffffaa7f → 0x6400FD4F`、`"" → 0x64FFFFFF`。

### 9.3 控件负载字段 —— `re/verify_payload.py`，全部一致

结构体照抄固件 `include/ui/control.h`，字段值来自 json 的同名属性：

| type | 负载 | json 属性 |
|---|---|---|
| 4 NewLayer | `u8 format` + rev[3] + action + layout | `color_format` 的枚举值（OSD1=4 / OSD16=2） |
| 3 NewLayout | action + ctrl | — |
| 5 List/Grid | `u8 page_mode` + `s8 highlight_index` + pad + action + info | `page_mode` / `highlight_index` |
| 8 ImageList | `u8 highlight` + pad + `u16 cent_x` + `u16 cent_y` + pad + 两个图片列表 + action | `highlight` / `cent_x` / `cent_y` |
| 9 Battery | normal_image + charge_image + action | — |
| 12 Text | `char source[8]` + `char code[8]` + color + hi_color + str + action | `source` / `code` / 两个 `color` |
| 10 Time | source[8] + `u8 auto_cnt` + rev[3] + `char format[16]` + color + hi_color + `u16 number[10]` + `u16 delimiter[10]` + action | `source` / `auto_cnt` / `format` / `number` / `delimiter` |
| 15 Number | source[8] + format[16] + color + hi_color + number[10] + delimiter[10] + space[2] + action | 同上 |

`number[10]` / `delimiter[10]` 里存的是**图片资源 ID**，空位填 `0xFFFF`。

### 9.4 一个必须知道的事实：原厂输出**本身就不可复现**

`Time` 控件的 `char format[16]`，字符串写完之后剩余字节**不清零**，是栈/堆上的垃圾：

```
BT_MUSIC_CUR_TIME    format = "m:s/" + 00 00 00 74 9C 3C 05 B8 B3 F3 00
BT_MUSIC_TOTAL_TIME  format = "m:s/" + 00 00 00 78 AF 3C 05 B8 B3 F3 00
MUSIC_CUR_TIME       format = "m:s/" + 00 00 00 F0 E6 2A 05 B8 B3 F3 00
MUSIC_TOTAL_TIME     format = "m:s/" + 03 00 00 00 CC F9 2A 05 B8 B3 F3 00
```

那几个 `xx xx 3C 05` / `xx xx 2A 05` 明显是堆指针，四个控件各不相同。
**原厂工具自己重跑一次也不会产出相同的字节。** 所以"和原厂逐字节相同"
根本不是一个可达的验收标准，正确的标准是**语义等价 + 固件能正常跑**。

### 9.5 还没解决的

| 项 | 情况 |
|---|---|
| `action` 块的内容 | 本工程 277 个控件的 action **全是 `num=0`**（空），没有可对照的样本；结构本身已知（固件 `struct event_action`） |
| image / text list 的条目 | 是资源 ID，**必须先有 ResBuilder** 分配 |
| id 低 16 位 | 不可复现，用自己的确定性方案（见 QTTOOLBIN.md） |

---

## 10. `result.bin`(=JL.res) / `result.str`(=JL.str) —— 完全打通

读侧权威是固件自己的解析器：`liba/res/resfile.c` 定结构、
`lcd_drive/middle/ui_synthesis_oled.c:488` 定像素排布。
写侧已由重建版 `ResBuilder` 复现，除下面列出的两处外与原厂**逐字节相同**。

### 10.1 容器布局（完全确定，没有对齐/填充）

```
0x00                RES_HEAD_T 16 B
                      char magic[4] = "RU21"
                      u16  version  = 0x0101
                      u16  bPanelType     LCDPANEL=0 / OLEDPANEL=1
                      u16  totalPage      .res 是页数；.str 放**语言数**
                      u16  reserved = 0
                      u32  resver         见 10.6
0x10                RES_PAGE_T[totalPage] 8 B/项 { u32 pageNum; u32 pageAddr; }   仅 .res

每页从 pageAddr 起，一段接一段，中间没有空隙：
  +0                  调色板索引 RES_ENTRY_T   bItemType='T'(0x54) wCount=1  langsum=0 language=0
  +12                 图片索引   RES_ENTRY_T   bItemType='P'(0x50) wCount=N  langsum=0 language=0
  +24                 RES_PAL_T { u32 num=256; u32 dwOffset; u32 dwLength=1024 }
  +36                 RES_BMP_T[N]  20 B/项
  +36+20N             调色板数据 1024 B（256 项，每项 B,G,R,00）
  +36+20N+1024        像素数据，按 id 顺序紧密排列
下一页的 pageAddr = 上一页像素数据的末尾。

.str 没有页表：头之后直接一个 RES_ENTRY_T（bItemType='S'=0x53，
langsum=语言数，language=语言位掩码），dwOffset=0x1C，
后面是 RES_BMP_T[wCount]，再后面是像素。条目按**语言分组**：
[语言0 的 1..N][语言1 的 1..N]…，固件 open_string_pic 用
tmp = (wCount/langsum)*(language_index-1) + id 定位。
```

`RES_BMP_T`：

```
u16 head_crc     CRC16-XMODEM(本结构第 2..20 字节)，固件会校验
u16 data_crc     .str 里是 CRC16-XMODEM(像素)；**.res 里原厂恒写 0**
u16 res_type     0=图片 1=字符串
u16 typeId       (compress<<13) | (format<<10) | id
u16 wWidth, wHeight
u32 dwLength     见 10.3 —— 原厂这个字段是**错的**
u32 dwOffset
```

`typeId` 里的 `id`：`.res` 是**页内**编号 1..N；`.str` 是**跨语言的流水号** 1..wCount
（固件不读它，只是个计数器）。

### 10.2 像素格式 OSD1 = 单色竖向分页

固件 `ui_synthesis_oled.c:488` 就一句话说明白了：

```c
int offset = (r.top + h - disp.top) / 8 * file.width + (r.left - disp.left);
br23_read_image_data(fp, &file, pixelbuf, r.width, offset);
u8 color = (pixelbuf[w] & BIT((r.top + h - disp.top) % 8)) ? 1 : 0;
```

即 `byte[(y/8)*width + x]` 的第 `y%8` 位，**行距等于 width**（不是 `(w+7)/8`）。
真实字节数 = `width * ceil(height/8)`。

源 BMP → 点阵的规则：**颜色 ≠ `bmp_transparent_color`（默认 0x00FFFFFF 白）就点亮**。
`re/verify_res_pixels.py` 拿 104 张原厂 BMP 逐张转换，与 `result.bin` 里的字节
**104/104 完全一致**。

### 10.3 `dwLength` 是个历史 bug，别照着它读

原厂写进去的是 `(w + 7) * ceil(h/8)`，比真实长度多 `7 * 页数`。
明显是把 `((w + 7) / 8) * h` 的括号打错成了 `(w + 7) * (h / 8)`。

- 固件读单色图时按 `offset` 逐行取，**根本不看 dwLength**，所以这个 bug 从没暴雷；
- 条目在文件里是**紧密相接**的，相邻两项 `dwOffset` 之差 = 真实长度，
  最后一项结束正好是文件末尾（实测 0x2289 = 文件大小）；
- 重建版照抄这个值（`res::legacyLength()`），否则和原厂对不上字节。

`.str` 那边的 `dwLength` 是**对的**（= `w * 2`，h 恒为 16）。

### 10.4 调色板 = 固定首色 + 本页用色 + 内置表

```
palette[0]      恒为 0x55AAA5（原厂硬编码）
palette[1..k]   Resbuilder.xml 里本页 <ColorList> 的颜色，按原顺序
palette[k+1..]  内置 255 色表里**去掉已用色**之后的前 255-k 项
```

内置表已从原厂 `ResBuilder.exe` 文件偏移 `0x000E8B64` 处提取（255 项，
BGR0 排列），落在 `src/res/DefaultPalette.h`。三页实测**逐字节复现**。
落盘每项 4 字节：`B, G, R, 0x00`。

`<ColorList>` 本身由 QtToolBin 收集：本页所有 css 的 `background_color`、
`border.color`，加上控件属性里的颜色，去重；**若没有黑色则在末尾补一个 000000**
（页 1 的 json 里一处 000000 都没有，输出里却排在最后，就是补的）。
**收集顺序没能复现**（不是前序、层序、也不是 .sty 的"孩子块序"），
集合是对的。OSD1 单色屏不读调色板，这处差异无害。

### 10.5 字符串位图 = GDI 光栅化

`.str` 里存的不是文字，是**渲染好的点阵**。重建版直接调同一套 Win32 GDI：

```
CreateFontIndirectW(LOGFONT{ lfFaceName="宋体", lfHeight=-16, lfCharSet=134,
                             lfWeight=400, lfQuality=0, lfPitchAndFamily=2 })
1bpp 自上而下 DIB，调色板 index0=白 index1=黑，位全清 0
SetBkMode(OPAQUE); SetBkColor(白); SetTextColor(黑);
TextOutW(hdc, 0, 0, text)
宽度 = GetTextExtentPoint32W(text).cx 向上取整到 8 的倍数，高度 = |lfHeight|
```

三种语言 **141/141 条逐字节一致**（`re/verify_str.py`）。两个坑：

- **不要 trim**。原厂 5 条英文（`"factory setting "` 等）末尾有空格，
  多占 8 px；trim 掉宽度就少一格。
- **不能用 `PatBlt(WHITENESS/BLACKNESS)` 清底**。单色 DIB 下它按物理调色板
  索引填，方向和自定义调色板相反，留白会被填成 1。直接 `memset(bits, 0, …)`。

`<Fonts>` 那 22 项 LOGFONT 是**假的**：`font00` 写着 Cambria/-32、
`font01..05` 宋体/-32，但产出的 `result.str` 三种语言全是 16 px 宋体。
也就是说原厂并没有按语言下标去用那张表。重建版的做法是：默认宋体 -16，
只有当 `<Fonts>` 里对应项的字高**也是 16** 时才采用它，避免莫名其妙换字体。

### 10.6 `resver` 不可复现

`res_ver.h` 里的 `IMAGE_VERION` / `STRING_VERION` 就是两个文件头里的 `resver`。
它**不是内容的散列**：一个只有 16 字节头、什么资源都没有的 `result.bin`，
`resver` 也是 `0xD9BA4727` 这样的非零值。只能是随机数或时间派生。

固件 `res_file_version_compare()` 只做"文件里的值 == 编译期常量"的相等比较，
而这两个常量正是 ResBuilder 自己写进 `res_ver.h` 的 —— 自洽即可。
重建版改成**内容的 CRC32**：确定性、可 diff，同时保住"内容变了版本号就变"的原意。

### 10.7 资源 ID 的分配规则

| | 规则 | 验证 |
|---|---|---|
| 图片 | 每页把引用到的图片按**宏名（文件名去扩展名转大写）ASCII 升序**编号 1..N | `result_pic_index.h` 104/104 |
| 字符串 | 所有页用到的 cell 去重后按名字排序，**全局**编号 1..N | `result.xml` 的 `<Cell id>` 47/47 |
| 空引用 | 写 `0xFFFF` | `str.list=[""]` 那条 |

`result_pic_index.h` / `result_str_index.h` 都是**按页分块**输出的，
跨页共用的名字会重复 `#define`（值相同，编译器不报错）。

### 10.8 多国语言表

输入是 BIFF8 的 `.xls`（OLE 复合文档）。重建版自带一个最小读取器
`src/res/XlsReader.cpp`：CFBF 目录 → `Workbook` 流 → BIFF 记录
（`BOUNDSHEET` / `SST` / `LABELSST` / `LABEL` / `NUMBER`，含 `CONTINUE` 续块）。
不依赖 Excel，也不拖第三方库。

`result.csv` 是这张表的原样导出：**UTF-16LE + BOM**，字段分隔 `",\t"`，
每行末尾也有一个 `",\t"`，行尾 CRLF。重建版输出与原厂**逐字节相同**。

## 2026-09-09 补：加了一个空页面之后才暴露的三处格式错误

用户在编辑器里新建了一个页面（`页面_3`，只有一个空图层）再导出，整链验收从
「不合格 0 项」变成有差异。查下来是**三处一直写错、但以前看不出来**的地方 ——
原来那三页的数据恰好让错误和正确的结果相同。

### 1. `.str` 头部 0x08：是页数，不是语言数

```
RES_HEAD_T { u8 magic[4]; u16 version; u16 bPanelType; u16 totalPage; u16 rsv; u32 resver; }
                                                        ^^^^^^^^^ 0x08
```

固件 `User/ui_framework/liba/res/resfile.c` 里这个字段就叫 `totalPage`，
`.res` 和 `.str` 共用同一个头。语言数在后面 `RES_ENTRY_T` 的 `langsum`
字节里，那个才是 `open_string_pic()` 拿去除 `wCount` 的。

本版 `buildStr()` 在这里写的是语言数。这套工程正好 **3 页 3 语言**，
两种写法数值都是 3。加到 4 页之后：原厂写 4，本版还写 3。

### 2. 调色板：ColorList 之后必须补上透明色

完整规则（四页实测逐字节复现）：

```
palette = [0x55AAA5] + <该页 ColorList> + [bmp_transparent_color 若前面没有]
        + <255 项默认表，去掉已出现的>        共 256 项，每项 4 字节 B G R 00
```

中间那一项以前漏了。页 0~2 的 ColorList 里本来就带 `FFFFFF`（= 本工程的
`bmp_transparent_color`），补不补结果一样；新页面的 ColorList 只有
`000000 / D9EE94 / 8DEEDB`，原厂第 4 项写了 `FFFFFF`，本版直接接默认表 ——
**后面整段错开 4 字节**，`result.bin` 一下多出 449 处对不上。

### 3. `result.xml`：空列表要写自闭合

原厂空的时候写 `<CellList/>`，本版写成 `<CellList>` + `</CellList>` 一对空标签。
以前每一页都有内容，看不出来。图片列表同理（同一个写出器）。

### 修完之后

| 文件 | 差字节 | 说明 |
|---|---|---|
| `project.bin` | 67 | 原厂 `char format[16]` 的未初始化栈内存 + 随之而来的 3 个 CRC |
| `ename.h` | 0 | 逐字节相同 |
| `result.bin` | 91 | resver + ColorList 排序（`verify_palette_order` 判定"无法解释 0 处"）|
| `result.str` | 4 | resver |
| `result.h` / `result.csv` | 0 | 逐字节相同 |
| `result.xml` | 171 | 全部是 ColorList 排序；去掉 `<Color>` 行后**差异 0 行** |

**教训**：只用一个工程当参考，"碰巧相等"会把错误藏起来。这三处都是靠用户
在真实使用中改了工程结构才暴露的 —— 参考工程的形态越单一，验收越容易过，
也越容易过得没意义。

---

## 2026-09-09 补二：拿原厂工具**当场重跑**之后定下来的五件事

前面所有对拍都是拿仓库里存档的原厂产物当参考。这一轮换了个做法：把
`tools/UI工程`（jl701n SDK 那份）整棵树复制到 `C:\bt\facexp`，在副本里用
**原厂 QtToolBin.exe 连跑三遍**（只给目标窗口 PostMessage 发 F5，不抢焦点），
得到三份同机同路径的新鲜产物。这一下把两类噪音同时消掉了：

* 存档产物是在另一台机器上生成的，盘符是 `M:`，我们是 `D:` —— 路径注释的
  几千处文本差异全是这么来的，跟逻辑无关；
* 三次运行互相一比，就能直接分辨"我们错了"和"原厂本来就每次不一样"。

### 10.6 `Resbuilder.dat` —— 字符串表的**逐格字体**

原厂 ResBuilder 是个表格界面，行是 ResID、列是语言，可以选中某一格单独改字体。
这份选择存在工程目录的 `Resbuilder.dat`，**不进** `Resbuilder.xml`
（xml 的 `<Fonts>` 只有 font00..21 那 22 条按语言的默认字体，而且实测原厂
根本没按语言下标去用它）。

```
+0    "1.2\0"
+4    记录数组，每条 132 字节：
        +0    6 字节保留（多数为 0）
        +6    LOGFONTW，92 字节
        +98   u32 行号（1 起，第 1 行是表头，第 2 行才是 xls 首行数据）
        +102  u32 列号（1 起，第 1 列是 ResID 列，第 2 列起是各语言）
        +106  u16 该格是否有内容
        +108  24 字节其余界面状态（列宽、颜色…），与光栅化无关
末尾  6 字节 0
```

jl701n 那份是 98 行 × 23 列 = 2254 条。里面 2226 条是宋体 -16，
**27 条是宋体 -11**，行 47..55 × 列 2/3/6 —— 正好是 m46..m54（日/一/二/三/
四/五/六/开/关）在简体/繁体/英文三列，和 `result.str` 里对不上的那 27 条
逐条吻合。另有 1 条是 Times New Roman -16（行 32 列 6）。

### 10.7 文字光栅化的三条细则

1. **宽度不补齐**：`wWidth = GetTextExtentPoint32().cx`，原样写。
   以前补齐到 8 的倍数，在 -16 宋体上永远看不出来（汉字进 16、ASCII 进 8，
   任何串的 cx 本来就是 8 的倍数）。-11 的那 27 条出现了 11 / 18 / 30 这种
   宽度，才暴露。
2. **高度取到 8 的倍数**：位图是竖向分页存的，`wHeight = ceil8(|lfHeight|)`。
   -11 → 16，-16 → 16。
3. **字比位图矮时竖直居中**：起笔 y = `(wHeight - |lfHeight|) / 2`。
   -11 在 16 行里上留 2 行。原厂那 27 条正是整体下移 2 行。

改完之后 `result.str` 210 条的宽/高/长度和位图数据**全部逐字节相同**，
整个文件只差 4 字节（resver）。

### 10.8 `dwLength` 那个 bug 的括号打在哪

原来写的是 `(w+7) * ((h+7)/8)`，只有 h 不是 8 的倍数时才和原厂分家。
全工程 314 张图里只有 26×26 那 4 张符合，原厂给的是 107 而不是 132：

```
(26+7) * 26 / 8 = 107        ← 原厂
(26+7) * ((26+7)/8) = 132    ← 之前
```

所以真正的写法是 **`(w + 7) * h / 8`**（整除）。存进文件的字节数仍然是
`w * ceil(h/8)`，`dwLength` 只是个错的声明，固件不看它。

### 10.9 图片宏名不做字符净化，编号按自然序

原厂 `result_pic_index.h` 里长这样：

```
#define  J3_LINEART (1)            1       //...\J3_lineart (1).bmp
#define  J3_LINEART (2)            2
```

括号和空格原样留着 —— 28 张图全叫 `J3_LINEART`，互相冲突，是原厂的缺陷。
但这个头文件固件一处都没引用（`grep` 过整棵 `User/`、`tools/JL`），只是给人
看的索引表，所以照抄。

编号规则是**自然序**（数字段按整数比）：`(1) < (2) < ... < (10)`，
字典序会排成 `(1) < (10) < (2)`。证据：`<PictureList>` 在 xml 里的次序每次
运行都不一样，但 `result_pic_index.h` 的编号两次运行完全一致 ——
说明 ResBuilder 拿到 xml 之后自己重排过。

### 10.10 原厂**自己也复现不了**的四样东西

把原厂 QtToolBin 在同一份工程上跑三遍，逐项比：

| 项 | 三次运行 | 结论 |
|---|---|---|
| `<ColorList>` 每页颜色次序 | 三次三样 | Qt5 `QSet` 迭代序，随机哈希种子 |
| `<PictureList>` 每页图片次序 | 三次三样 | 同上 |
| `<CellList>` 每页 ResID 次序 | 三次三样 | 同上 |
| `IMAGE_VERION` / `STRING_VERION` | 每次不同 | 原厂随机数 |

页 5/6 只有 4 个颜色，三次分别是
`D2EE45,000000,D9EE94,8DEEDB` / `D2EE45,000000,8DEEDB,D9EE94` /
`000000,D2EE45,8DEEDB,D9EE94` —— 一目了然。

**影响面**：`<ColorList>` 的次序直接决定 `result.bin` 里调色板的排布，所以
`result.bin` 的调色板段永远对不上（颜色集合一致、顺序不同）。这不是缺陷，
是原厂产物本身就不可复现。图片和 ResID 的编号反而是稳的，因为 ResBuilder
拿到 xml 后会重排。

本版这三处一律用**确定性排序**（颜色按首次出现、图片和 ResID 按自然序），
同一份工程跑多少遍产物都一样 —— 这是比原厂更好的性质，不打算改成随机。

### 10.11 `project.bin` 剩下的差异全是原厂的未初始化内存

`char format[16]`（Time 在 +28、number 在 +24）原厂只 `strcpy` 了
`strlen+1` 个字节，**剩下的十来个字节没清**，里面躺着上一次用过的堆内容：

```
Time 控件 "M"：  +28 = 4d 00 00 00   +32 = 03 00 00 00   +36/+40 = 两个堆指针
Time 控件 "Y/M/D"：+28 = 59 2f 4d 2f  +32 = 44 00 00 00   ← 字符串盖掉了那个 3
```

两次原厂运行一比：指针部分每次都变，`03 00 00 00` 每次都一样 —— 是同一块
残留，不是什么字段。本版把整个 `format[16]` 清零，比原厂正确。

还有一处：某个**没有子控件**的 `NewLayout`（type=3），原厂在子指针
（+20）里写了和 +12（下一个兄弟）相同的值，本版写 0。固件 `layer.c` 是
`if (ctrl_num) { ... }` 先判个数再解引用，`ctrl_num == 0` 时这个指针根本不会
被用到，写 0 是安全的。

### 这一轮之后的对拍结果（同机同路径，原厂当场生成的参考）

jl701n SDK 那份 oled 工程（11 页 / 314 张图 / 210 条字符串）：

| 文件 | 结果 |
|---|---|
| `result.h` / `result.csv` / `ename.h` | 逐字节相同 |
| `result_pic_index.h` | 只差生成时间戳那一行 |
| `result_str_index.h` | 差时间戳 + 2 行位置（原厂 CellList 随机序） |
| `result.bin` | 314 张图的元信息和像素**全部相同**；只差 resver 和调色板排序 |
| `result.str` | 210 条**全部相同**；只差 4 字节 resver |
| `result.xml` | 只差 `<Color>` 的排序 |
| `project.bin` | 202 字节：时间戳 2 + `format[16]` 未初始化尾巴 186 + 页表 CRC 14 |

用户那份 TFT 工程（4 页 / 105 张图 / 141 条字符串）同样处理，
`result.xml` 逐字节相同，`project.bin` 只差 63 字节（同上三类）。

