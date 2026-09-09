# QtToolBin / ResBuilder 重建依据与验收

整链三个工具都已重写并跑通：

| 原厂 | 重建版 | 产物与原厂的差距 |
|---|---|---|
| `ui-tools.exe`   | `UITools.exe`    | 工程 json 读写**逐字节相同** |
| `QtToolBin.exe`  | `QtToolBin.exe`  | `project.bin` 24498 B 里差 **61 B**，全是原厂未初始化内存 |
| `ResBuilder.exe` | `ResBuilder.exe` | `result.str` 差 **4 B**（resver）；`result.bin` 差 4 B + 调色板顺序 |

一条命令复跑整个验收：

```
python re/verify_toolchain.py <重建版 bin 目录> <工程目录> [输出目录]
```

本机实测：

```
文件                       本版       原厂       差字节      判定
project.bin              24498    24498    61       允许范围内
ename.h                  8564     8564     0        逐字节相同
result.bin               8841     8841     79       允许范围内
result.str               17472    17472    4        允许范围内
result.h                 1201     1201     0        逐字节相同
res_ver.h                182      182      21       允许范围内
result_pic_index.h       19564    19564    5        允许范围内
result_str_index.h       2453     2453     5        允许范围内
result.csv               19360    19360    0        逐字节相同
result.xml               59318    59318    146      允许范围内

不合格 0 项
```

**那些"允许范围内"分别是什么**——都不是能修的 bug，是原厂本身不可复现：

| 差异 | 字节 | 原因 |
|---|---|---|
| `project.bin` | 61 | 8 个 Time/number 控件的 `char format[16]` 尾部是原厂没清零的堆内存，各控件互不相同（见 FILE_FORMATS 9.4）。原厂自己重跑也会变 |
| `result.bin` / `result.str` | 各 4 | 文件头的 `resver`。原厂是随机值——空资源文件里也非零。重建版改成内容 CRC32 |
| `result.bin` / `result.xml` | 75 / 146 | 调色板前缀的**排列顺序**。集合一致，顺序没能复现（不是任何一种树遍历）。OSD1 单色屏不读调色板 |
| `res_ver.h` / `result_*_index.h` | 21 / 5 / 5 | 生成时间戳 |

`--ename <原厂 ename.h>` 用来复用原厂分配的 id，只在做上面这种逐字节对拍时加；
正常使用不要加，重建版会自己分配（见第 3 节）。

---

## 1. 工具链分工（已确证）

在各 exe 里搜关键字符串得到：

| 字符串 | ui-tools | QtToolBin | ResBuilder |
|---|:--:|:--:|:--:|
| `uitoolbin` | – | ✔ | – |
| `ename.h` | – | ✔ | – |
| `project.bin` | – | ✔ | – |
| `Resbuilder.xml` | – | ✔ | – |
| `UI_VERSION` | – | ✔ | – |
| `debug.txt` | – | ✔ | – |
| `autosave.json` | ✔ | – | – |

`QtToolBin` 还引用了 `ResBuilder.exe` 字符串 —— 它会去调 ResBuilder。

**QtToolBin 的界面**（`UIToolBin工具(V1.10.1)`）：

```
运行:      [ 生成资源文件(F5) ]
进度条:    [                    ] 0%
JSON文件:  [ SmallColorTFT.json ]        工程ID: [0]
资源设置:  [ 功能设置 ]
资源文件:  ☐ 不重新生成资源文件
调用脚本:  [ copy_file.bat ]
芯片平台:  [ 全平台 ▼ ]
旋转:      [ 0 ▼ ]
版本配置:  [ ]
```

要点：
- 它**直接吃工程 json**（不是 uitoolbin.bin），`工程ID` 就是 id 里的 `pj_id`。
- `旋转` 直接进 `.sty` 头的 `rotate` 字段。
- 生成完会调用 `调用脚本` 里那个 bat（默认 `copy_file.bat`）。
- 配置存在 `option.ini` / `project.ini`。

---

## 2. 控件类型码：完全来自 option.ini（已验证 273/273）

`UITools/config/ini/option.ini` 的 `[Control]` 段就是权威表：

