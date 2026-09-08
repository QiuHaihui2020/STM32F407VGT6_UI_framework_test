# UITools_rebuilt —— 重建版 UI 工具目录

这个目录是原厂 `UITools\` 的**同位替代品**：布局一样、相对路径约定一样，
所以工程里那套 `..\..\..\UITools\` 的调用方式换成 `..\..\..\UITools_rebuilt\`
就能直接用，固件那边一行都不用改。

源码与逆向依据在 [`../UITools_src/`](../UITools_src/)。
本目录是 `UITools_src\mkdist_tooldir.bat` 生成的，**不要手改**——改了下次重新
生成就没了。

---

## 和原厂 UITools\ 的**逐项**对照

原厂目录里 19 个条目，这里一个不漏地交代。

### 有对应的

| 原厂 `UITools\` | 这里 | 说明 |
|---|---|---|
| `ui-tools.exe` (20 MB) | `UITools.exe` | 布局编辑器 |
| `QtToolBin.exe` (18 MB) | `QtToolBin.exe` | 工程 json → `project.bin` + `ename.h` + `Resbuilder.xml` + `debug.txt` |
| `ResBuilder.exe` (1.1 MB) | `ResBuilder.exe` | `Resbuilder.xml` + 图片 + xls → `result.bin` / `result.str` + 5 个头文件 |
| `config\ini\option.ini` | 同 | 控件类型码表，原样拷贝 |
| `control\` | 同 | 控件库 + `ex\` 扩展模板，原样拷贝 |
| `backgrounds\` | 同 | 画布背景，原样拷贝 |
| `Application Data\ui-config` | 同 | 编辑器默认值，原样拷贝 |
| `多国语言_128_64.xls` | 同 | 多国语言表，原样拷贝 |
| `Resbuilder.xml` | 同 | 模板，工程里没有时拷一份过去 |

### 没做的（两个 exe）

| 原厂 | 情况 |
|---|---|
| `QTToolJson 1.1.exe` (16 MB) | **没逆向**。从名字和 SDK 文档推断是老工程 json 的坐标换算（绝对像素 → 万分比），**这是推断不是实锤** —— 它是 Qt 静态包、资源压缩过，挖不出可靠字符串。正常流程用不到：新工程从编辑器建、坐标本来就写在 `element_css` 里。 |
| `UI工程源文件升级工具.exe` (17 MB) | **没逆向**。推断是把更老版本的工程文件升级到当前格式，同样是推断。要升级老工程，还得用原厂这个 —— 它一直在 `..\UITools\` 里，没动过。 |

### 不需要的（原厂目录里的缓存和残留）

| 原厂 | 为什么不搬 |
|---|---|
| `Resbuilder.dat` (920 KB) | 原厂 ResBuilder 的缓存文件。重建版不用缓存，每次全量算。 |
| `res_ver.h` | 原厂在这个目录里跑过一次 ResBuilder 留下的产物，不是输入。 |
| `result.bin` (16 B) / `result.str` (28 B) | 同上 —— 空资源文件，就是"在 UITools\ 里空跑一次"的结果。 |
| `result.h` / `result.csv` / `result.xml` | 同上。 |
| `uitoolbin.bin` (29 B) | 原厂 QtToolBin 的中间文件，重建版不产这个东西。 |

> 判定依据：`step2-打开UI资源生成工具.bat` 只从这个目录拷 `Resbuilder.xml` 和
> `config\ini\option.ini` 两样东西进工程，其余谁都没读。

### 多出来的

| 这里 | 说明 |
|---|---|
| `Qt5Core.dll` `Qt5Gui.dll` `Qt5Widgets.dll` | Qt 运行库。原厂是静态链接的单文件，这里是动态的 |
| `platforms\` `imageformats\` `styles\` | Qt 插件。**不用配任何环境变量** |
| `re\` | 验证脚本（需要 python3） |
| `template\` `新建工程.bat` | 拉一个空工程出来 |
| `README.md` | 本文件 |

---

## 日常用法

### 已有工程

配套的工程目录是 [`../ui_128_64_JL02_rebuilt/`](../ui_128_64_JL02_rebuilt/) ——
原厂 `ui_128_64_JL02\` 的同位副本，设计文件和图片一样，只是由本目录驱动。
每个 `<界面>\` 下三个脚本，双击即可：

| 脚本 | 对应原厂 | 干什么 |
|---|---|---|
| `step1-打开UI绘图工具.bat` | 同名 | 开编辑器。工具目录和要打开的工程都自动找，不用传参 |
| `step2-生成资源.bat` | `step2-打开UI资源生成工具.bat` | **弹出 QtToolBin 界面**，在界面上点「生成资源文件(F5)」 |
| `step3-自检.bat` | 无 | 检查产物自洽性（21 项，见下） |

想直接驱动**原厂那个** `ui_128_64_JL02\` 工程也行 —— 目录深度一样：

```
cd ui_128_64_JL02\模式界面\project
..\..\..\UITools_rebuilt\QtToolBin.exe --run-resbuilder ..\..\..\UITools_rebuilt\ResBuilder.exe
```

step2 和原厂一样是**弹窗**的：双击弹出 QtToolBin 界面，点「生成资源文件(F5)」
才开始生成，界面上有进度条和输出。界面各行就是
`project\config\ini\project.ini` 里的内容，点生成时写回去
—— **和原厂读的是同一份配置**：

```ini
[Project]
projectfilename=SmallColorTFT.json   ; 要生成哪个工程
projectid=0                          ; 工程 ID，进控件 id 的 bit29..31
projectrotate=0                      ; 0/1/2/3 -> 0°/90°/180°/270°
projectbatscript=copy_file.bat       ; 收尾脚本
projectresbuilder=false              ; true = 不重新生成资源
```

编辑器保存工程时会把 `projectfilename` 写回这个文件，所以新建工程后不用手填。

### 新工程

```
新建工程.bat <工程名> [界面名]
    例: 新建工程.bat ui_128_64_MY 模式界面
