# 点阵屏 UI 工具链

一套点阵屏 UI 的编辑器 + 资源生成工具，三个 exe：

| 工具 | 干什么 |
|---|---|
| `UITools.exe`    | 布局编辑器，读写工程 json |
| `QtToolBin.exe`  | 工程 json → `project.bin`(=JL.sty) + `ename.h` + `Resbuilder.xml` + `debug.txt` |
| `ResBuilder.exe` | `Resbuilder.xml` + bmp + xls → `result.bin`(=JL.res) / `result.str`(=JL.str) + 5 个头文件 |

**设计目标是互操作**：读写既有的工程文件(`.json`)、产出固件能直接吃的资源
文件(`.sty` / `.res` / `.str`)。已有工程不用转换，已有固件不用改，
资源换过去就能用。

| | |
|---|---|
| 文件格式说明（工程 json / `.sty` / `.res` / `.str`） | [`docs/FILE_FORMATS.md`](docs/FILE_FORMATS.md) |
| 资源生成链的实现与验收 | [`docs/QTTOOLBIN.md`](docs/QTTOOLBIN.md) |
| 界面与编辑行为说明 | [`docs/UI_BEHAVIOR.md`](docs/UI_BEHAVIOR.md) |
| 兼容性校验脚本（全部可复跑） | [`compat/`](compat/) |

---

## 一条命令跑完整链校验

```
python compat/verify_toolchain.py <bin 目录> <工程目录> [输出目录]
```

它会从工程 json 重新生成全部产物，再和工程目录里现成的那份逐字节对比。
本机实测：

```
文件                       本次     参考     差字节    判定
project.bin              24498    24498    61       差异都可解释
ename.h                  8564     8564     0        逐字节相同
result.bin               8841     8841     79       差异都可解释
result.str               17472    17472    4        差异都可解释
result.h                 1201     1201     0        逐字节相同
res_ver.h                182      182      21       差异都可解释
result_pic_index.h       19564    19564    5        差异都可解释
result_str_index.h       2453     2453     5        差异都可解释
result.csv               19360    19360    0        逐字节相同
result.xml               59318    59318    146      差异都可解释

不合格 0 项
```

判定看的是**差在哪儿**，不是差多少 —— 每一处差异都必须落在下面这些
本来就不可复现的位置上，否则一律报"有讲不通的差异"：

- `project.bin` 的 61 B —— 8 个 Time/number 控件 `char format[16]` 的尾部。
  参考文件里那一段是没清零的堆内存，每次生成都不一样；这里整片写 0。
- `result.bin` / `result.str` 各 4 B —— 文件头的 `resver`。参考文件里是随机值
  （一个只有 16 字节头、什么资源都没有的 `result.bin`，`resver` 也非零）。
  这里改成内容的 CRC32：确定性、可 diff，"内容变了版本号就变"的原意还在。
- 调色板前缀的**排列顺序**（集合一致）。OSD1 单色屏根本不读调色板。
- 生成时间戳。

---

## 第二类验收：自主生成的产物自洽吗

上面那张表是**借了现成的 id**（`--ename`）跑出来的，证明的是
"同样的输入我算出同样的字节"。实际用的时候不会借 —— id 自己分配，
`.sty` 里的 id 和 `ename.h` 里的宏值都是自己算的，两边必须严丝合缝。
这一项由另一个脚本管：

```
python compat/verify_selfconsistent.py <产物目录> <固件 include/common 目录>
```

它只看产物自身，对着固件的加载路径逐条查。本机实测（**不带 `--ename`** 生成）：

```
1) 版本号自洽        .sty 头 0xF0D30FD3 == ename.h 的 UI_VERSION
2) 页表 CRC          3 页 crc_data / crc_table / crc_head 全对
3) id <-> ename      277 个控件 id 全部有宏；280 个宏无重号
4) PAGE_n            PAGE_0/1/2 = 0x020000 / 0x420001 / 0x820002
5) id 位域自洽        type 段 == 控件头 type，page 段 == 所在页
6) 指针与重定位表     3 页指针全部落在本页数据区内，且全部登记进重定位表
7) 资源号范围        图片 <= 本页图片数 {0:40, 1:54, 2:10}；文字 <= 47
8) .res / .str       104 + 141 条 head_crc 全对
9) 固件需要的宏       ui_style.h 硬依赖的 3 个 PAGE_* 都有（PAGE_11/12 是 #ifdef 可选）

通过 21 项，失败 0 项
```