```ini
[Control]
window=1  project=1
ScenesScreen=2  page=2
NewLayout=3
NewLayer=4
NewGrid=5  VerticalList=5  HorizontalList=5
Button=7   ImageList=8   Battery=9   Time=10  Camera=11
Text=12    animation=13  player=14   number=15
窗口=1 工程=1 页面=2 布局=3 图层=4 表格控件=5 垂直列表=5 水平列表=5
按钮=7 图片=8 电池电量=9 时间=10 文字=12 动画=13 播放器=14 数字=15
progressbar=20  progressbar_highlight=21
multiprogressbar=22  multiprogressbar_highlight=23
watch=24 watch_hour=25 watch_min=26 watch_sec=27
slider=28 right_pic=29 left_pic=30 slider_pic=31 slider_text=32
vslider=33 vslider_right_pic=34 vslider_left_pic=35 vslider_pic=36 vslider_text=37
[Picture]
png=233
```

**查表键的顺序是 `caption` 优先，再退回 `-type`。**
这一点很关键：`control/ex/` 里的扩展控件（slider / vslider 及其零件）的 `-type`
都是 `NewLayout` / `ImageList` / `Text`，真正决定类型码的是 `caption`。
option.ini 里同时给了中英两套键就是为这个。

验证脚本 `re/verify_types.py`，在真实工程上的结果：

```
类型码对拍: 一致 273 / 不一致 0
页号对拍:   不一致 0 / 共 273
```

---

## 3. 控件 ID 的组装（已从二进制反出原函数）

`QtToolBin.exe` 里 `0x0042ABC0` 就是 id 组装函数，反汇编逐字如下：

```asm
0042ABC9  cmp dword ptr [ebp+8],  7      ; arg0 pj_id  ≤ 7      否则报错
0042ABD3  cmp dword ptr [ebp+0xC], 0x7f  ; arg1 page   ≤ 0x7f
0042ABDD  cmp dword ptr [ebp+0x10],0x3f  ; arg2 type   ≤ 0x3f
0042ABE3  mov eax, [ebp+0x10]            ; type
0042ABE9  shl eax, 0x10                  ; type  << 16
0042ABEC  or  eax, [ebp+0x14]            ; | arg3 —— 低 16 位
0042ABEF  shl edx, 0x16                  ; page  << 22
0042ABF2  or  eax, edx
0042ABFB  shl edx, 0x1d                  ; pj_id << 29
0042ABFF  or  eax, edx
0042AC03  ret 0x10
```

即：

```
id = (pj_id << 29) | (page << 22) | (type << 16) | low16
```

调用点 `0x0042CF59`：`pj_id` 取自 `this+0x38`（界面上的"工程ID"），
`page`/`type` 是形参，`low16` 来自 `0x0042B3B0(page, "ename", node)` 的返回值。

### 低 16 位：查表得来，且**离线不可复现**

`0x0042B3B0` 不是哈希函数，它按属性名（`"color"` / `"cell"` / `"ename"` …）
在一张 **per-page 的 `QMap<QString,int>`**（成员 `this+0x20` 的 QList 里按页取）
中查找并返回 `[node+0x10]`。也就是说低 16 位是**先分配好、再查出来**的。

穷尽排除的结果（`re/hash_*.py`，281 条样本全部 0 命中）：

- djb2 / sdbm / BKDR(31,131,1313,65599,37,17,5381,1000003) / FNV-1a / FNV-1 /
  ELF / murmur2 / adler32 / **CRC32**，× 7 种输入编码（ASCII、补 NUL、小写、
  UTF-16LE/BE、小写 UTF-16LE）× 8 种输出折叠（低16/高16/异或折叠/移位/取模…）
- CRC16 的 9 种标准变体，以及 **全 65536 个多项式 × 3 种 init × 2 种移位方向 ×
  6 种输入变体**
- 输入换成中文 `-name`（UTF-8 / GBK）、`caption`、`-type`
- 也确认了 ID 不在工程 json、uitoolbin.bin、Resbuilder.dat、result.* 等任何中间文件里

已知规律：

| 项 | 观察 |
|---|---|
| 页节点 | `PAGE_0/1/2` 的低 16 位 = **0/1/2**（就是页序号，不是哈希） |
| 控件 | 277 个控件的低 16 位在 0..65535 上均匀分布，且**两两不重复** |
| 唯一性 | 全 24 位 id 281/281 唯一；唯一一处低 16 位相同是 `PAGE_0` 与 `UI_ROTATE`（都是 0，特殊项） |