```

在 `LCD_UI工程\<工程名>\<界面名>\` 拉出一份空工程（目录结构与
`ui_128_64_JL02_rebuilt\` 一致，模板在本目录的 `template\`），然后：

1. 把 BMP 放进 `<界面名>\project\config\pic_lcd\`
2. 双击 `重建版-step1-打开UI绘图工具.bat`，用「新建工程」建页面，保存
3. 双击 `重建版-step2-生成资源.bat`
4. 按需改 `project\copy_file.bat` 里的两个路径（资源目录 / 头文件目录）

### 编辑操作与限制

和原厂对齐的那一套（每一条都能在 `ui-tools.exe` 里找到对应的提示语，出处和逐条对照见
`docs/FACTORY_UI.md` 第 5 节 —— 依据是原厂官方手册
`tools/LCD_UI工程/doc/UI布局工具使用说明.pdf` 加上 exe 里扫出来的界面用语）：

* **从控件列表把控件拖到画布上**建控件 —— 这是手册里的主要手势。落在哪个容器
  上就挂到哪个容器下，落点就是控件坐标；落到所有布局之外时拒绝（松手前指针
  就是禁止符号）。
* **点击建**是第二条路：要先选中一个布局（建布局则要先选中图层），否则拦下来
  并给出原厂原话。建完弹框问名字，默认名是整页流水号（图层_0 / 布局_1 /
  电池电量_2 …）。
* **CSS 属性页整页都能改了**：对齐方式、默认隐藏、标志、背景色、背景图片、
  内边框线、滚动方式…… 以前这一页除了「位置坐标」全是只读的，改了不落盘。
* **列表能用滚轮翻行**（手册 2.11），背景色/背景图右边有清除键（手册 2.3）。
* **右键菜单画布和对象树是同一套**：删除当前-xxx / 保存成控件 / 显示·隐藏（自己
  和子对象两档）/ 复制 / 粘贴 / 移到顶层·上一层·下一层·底层 / 查找对像。
  列表和表格再各加自己的（添加行列、行高列宽、间隔、滚动方向、单元尺寸）。
  页面空白处右键是：删除当前页面 / 修改背景色 / 修改背景图片。
* **粘贴只有布局收**，不收的时候按原厂的两句话分别拒绝。
* **删除一律先问**，按钮就叫 `<删除>`；这个工具没有撤消，所以默认焦点在"取消"。
* **改了没存**就要新建/打开/退出时会拦一道。
* **宽高不能设成 0**，直接拦下来告诉你，而不是偷偷改成 1。
* **对话框也按原厂重做了**：[全局设置]是五条路径（多国语言文件 / 控件文件 /
  图片资源目录 / 工程目录 / 自定义控件目录），不是之前那套自己编的编辑器偏好；
  [工程缩放]、[图片编辑]、[显示列表]、[工程设置]、[事件动作]的标题、按钮、
  提示语也都换成了原厂原话。153 条原厂界面用语用上了 149 条，差的 4 条见
  `docs/FACTORY_UI.md` 5.10。

回归（无人值守，20 项）：

```
UITools.exe --tools-root <UITools目录> --ops-test --out 报告.txt <工程.json>
```

### 命令行

```
UITools.exe    [--tools-root <本目录>] [工程.json]
               --json-roundtrip <json>    读进来再写出去，逐字节比
               --json-rebuild   <json>    全节点标脏重建，逐字节比
               --sty-dump       <JL.sty>  解析 .sty + 往返校验
               --dialog-smoke   <工程目录> 15 个对话框各造一遍
               --ops-test --out <报告.txt> <json>
                                          无人值守跑一遍"操作逻辑与限制"
               --shot a.png               自截主窗口（锁屏也能出图）