另外两条实测结论：

- **产物是确定性的**。同一份工程连跑两遍，`project.bin` / `ename.h` /
  `result.bin` / `result.str` 的 SHA256 完全相同（只有两个 `.h` 里的生成时间戳变）。
  id 用 `CRC16-XMODEM(宏名)` + 线性探测算出来，只有增删/改名控件时才会变。
- **宏名集合与既有 `ename.h` 完全相同**（282 个，一个不差，只有值不同），
  固件源码里实际引用到的 46 个宏一个不缺。所以 `style_jl02.h` 换过去照样编。

### 顺带查清的一件事：固件里那三个版本校验是死代码

```
ui_style_file_version_compare()   ui.h 声明了、resources_manager.c 实现了 —— 没人调
res_file_version_compare()        resfile.h 声明了、resfile.c 实现了     —— 没人调
str_file_version_compare()        同上                                   —— 没人调
```

而且这份固件树里**根本没有 `res_ver.h`**。所以 `resver`（`IMAGE_VERION` /
`STRING_VERION`）对不上不会有任何后果 —— 这也是"把 resver 改成内容 CRC32"
这个决定完全安全的原因。`UI_VERSION` 同理，但仍然让 `.sty` 头和 `ename.h`
保持一致，万一哪天那个 ASSERT 被启用也不会翻车。

---

## 三个工具怎么分工

| 工具 | 职责 |
|---|---|
| **`UITools.exe`** | **纯布局编辑器**，只读写 `<工程>.json` 和 `autosave.json` |
| `QtToolBin.exe` | 工程 json → `project.bin`(=JL.sty) + `ename.h` + `debug.txt` + `Resbuilder.xml` + `UI_VERSION` |
| `ResBuilder.exe` | `Resbuilder.xml` + bmp → `result.bin`(=JL.res) / `result.str`(=JL.str) |

编辑器不产资源、也不调下游工具，它的兼容性门槛就一条：
**工程文件读写要和既有格式完全一致**。

后两个是资源生成链：`QtToolBin` 吃工程 json，吐 `.sty` 和 `Resbuilder.xml`，
再调 `ResBuilder` 把图片和多国语言表打成 `.res` / `.str`，
最后跑 `copy_file.bat` 拷进固件工程。
`QtToolBin --run-resbuilder <ResBuilder.exe>` 一步跑完。

## 编辑器的验收（本机实测，全部 PASS）

```
[PASS] 工程 json 往返   SmallColorTFT.json    4576146 字节逐字节相同
[PASS] 工程 json 往返   SmallColor_oled.json  8120567 字节逐字节相同
[PASS] 工程 json 往返   autosave.json         5338821 字节逐字节相同
[PASS] 全节点标脏重建   SmallColorTFT.json    280 个节点，4576146 字节逐字节相同
[PASS] 全节点标脏重建   SmallColor_oled.json  510 个节点，8120567 字节逐字节相同
[PASS] .sty 解析+往返   JL.sty                  24498 字节逐字节相同
[PASS] 对话框冒烟       15 个全部构造+渲染+销毁通过
[PASS] 选中链路         SmallColorTFT 277 + SmallColor_oled 499 个节点
                        （树点选 + 画布真鼠标事件两条路都走）
```

两项的区别很关键：

- **往返**证明"没改过的东西原样带出去"。工程 json 就是 Qt 的
  `QJsonDocument::toJson(Indented)` 格式（`compat/qtjson.py` 用纯 Python 实现了同样的
  写法），本工程每个节点都留着原始 `QJsonObject`，没动过的直接吐回去。
- **全节点标脏重建**证明"改过的东西重建出来也一样"。属性面板一编辑，节点就会
  被标脏，回写走的是"按模型重建"那条路 —— 只要模型漏了任何一个字段，这一项
  立刻就炸。280 / 510 个节点全标脏还能逐字节相同，说明模型是完整的。

自己跑：
```
UITools.exe --json-roundtrip <工程.json>       # 返回 0 = 逐字节相同
UITools.exe --json-rebuild   <工程.json>       # 全节点标脏再比
UITools.exe --sty-dump       <JL.sty>          # 解析 .sty + 往返校验
UITools.exe --dialog-smoke   <工程目录>        # 15 个对话框各造一遍再销毁
UITools.exe --tools-root <T> --click-test N <工程.json>
                                               # 连选 N 个节点（树+画布两条路）
UITools.exe --tools-root <T> --ops-test --out 报告.txt <工程.json>
                                               # 编辑操作与限制，无人值守 20 项
UITools.exe --tools-root <T> --shot a.png <工程.json>
```
`--shot` 走 `QWidget::grab()`，画的是 Qt 自己的后备缓冲，**锁屏/无人值守也能出图**，
用来做界面回归比屏幕截图可靠。