最合理的解释是 **Qt 的 `qHash` 带随机种子**（Qt 默认开启哈希随机化），
或"随机分配 + 查重"。两者都意味着**同一份工程每次生成的 id 都不一样**。

**这不阻塞替换。** 因为：

1. `.sty` 与 `ename.h` 是**同一次生成一起产出**的，两者内部自洽即可；
2. `Resbuilder.xml` / `result.*` 里**不含**这些 id（已扫过，0 命中）；
3. 固件只用 `ename.h` 里的**宏名**（`ui_style.h` 把 `PAGE_2` 映射成
   `ID_WINDOW_MAIN` 之类），`copy_file.bat` 每次都会把新的 `ename.h` 拷进去。

所以重写版可以用**自己的确定性低 16 位**（例如 `CRC16(ename)` + 冲突时线性探测），
产出的 `.sty` + `ename.h` 完全可用，只是不会和原厂**逐字节**相同——
而原厂那份本来就不可复现。

---

## 4. ename.h 的确切格式（已确证）

字节级确认（真实文件 + 二进制里的字符串常量都对得上）：

```
#ifndef UI_TOOL_ENAME\n
#define UI_TOOL_ENAME\n
\n
#define <宏名> 0X<大写十六进制，无前导零>\n     ← 按宏名 ASCII 序，QMap 天然有序
...
\n
#endif //UI_TOOL_ENAME_H\n
```

纯 LF、UTF-8、`0X` 前缀大写。二进制里对应的字符串片段是
`"#ifndef UI_TOOL_ENAME\n"` / `"#define UI_TOOL_ENAME\n"` / `"#define "` /
`" "` / `"0X"` / `"\n"` / `"#endif //UI_TOOL_ENAME_H\n"`，
写出代码在 `0x00434D10`（`QTextStream` + `setCodec("utf-8")`）。

---

## 5. .sty 的写出：容器层已完全打通

见 [`FILE_FORMATS.md`](FILE_FORMATS.md) 第 3、6 节。已验证：

- 文件头 24 B、页表 20 B/页、控件头 16 B、各控件负载与固件结构体逐字节吻合
- **指针是页内相对**：固件按 `页基址 + (ptr & 0xFFFF)` 取。
  注意落盘的指针**高位是 0**（就是纯偏移）；`>>22` 取页号、`>>29` 取 pj_id
  那套只对**控件 id** 有效，别把它套到指针字段上
- css 记录 36 B（`element_css1`），从高地址往低地址分配
- 页尾 u16 表是**重定位表**：登记控件区里所有指针字段的页内偏移，
  逐类型偏移已列全（Layout +12/+16/+20，Text +12/+40/+44，ImageList +12/+24/+28/+32 …）
- 拆开重拼 24498 字节逐字节一致（`re/sty_roundtrip.py`）

**后续又打通的（见 FILE_FORMATS.md 第 8 节）**：

- image list / text list / `element_event_action` 三种数据块的内部布局 —— 数据区
  **100% 字节覆盖，0 字节未解释**
- 数据区的**排布算法**（组间倒序、组内按字段偏移升序、4 字节对齐 0xFF 填充、
  空列表也占位）—— `re/sty_gen.py` 丢掉全部偏移后重新排布，**逐字节相同**
- 重定位表的生成规则 —— 3/3 页重建一致
- 页表里三个 CRC 的定义（`crc_head` / `crc_data` / `crc_table`，均为 CRC16-XMODEM）
  —— 9/9 验证通过，生成器已自算

**已全部补齐**：窗口记录 28 字节的字段含义（`re/verify_window.py` 18/18）、
css 字段映射与万分比换算（`re/verify_css.py` 273/273）、控件负载字段
（`re/verify_payload.py`）、图片/字符串资源号（`re/verify_resids.py` 511/511）、
记录排列顺序（`re/verify_order.py` 3/3 页）。生成器 `src/sty/StyBuilder.cpp`
直接吃工程 json 产出 `project.bin`，与原厂只差那 61 字节未初始化内存。

---

## 6. ResBuilder 侧（已完成）