QtToolBin.exe  不带参数 = 弹界面（同原厂）
               [工程.json] [--pj-id N] [--rotate N] [--excel xls]
               [--run-resbuilder <ResBuilder.exe>] [--script bat|--no-script]
               [-o 输出目录] [--verify 原厂目录]   ← 给了这些就走命令行不弹窗
               [--gui] [--cli]                    强制某种模式
               不给 json 时读当前目录的 config\ini\project.ini

ResBuilder.exe [Resbuilder.xml] [-o 输出目录] [--verify 原厂目录]
               不给参数时找当前目录的 Resbuilder.xml
```

---

## 验证

```
python re\verify_selfconsistent.py <产物目录> <固件的 include\common 目录>
```

只看产物自身，对着固件的加载路径逐条查：`UI_VERSION` 一致性、三个页表 CRC、
控件 id ↔ ename 宏的双向对应、id 位域自洽、指针落点与重定位表、
资源号范围、`.res`/`.str` 的 head_crc、以及 `ui_style.h` 硬依赖的宏在不在。
21 项，全过才算能上机。

想和原厂产物逐字节对拍（需要工程目录里还留着原厂的产物）：

```
python re\verify_toolchain.py <本目录> <工程目录> <临时输出目录>
```

其余脚本见 `../UITools_src/README.md` 的验证脚本表。

---

## 两件必须知道的事

**1. id 会变，资源和头文件必须一起换。**
重建版分配的控件 id 和原厂不一样（原厂是随机的，同一份工程每次生成都不同；
重建版是 `CRC16(宏名)` + 线性探测，**确定性**的，只有增删/改名控件时才变）。
`copy_file.bat` 本来就是把 `ename.h` 和三个资源文件一起拷过去的，照常跑没事；
但**不要**手工只拷 `JL.sty` 而不更新 `style_jl02.h` —— 那样 id 对不上，
界面整个不出来。

**2. 和原厂产物的差异都是原厂自己也复现不了的。**

| 差异 | 原因 |
|---|---|
| `project.bin` 61 字节 | Time/number 控件 `char format[16]` 尾部是原厂没清零的堆内存 |
| `result.bin`/`result.str` 各 4 字节 | 文件头 `resver`，原厂是随机值。重建版改成内容 CRC32 |
| 调色板前缀顺序 | 集合一致，顺序没复现。OSD1 单色屏不读调色板 |
| 几个 `.h` 里的时间戳 | — |

顺带：固件里 `ui_style_file_version_compare` / `res_file_version_compare` /
`str_file_version_compare` 三个版本校验**都没人调**，这份固件树里也没有
`res_ver.h`，所以 `resver` 对不上不会有任何后果。
