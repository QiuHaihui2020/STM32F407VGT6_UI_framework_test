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