- 输入 `Resbuilder.xml`（QtToolBin 生成）+ `config/pic_lcd/*.bmp` + 多国语言 `.xls`
- 输出 `result.bin`(=JL.res) / `result.str`(=JL.str) / `result.h` / `res_ver.h` /
  `result.csv` / `result.xml` / `result_pic_index.h` / `result_str_index.h`
- 完整格式规范见 [`FILE_FORMATS.md` 第 10 节](FILE_FORMATS.md)，要点：
  - 像素是 **OSD1 单色竖向分页**（`byte[(y/8)*w+x]` 的第 `y%8` 位），
    转换规则"颜色 != 透明色就点亮"，104/104 张图逐字节复现；
  - 字符串位图是 **GDI 光栅化**的（宋体 -16、1bpp DIB、`TextOutW(0,0)`），
    141/141 条逐字节复现；
  - `RES_BMP_T.dwLength` 原厂写的是**错值**（`(w+7)*页数`），照抄即可；
  - 调色板 = 固定首色 + 本页用色 + 从 `ResBuilder.exe` 提取的内置 255 色表。
- 本工程 `image_compress_method` / `string_compress_method` 都是 `none`，
  **压缩路径没有样本**，因此重建版只实现了不压缩。固件里 RLE/QuickLZ 的
  解码器（`liba/res/rle.c`、`quicklz.c`）还在，真要用再照着反写。

---

## 7. 生成器的实现要点（写代码时踩的坑）

按重要性排列，每条都是实测出来的，不是推断：

1. **控件记录的排列顺序**：不是前序也不是层序，而是"**孩子块序**"——
   先把一个节点的孩子**连续**写完，再依次递归每个孩子。父节点的
   `ctrl` / `layout` / `info` 指针指向自己的第一个孩子，`ctrl_num` 是孩子数。
   `re/verify_order.py` 三页全中。

2. **落盘的指针是纯页内偏移**，高位不带页号/工程号。固件读的时候用
   `window_head.offset` 当基址；`>>22` / `>>29` 那套只对**控件 id** 有效。
   （页 1、页 2 的指针高位实测全 0。）

3. **万分比是向上取整**。`95*10000/128 = 7421.875 -> 7422`。
   注意 C++ 整数除法向零截断，不能照抄 Python 的 `-((-a)//b)`，
   要写成 `(num + ref - 1) / ref`。

4. **窗口记录 28 字节里的 `len` 字段写 0**，不是记录长度。

5. **结构体对齐空洞填 `0xFF`**：`type 5` 的 18..19、`type 8` 的 17 和 22..23、
   `type 33` 的 17..19、`type 4`/`type 10` 的 `rev[3]`。

6. **每个带 action 字段的控件都有一个 action 块**，哪怕一条事件都没配
   （就是个 `u16 num = 0`，对齐到 4 字节），并且**指针要回填**、
   重定位表里也要有它。三页各多 49/96/132 项就是这么来的。

7. **空列表的指针，图片和文字不一样**：
   - 图片列表（`type 8`/`9`）为空 -> 块照写 `0x0000`，但**指针留 null**；
   - 文字列表（`type 12` 的 `strlist`）为空 -> **照样指过去**（`num=0`）。

   这是把 273 个控件全统计了一遍的结果，不是猜的。

8. **同一个字段在不同控件模板里名字不一样**：`type 5` 的 page_mode，
   表格叫 `page_mode`，垂直/水平列表叫 `scroll_mode`（枚举都是 SCROLL=0 / PAGE=1）。

9. **`option.ini` 是 UTF-8 带 BOM**，但 `Resbuilder.xml` 里的中文路径是
   **本地代码页**（尽管头上写着 `encoding='UTF-8'`），而且 XML 版本号写的是
   `version='2.0'`（世上没有 XML 2.0，`QXmlStreamReader` 会直接报错）。
   两处都得兼容。

10. **`prop_len` = `24 + 20*页数 - 16`**（3 页 = 68）。固件不读它，但对拍要一致。

## 8. 还没做的

| 项 | 情况 |
|---|---|
| `action` 块的**内容** | 结构已知（固件 `struct event_action`：`u16 event; u16 action; int id; u8 argc; char argv[]`，对齐 4），生成器也照着写了，但本工程 277 个控件全是 `num=0`，**没有真实样本可对拍** |
| 调色板前缀顺序 | 集合已复现，顺序没有。单色屏无影响 |
| `debug.txt` 排版 | 内容对（同一份 .sty 的十六进制转储 + 页表摘要），空白/日期格式与原厂略有出入。它只是调试辅助文件 |