---

## 构建

已在本机验证：**Qt 5.15.2 (msvc2019_64) + VS2022 + CMake + Ninja，编译链接通过。**

```bat
build.bat C:/Qt/5.15.2/msvc2019_64
```

Qt 只需要 `qtbase` 一个组件（约 33 MB）：
```
pip install aqtinstall
python -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 ^
       --archives qtbase --outputdir C:\Qt -b https://mirrors.ustc.edu.cn/qtproject
```

也支持 qmake（`UITools.pro`）和 Qt6。

> **中文路径的坑**（已在 CMakeLists 里解决，但值得知道）：本工程就放在
> `tools\LCD_UI工程\` 下，路径带中文。moc 生成 `moc_*.cpp` 时会把头文件的
> **绝对路径**写进 `#include`，落盘时按本地 8 位编码转换，中文转不出来会写成
> `LCD_UI??/...`，编译直接 C1083。解决办法是给 moc 加 `-p .`
> （`AUTOMOC_MOC_OPTIONS "-p;."`），让它只写文件名，再靠 `-I` 去找。
> 构建目录本身仍建议放纯 ASCII 路径。

## 打包成能直接用的一套

```
dist.bat [输出目录] [Qt根]        默认 C:\bt\dist
```

把三个 exe 和它们需要的 Qt DLL / 插件收进一个目录，**不用配任何环境变量**
（不用 PATH、不用 QT_PLUGIN_PATH）就能跑，约 21 MB。实测在剥空的
`PATH=C:\Windows\system32;C:\Windows` 下：编辑器起得来、能自截，
`QtToolBin --run-resbuilder` 跑完整链，产物与开发环境下**逐字节相同**。

> 目标机器上要有 "Visual C++ 2015-2022 可再发行组件 (x64)"。
> `dist.bat` 会尝试把 `msvcp140.dll` / `vcruntime140*.dll` 一起收进去
> （只有在 VS 开发者命令提示符里跑才找得到），找不到会打一行提示。

---

## 运行

三个 exe 都在同一个构建目录里。先把 Qt 的 bin 加进 PATH：

```
set PATH=C:\Qt\5.15.2\msvc2019_64\bin;%PATH%
set QT_PLUGIN_PATH=C:\Qt\5.15.2\msvc2019_64\plugins
```

### 编辑器

```
UITools.exe --tools-root ..\UIToolkit  ..\ui_128_64_app\模式界面\project\SmallColorTFT.json
```
`--tools-root` 指向工具目录，控件库 / 图标 / 多国语言表都从那儿读。

无界面自检：
```
UITools.exe --json-roundtrip <工程.json>       # 返回 0 = 逐字节相同
UITools.exe --json-rebuild   <工程.json>       # 全节点标脏再比（见上文）
UITools.exe --sty-dump <JL.sty>                # 解析 .sty + 往返校验
UITools.exe --dialog-smoke <工程目录>          # 15 个对话框各造一遍再销毁
UITools.exe --tools-root <T> --click-test N <工程.json>
UITools.exe --tools-root <T> --ops-test --out 报告.txt <工程.json>
UITools.exe --tools-root <T> --shot a.png <工程.json>
```

### 资源生成（整链一步跑完）

```
QtToolBin.exe <工程.json> --run-resbuilder <ResBuilder.exe 路径>
```

它会在工程目录里产出 `project.bin` / `ename.h` / `Resbuilder.xml` / `debug.txt`，
接着调 `ResBuilder` 产出 `result.bin` / `result.str` / `result.h` / `res_ver.h` /
`result_pic_index.h` / `result_str_index.h` / `result.csv` / `result.xml`，
最后跑 `copy_file.bat`（`--no-script` 可关掉）把三个资源文件拷进固件工程。

常用参数：