---

## 「功能设置」（原厂窗口标题：配置界面）

版式照原厂那一页复刻，见 `temp/功能设置.jpg`。这一页的每一项最终都落进
`Resbuilder.xml`，也就是 ResBuilder.exe 的唯一输入。

### 验过的（有原厂产物做依据）

| 结论 | 依据 |
|---|---|
| 字段名/格式：`language` 是 8 位小写十六进制、`bmp_transparent_color` 是大写 | 原厂 `UITools/Resbuilder.xml`（240x240 twsbox 那份遗留文件） |
| 「选中: N」= `<language>` 掩码，选中的行就是置位的位 | 两个独立来源：截图里 `选中: 13` 且 1/2/5 行高亮（0x13 = bit0/1/4）；原厂 xml 里 `0x00000007` 对应简中+繁中+日语 |
| 「字号」是磅值，`lfHeight = -(字号 x 4 / 3)` | 截图前 6 行 24、第 7 行 12；原厂 xml font00–05 是 -32、font06 起 -16 |
| 「资源文件名」带出 `res` / `resfilename` / `headerfilename` 三项 | 原厂 xml：`result` / `result.bin` / `result.h` |
| 默认值改成原厂那张字体表后产物不变 | `result.str` 与改动前**逐字节相同**（0 差异），整链验收其余项判定不变 |

### 没验过的（是推的，不是在原厂工具上试出来的）

| 项 | 现在的做法 | 风险 |
|---|---|---|
| 上移/下移/增加/删除 之后表怎么变 | 生成时**固定写 22 条** font00..21，行数不够用内置默认补齐，多出的忽略 | 第 i 行 = font{i} = 掩码第 i 位 = xls 第 1+i 列，挪动顺序会整体改变这个对应关系。界面上四个按钮都挂了提示说明 |
| 「其他配置: 旋转」勾选框管什么 | 只存不用（实际角度在主界面那个下拉框） | 勾了不生效 |
| 压缩方式除 `none` 外的取值名 | 列了 `rle` / `quicklz`，取自逆向笔记 | 本版 ResBuilder 只实现 none，选了也不压；下拉框上有提示 |
| 设置存哪 | `<工具目录>/config/ini/resbuilder.ini` | 原厂存在 `Resbuilder.dat`（941KB，`"1.2"` 开头 + 一串 LOGFONT 记录），格式没逆向过，**两边设置不互通** |

要把这几项坐实，得跑原厂 `UITools/QtToolBin.exe`，在它的界面上逐项改、每改一次
看 `Resbuilder.xml` 怎么变。那是交互操作，脚本代劳不了。

### 拿原厂工具做的对照实验（2026-09-09）

光看截图复刻界面是不够的。这一轮用**原厂 ResBuilder.exe** 做了受控实验，
把"差异到底出在谁身上"钉死了。原厂 QtToolBin 是纯弹窗工具（`-h` 直接退出、
无输出），驱动不了；但 ResBuilder 是命令行的，可以拿来当裁判。

实验都在 `C:\bt` 的工程副本里做，原厂目录只读。

| 实验 | 结果 |
|---|---|
| 原厂 ResBuilder + **本版** Resbuilder.xml | result.str / result.h / result.csv **逐字节相同**；result.bin 差 79 字节 |
| 原厂 ResBuilder + **原厂** Resbuilder.xml（对照组） | result.bin **0 差异** |

同一个 ResBuilder，喂原厂 xml 得 0、喂本版 xml 得 79 —— 差异 **100% 来自
本版生成的 Resbuilder.xml**，与 ResBuilder 的实现无关。

再定位那 79 字节：4 个在 0xC..0xF（resver，原厂就是随机值），
75 个从 0x374 起 —— 全在**各页调色板**那一段。也就是说：

**除了 resver 和 ColorList 的排列顺序，本版产物与原厂逐字节相同。**
（OSD1 单色屏不用调色板，所以这条对本项目没有功能影响；但要做到 100%
字节替换，得把 ColorList 的排序规律挖出来。）

#### 这一轮改对的四处（都是对照原厂产物查出来的）

| 问题 | 原来 | 原厂 | 怎么发现的 |
|---|---|---|---|
| 编码 | `toLocal8Bit()`（本机 GBK） | **UTF-8** | 原厂 `UITools/Resbuilder.xml` 里"多"是 `E5 A4 9A`；本机 ACP 是 936 |
| 缩进 | 每级 1 个 Tab | 每级 **8 个空格** | 原始字节比对 |
| 元素顺序 | endian/paneltype/picture_path/excel_path 排在 PageList **之后** | 排在**之前** | 逐行 diff |
| excel_path | 绝对路径 | **相对** Resbuilder.xml 所在目录 | 逐行 diff |

改完之后 `--verify-resxml` 报「配置项不一致 0 处」：LanguageList、Fonts、
全部 14 个设置项与原厂逐字节一致。

#### ColorList 排序：还没破解

页 0 的颜色集合两边一样（11 个），顺序不同：

```
原厂: 0000FF D2EE45 FF557F 368FEE FF0000 AAA555 D9EE94 FFFFFF 000000 FFAA7F 55AA00
本版: 0000FF 55AA00 FF557F FF0000 D9EE94 FFAA7F FFFFFF D2EE45 368FEE AAA555 000000
```

线索：两边都以 `0000FF` 开头、第 3 位都是 `FF557F`；原厂的 `000000` 落在第 9 位
（**不是**补在末尾，本版是"缺了就 append"）。本版的收集顺序是"逐节点：先 css
各状态的 background_color / border color，再 property 里的文字色/高亮色"。

要继续挖，用这个回路最快（一轮不到一分钟）：
1. 改 `StyBuilder.cpp` 里 `pageColors` 的收集顺序
2. 生成到工程副本：`QtToolBin --cli --no-script <json> -o <副本> --excel <abs> --ename <ename.h>`
3. 用**原厂** ResBuilder 吃它，比 result.bin，目标 0 差异

### ColorList 排序：挖到头了，结论是「查不出规律，且无功能影响」

那 79 字节差异 = 4 字节 resver + 75 字节调色板。这一轮把调色板这条线挖到底。

#### 先把事实钉死

* `result.bin` 偏移 **0x370** 起就是调色板，每项 **4 字节：B G R + 1 字节填充**。
  例：`45 ee d2 00` = `D2EE45`。
* 用**原厂 ResBuilder** 分别吃原厂 xml 和本版 xml，解出来的调色板
  **与各自 xml 里 ColorList 逐项一一对应** —— ResBuilder **不重排**，原样照抄。
* 两边调色板的**集合完全相同**，仅顺序不同（`原厂独有: []  本版独有: []`）。

所以：**这 75 字节差异 100% 来自本版 ColorList 的排序，别无其它来源。**

#### 试过并否掉的假设（页0/1/2 三页都不满足）

| 假设 | 结果 |
|---|---|
| 文档序（先序遍历），节点内 css 先 / property 先 | 否 |
| 全局分组：所有 bg → 所有 border → 所有 property（及反序） | 否 |
| BFS 层序 / 逆文档序 | 否 |
| 值排序：RGB 升降、RGB565 升降、565 字节交换、BGR、灰度、字符串序 | 否 |
| 单一全局顺序（每页取子集）| 否。页0 与页1 的共有色次序一致，但页2 与两者都不一致 |
| Qt `QHash<QString>` 桶序：seed 暴搜 0..2²⁰ × Qt 表大小序列 × 头插/尾插 | 否。只有"桶数 1024 装 8 色"这种人人独占一桶的过拟合解，换页即失效 |

反证也很硬：页1 的第 1 项是 `005500`，它首次出现在**文档序第 71 个节点**
（一个 vslider）；而图层自身的背景 `8DEEDB`（第 1 个节点）却排在第 4 位。
任何"按遍历先后收集"的方案都解释不了。

第二样本也没有：工具目录里那份 twsbox 的 `Resbuilder.xml` 是**空工程**
（`<PageList/>`，0 个 Color），无法用来判断确定性。