| 参数 | 说明 |
|---|---|
| `--pj-id N` | 工程 ID，进控件 id 的 bit29..31，默认 0 |
| `--rotate N` | 0/1/2/3 -> 0°/90°/180°/270°，直接进 `.sty` 文件头 |
| `--option-ini <路径>` | 控件类型码表，默认在 `<工程>/../../../UITools/config/ini/option.ini` |
| `--excel <路径>` | 多国语言 xls，写进 `Resbuilder.xml` 的 `excel_path` |
| `--language 0xNN` | 语言位掩码，默认 `0x13`（简中+繁中+英文） |
| `--ename <ename.h>` | **只用于比对**：复用现成 `ename.h` 里已分配的 id |
| `--verify <目录>` | 生成后与该目录里的参考产物逐字节比对 |
| `-o <目录>` / `--no-script` | 输出目录 / 不调 bat |

`ResBuilder` 也能单独跑：

```
ResBuilder.exe [Resbuilder.xml] [-o 输出目录] [--verify 参考目录]
```
不给参数就在当前目录找 `Resbuilder.xml`。

---

## 目录怎么摆

装好之后是**成对的两个目录**：

```
tools\LCD_UI工程\
    UIToolkit\             工具目录        22 MB
    ui_128_64_app\         工程目录        15 MB
```

工程里的相对路径约定是 `..\..\..\<工具目录>\`。

### `UIToolkit\` —— 工具目录

```
UITools.exe  QtToolBin.exe  ResBuilder.exe        三个工具
Qt5*.dll  platforms\  imageformats\  styles\      Qt 运行库，不用配环境变量
config\ini\option.ini  control\  backgrounds\     控件类型码表 / 控件库 / 背景图
Application Data\  多国语言_128_64.xls  Resbuilder.xml
compat\                                           校验脚本
template\  新建工程.bat                            拉空工程
README.md                                         目录内容清单
```

### `ui_128_64_app\` —— 工程目录

```
clear.bat  clear.sh  README.md
模式界面\
    step1-打开UI绘图工具.bat
    step2-生成资源.bat
    step3-自检.bat
    打开图片资源文件夹.bat
    project\
        SmallColorTFT.json  SmallColor_oled.json
        config\  backgrounds\  Application Data\
        copy_file.bat  release.bat  version.txt
        （生成物：跑 step2 就有）
```

缓存和中间文件（`autosave.json`、`uitoolbin.bin`、`Resbuilder.dat`、
`qtread.csv`、`imagelist.txt`）不入库，所以 25 MB 变 15 MB。

> `clear.bat` 里**没有** `del config\*.png`（文字预览图）这一条：本工具不产这些图，
> 删了找不回来。

### 重新生成这两个目录

```
build.bat              编出三个 exe 到 C:\bt\uitools
mkdist_tooldir.bat     装配 ..\UIToolkit\
mkdist_project.bat     装配 ..\ui_128_64_app\
```

两个装配脚本都从源码树里取"覆盖层"，所以目录可以随时删掉重建：

| 源码树 | 装到哪 |
|---|---|
| `tooldir\` | 工具目录根：`README.md`、`新建工程.bat` |
| `projectdir\family\` | 工程族根：`clear.bat`、`clear.sh`、`README.md` |
| `projectdir\screen\` | 每个 `<界面>\`：step1/2/3 + `打开图片资源文件夹.bat` + `ui-config` |
| `projectdir\newproject\` | 空工程才要的：空 `project.ini`、`copy_file.bat`、`pic_lcd\` |

`template\`（新建工程用）= `projectdir\screen\` + `projectdir\newproject\`，
所以新工程和已有工程的 step 脚本永远是同一份，不会走偏。

### 日常就三个双击

| 脚本 | 干什么 |
|---|---|
| `step1-打开UI绘图工具.bat` | 开编辑器 |
| `step2-生成资源.bat` | 弹 QtToolBin 界面，点「生成资源文件(F5)」生成并拷进固件工程 |
| `step3-自检.bat` | 21 项自洽性检查 |

step2 是弹窗的（界面上有进度条和输出）；step1/step3 直接干活。
三个都**不带参数** —— 工具目录按 `..\..\..\UIToolkit` 找，
其余参数从 `project\config\ini\project.ini` 读：

```ini
[Project]
projectfilename=SmallColorTFT.json   ; 要生成哪个工程
projectid=0                          ; 工程 ID，进控件 id 的 bit29..31
projectrotate=0                      ; 0/1/2/3 -> 0°/90°/180°/270°
projectbatscript=copy_file.bat       ; 收尾脚本
projectresbuilder=false              ; true = 不重新生成资源
```

编辑器保存工程时会把 `projectfilename` 写回去，所以新建工程后不用手填。

新工程：`UIToolkit\新建工程.bat <工程名> [界面名]`。

### 两件必须知道的事

> **step2 会真的写你的固件树** —— 按 `copy_file.bat` 把三个资源拷进 `tools\JL\`、
> 把 `ename.h` 拷成 `User\ui_framework\include\common\style_jl02.h`。
> 只想看产物不想拷，加 `--no-script -o <临时目录>` 手动跑。

> **id 会变，资源和头文件必须一起换。** 控件 id 是 `CRC16(宏名)` + 线性探测算的，
> 和别处生成的那一套不一样。`copy_file.bat` 本来就是把 `ename.h` 和三个资源文件
> 一起拷过去的，照常跑没事；但**不要**手工只拷 `JL.sty` 而不更新
> `style_jl02.h` —— 那样 id 对不上，界面整个不出来。

---

## 界面

无菜单栏、无状态栏；一排"图标+文字"工具栏，状态文字在最右；左侧三列树
（结点/属性/ID号 + 眼睛列 + 底部"控件数量"）；第二列「控件列表」组框
（图层/布局大按钮 + 2 列控件网格 + 自定义控件子组）叠属性区（ID号 →
CSS属性_N → 控件专有属性）；右侧页面栏逐页渲染；画布贴左上角。
配色：面板绿 `#C0DCC0`、属性区 `#CEE2CE`、画布 `#F0F0F0`。
详见 [`docs/UI_BEHAVIOR.md`](docs/UI_BEHAVIOR.md)。