最可能的解释是原厂用了某种 Qt 关联容器（`QSet`/`QHash`）收集颜色、直接按容器序
输出，而 Qt5 的 `qHash(QString)` 默认带**进程级随机种子** —— 那样的话原厂自己
重跑一次顺序也会变，根本不存在可复刻的"正确顺序"。这和已知的
"控件 id 低 16 位是 per-page QMap 查表、每次生成都不同"是同一类现象。

#### 为什么可以就此打住

本工程 `Resbuilder.xml` 里 **104 张图片全是 `fmt="OSD1"`**，即 1bpp 位图 ——
每个像素只有"点亮/不亮"两种状态，**像素里不存调色板下标**。调色板对 OSD1
资源是一段没人按下标引用的数据，顺序变了不影响任何显示结果。

（若将来上彩屏、出现 `fmt="RGB565"` 之类按调色板取色的图，这条要重新评估。）

#### 要继续的话，唯一有意义的下一步

用**原厂 QtToolBin** 对同一个工程连跑两次，比两次的 ColorList：

* 两次**不同** → 坐实是随机序，本项到此为止，改成"记录已知差异"即可；
* 两次**相同** → 说明有确定性算法，再拿这两份样本继续找规律。

原厂 QtToolBin 是纯弹窗工具（`-h` 直接退出、无命令行），这一步得手工点两次
「生成资源文件」，脚本代劳不了。

#### 结案（2026-09-09，用原厂工具连跑两次实测）

用**原厂 QtToolBin** 对同一工程连点两次「生成资源文件」，结果：

| 对比 | 结果 |
|---|---|
| 同一次进程内的两次生成 | `Resbuilder.xml` **逐字节完全相同** |
| 与前一天那份原厂参考（另一次进程） | **三页顺序全变**，但集合一模一样 |

```
页0 上次: 0000FF D2EE45 FF557F 368FEE FF0000 AAA555 D9EE94 FFFFFF 000000 FFAA7F 55AA00
页0 这次: FFAA7F 000000 FF0000 368FEE AAA555 D9EE94 D2EE45 0000FF FF557F FFFFFF 55AA00
```

这正是 Qt `QHash` + 随机 hash 种子的特征：**同进程内种子固定**（所以连跑两次一致），
**换进程重新随机**（所以隔天跑就全变）。

**结论：不存在"原厂的正确 ColorList 顺序"——原厂自己每次启动都不一样。**
本项永久结案，不再尝试复刻。和"控件 id 低 16 位每次生成都不同"是同一类现象。

守住真正重要的那条线即可：`re/verify_palette_order.py` 检查**集合**一致
（少一个颜色 = 某个控件的背景/边框色没收进去，那是真 bug），顺序差异记为已知。

附带收获：这次生成把工程目录里那份被 oled 覆盖过的参考产物（09-08 16:19，
project.bin 45248 字节）刷新成了 TFT 的正确版本，整链验收的那 1 项假失败随之消失。

#### 同一个成因的第二处：`<fontNN>` 的**属性次序**

2026-09-09 再次生成参考之后，`--verify-resxml` 报了「Fonts 不一致（原厂 5816
字符 / 本版 5816）」。逐行看下去，**每个属性的值都一样，只是排列不同**：

```
原厂: <font00 lfOrientation lfClipPrecision lfWidth lfHeight lfFaceName lfCharSet
             lfUnderline lfOutPrecision lfItalic lfWeight lfQuality lfStrikeOut
             lfPitchAndFamily lfEscapement />
本版: <font00 lfQuality lfPitchAndFamily lfOrientation lfHeight lfWeight lfUnderline
             lfOutPrecision lfItalic lfClipPrecision lfFaceName lfCharSet
             lfEscapement lfStrikeOut lfWidth />
```

本版这个次序是当初照着**某一次**原厂产物抄下来硬编码的。现在参考换成了另一次
进程的产物，次序就对不上了 —— 和 ColorList 一样，原厂把 LOGFONT 的字段塞在
Qt 关联容器里，输出时按容器序写，每个进程一个样。

XML 属性本来就是无序的，ResBuilder 解出来完全相同。所以 `--verify-resxml` 改成
**把属性名排序后再比**（`src/qttoolbin/main.cpp` 里的 `canonSection`），
这样比的是"配置内容一不一致"，而不是"这次原厂碰巧怎么排"。改完 0 处不一致。