属性面板是**数据驱动**的：CSS 状态数 = `element_css.struct` 的长度，
每一项的内容按该状态里的属性描述（`-type` / `enum` / `default` / `min` / `max`）
现场生成。写死反而会和别的控件对不上。

---

## 目录

```
UITools_src/
├─ build.bat            编三个 exe
├─ dist.bat             打成能独立运行的一套
├─ mkdist_tooldir.bat   装配 ..\UIToolkit\
├─ mkdist_project.bat   装配 ..\ui_128_64_app\
├─ tooldir\             装进工具目录的覆盖层：README / 新建工程.bat
├─ projectdir\          装进工程目录的覆盖层：family / screen / newproject
├─ CMakeLists.txt  UITools.pro
├─ include/          编辑器的 21 个头文件
├─ src/
│    ├─ core/        ProjectModel（工程 json，保真往返）/ ControlLibrary / StyFile
│    ├─ ui/          Forms / Canvas / Docks / Property / MainWindow / ProjectDialog
│    ├─ gen/         对话框与全局设置
│    ├─ main.cpp     编辑器入口
│    ├─ sty/         StyBuilder —— 工程 json -> .sty / ename.h / Resbuilder.xml / debug.txt
│    ├─ qttoolbin/   QtToolBin.exe 入口
│    ├─ res/         ResFormat / ImageMono / TextRaster / XlsReader / ResConfig
│    │               / ResBuilderCore / DefaultPalette.h
│    └─ resbuilder/  ResBuilder.exe 入口
├─ resources/        77 个图标 + uitools.qrc
├─ compat/           兼容性校验脚本（全部可复跑，见下）
└─ docs/             格式说明 / 生成链验收 / 界面与编辑行为
```

### 校验脚本（`compat/`）

| 脚本 | 验的什么 | 本机结果 |
|---|---|---|
| `verify_toolchain.py` | **整链**：本次产物 vs 参考产物（借现成 id） | 不合格 0 项 |
| `verify_selfconsistent.py` | **自主产物自洽性**：不借 id，按固件加载路径逐条查 | 21 项全过 |
| `verify_allctrl.py` | 每种控件 × 每条建节点的路，产物结构逐项比 | — |
| `sty_gen.py` | `.sty` 数据区排布算法 | 24498 B 逐字节相同 |
| `sty_diff.py` | 两个 `.sty` 的结构化对比 | — |
| `verify_types.py` | 控件类型码（option.ini 查表） | 273/273 |
| `verify_order.py` | 控件记录排列顺序 + 父子指针 | 3/3 页 |
| `verify_window.py` | 页头 28 字节窗口记录 | 18/18 |
| `verify_css.py` | `element_css1` 各字段 | 273/273 |
| `verify_payload.py` | 控件负载字段 | 全部一致 |
| `verify_resids.py` | `.sty` 里的图片/文字资源号 | 511/511 |
| `verify_crc.py` | 页表三个 CRC | 9/9 |
| `res_dump.py` | `.res`/`.str` 解析 + 覆盖率 | 100.00%，0 空洞 |
| `verify_res_pixels.py` | 源 BMP -> OSD1 点阵 | 104/104 |
| `verify_str.py` | GDI 渲染 -> 字符串点阵 | 141/141 |
| `verify_palette_order.py` | 调色板：集合必须相同，差异只许在顺序上 | 3/3 页 |

## 各部分成色

**已验证**

- 工程 json 的写出格式 = Qt `toJson(Indented)`，三个真实工程字节一致
- `.sty` / `.res` / `.str` 的**读侧和写侧**都打通了，见上面的整链验收
- 控件库从 `control/control.json` + `control/ex/*.json` 读，既有工程完全兼容

**能真正干活**

- 打开 / 保存工程（保真往返），未建模字段原样保留
- 多页画布、图层/布局/控件树、拖动、八向手柄改尺寸、缩放 25%–800%、像素网格
- 对象树 / 页面栏 / 属性面板 / 控件列表 四联动
- 几何编辑正确写回 `element_css.struct[N].rect`（工程 json 里控件的坐标就在那儿，
  不是顶层 `rect`）
- **属性编辑全通**：枚举/整数/字符串当场写回；图片列表、文字列表、事件动作
  各有专门的对话框
- **工程缩放**（换屏幕尺寸，所有控件坐标按比例换算）、**工程配置**
  （工程名 / 多国语言表 / 启用语言掩码）、**全局设置**（跟着工程走）
- 从工程 json 一路生成 `.sty` / `.res` / `.str` 并调 `copy_file.bat`

**对话框**

| 类 | 干什么 |
|---|---|
| `ImageFileDialog` | 选图片（目录树 + 缩略图 + 已选列表 + 上下移），返回相对工程目录的路径 |
| `I18nLanguage` | 选文字 ResID（读多国语言 xls，带勾选/过滤/排序） |
| `ActionList` | 事件动作表（事件/动作/对象/参数，右键增删移） |
| `ConfigProject` | 工程名 / 多国语言表 / 语言掩码 |
| `GlobalSettings` | 编辑器偏好，存工程目录下的 `Application Data/ui-config` |
| `ZoomProject` | 换屏幕尺寸 + 等比缩放全部控件 |
| `findDlg` | 按 ID号/名称查找控件 |
| `MenuItemDialog` | 列表控件的菜单项（图片 + 文字 ResID） |
| `ImageListView` | 只看不选的图片浏览器 |
| `ProgressDlg` / `BusyIndicator` | 进度条 / 转圈提示 |
| `GridHelpLine` / `HVLineWidget` / `RuleWidget` | 画布上的网格层 / 对齐十字线 / 标尺 |
| `BaseDialog` / `BaseScrollArea` / `DragButton` | 基类与小部件 |

**没做 / 还缺样本**

- 事件动作（`element_event_action`）的**内容**：结构已知，编辑器和生成器都照着
  写了，但本工程 277 个控件的 action 全是 `num=0`，**没有真实样本可比对**。
- 图片的 RLE / QuickLZ 压缩：本工程 `image_compress_method = none`，没有样本。
- 调色板前缀的排列顺序（集合已对上）。
- 标尺/辅助线控件写好了，但还没挂到画布上（需要时接两行即可）。

---

## 控件 ID 的低 16 位

`ename.h` 里每个宏的值是 24 位 ID，位域是：

```
bit23..22 页索引    bit21..16 控件类型(== .sty 控件头 type)    bit15..0 名字哈希
```

既有资源里那低 16 位是一份查表，同一份工程每次生成都不一样，没法照着复现。
这里用**确定性**的方案：`CRC16-XMODEM(宏名)`，撞了就线性探测。

**这不影响使用**，原因有三：

1. `.sty` 和 `ename.h` 是**同一次生成一起产出**的，两者内部自洽就够；
2. `Resbuilder.xml` / `result.*` 里**不含**这些 id（扫过，0 命中）；
3. 固件只用 `ename.h` 里的**宏名**（`ui_style.h` 把 `PAGE_2` 映射成
   `ID_WINDOW_MAIN` 之类），`copy_file.bat` 每次都会把新的 `ename.h`
   拷成 `style_jl02.h`。

只有要和既有资源**逐字节比对**时才需要 `QtToolBin --ename <现成的 ename.h>`
把那一套 id 借过来。
