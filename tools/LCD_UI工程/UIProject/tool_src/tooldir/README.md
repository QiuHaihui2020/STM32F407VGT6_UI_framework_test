# tool —— UI 工具目录

点阵屏 UI 工具链的工具：三个 exe + 它们要用的数据文件。
本目录住在它服务的那个 UI 工程里 —— `<UI工程>\tool\` ——
所以工程里的脚本用 `..\tool` 就找到这里。固件那边一行都不用改。

源码在 [`../tool_src/`](../tool_src/)。
本目录由 `tool_src\mkdist_tooldir.bat` 生成，**不要手改** —— 改了下次重新
生成就没了。唯一的例外是 `assets\`：它在版本库里，改了直接提交。

---

## 目录里有什么

### 三个工具

| 文件 | 干什么 |
|---|---|
| `UITools.exe` | 布局编辑器 |
| `QtToolBin.exe` | 工程 json → `project.bin` + `ename.h` + `Resbuilder.xml` + `debug.txt` |
| `ResBuilder.exe` | `Resbuilder.xml` + 图片 + xls → `result.bin` / `result.str` + 5 个头文件 |

### 数据与配置

全在 `assets\` 下（代码里的解析和兜底见 `../tool_src/src/core/AssetPaths.h`）：

| 文件 | 干什么 |
|---|---|
| `assets\widgets.json` | 控件库 |
| `assets\widgets.d\` | 自定义控件（「保存成控件」落这儿） |
| `assets\typecodes.ini` | 控件类型码表 |
| `assets\canvas\` | 画布背景图（放 JPG 进去就能选） |
| `assets\i18n_128_64.xls` | 多国语言表 |
| `assets\pack_template.xml` | 资源描述模板，工程里没有时拷一份过去 |

### 运行时与附件

| 文件 | 说明 |
|---|---|
| `Qt5Core.dll` `Qt5Gui.dll` `Qt5Widgets.dll` | Qt 运行库（动态链接） |
| `platforms\` `imageformats\` `styles\` | Qt 插件。**不用配任何环境变量** |
| `compat\` | 兼容性校验脚本（需要 python3） |
| `template\` | 新建工程用的空工程（上一级的 `新建工程.bat` 克隆它） |
| `自检.bat` | 对上一级的 `project\` 跑 25 项自洽性检查（要 python3） |
| `clear.bat` `clear.sh` | 清掉 `project\` 里的全部生成物 |
| `README.md` | 本文件 |

不产缓存和中间文件：`Resbuilder.dat`（每次全量算）、`uitoolbin.bin`（不需要）。

---

## 日常用法

### 已有工程

工程就在上一级 [`../`](../)。打开它有四种方式，随便哪种都行：

1. **双击 `tool\UITools.exe`** —— 不给任何参数，它自己往 `..\project` 找
   `config\ini\project.ini`，把里面 `projectfilename` 指的那份打开。
2. **双击工程文件 `project\*.uiproj`** —— 先跑一次 `注册文件关联.bat`
   （只写当前用户的注册表，不需要管理员；只碰 `.uiproj`，不动 `.json`）。
3. **把工程文件或工程目录拖进编辑器窗口** —— 工程文件、`project\`、
   或者整个 UI 工程目录，三种都认。
4. **双击上一级的 step 脚本**：


| 脚本 | 干什么 |
|---|---|
| `step1-打开UI绘图工具.bat` | 开编辑器。工具目录和要打开的工程都自动找，不用传参 |
| `step2-生成资源.bat` | **弹出 QtToolBin 界面**，在界面上点「生成资源文件(F5)」 |
| `step3-自检.bat` | 检查产物自洽性（25 项，见下） |

也可以直接跑命令行：

```
cd <UI工程>\project
..\tool\QtToolBin.exe --run-resbuilder ..\tool\ResBuilder.exe
```

step2 是**弹窗**的：双击弹出 QtToolBin 界面，点「生成资源文件(F5)」
才开始生成，界面上有进度条和输出。界面各行就是
`project\config\ini\project.ini` 里的内容，点生成时写回去：

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
新建工程.bat <工程名>
    例: 新建工程.bat ui_128_64_MY
```

在 `LCD_UI工程\<工程名>\` 拉出一份完整的空工程 —— 空 `project\`、几个启动
脚本，外加一份 `tool\`（本目录整份拷过去，约 22 MB）。这样每个 UI 工程都是
自足的一个目录，删掉就干净了。然后：

1. 把 BMP 放进 `<界面名>\project\config\pic_lcd\`
2. 双击 `step1-打开UI绘图工具.bat`，用「新建工程」建页面，保存
3. 双击 `step2-生成资源.bat`
4. 按需改 `project\copy_file.bat` 里的两个路径（资源目录 / 头文件目录）

### 编辑操作与限制

逐条对照见 `docs/UI_BEHAVIOR.md` 第 5 节：

* **从控件列表把控件拖到画布上**建控件 —— 这是主要手势。落在哪个容器
  上就挂到哪个容器下，落点就是控件坐标；落到所有布局之外时拒绝（松手前指针
  就是禁止符号）。
* **点击建**是第二条路：要先选中一个布局（建布局则要先选中图层），否则拦下来
  并给出提示。建完弹框问名字，默认名是整页流水号（图层_0 / 布局_1 /
  电池电量_2 …）。
* **CSS 属性页整页都能改**：对齐方式、默认隐藏、标志、背景色、背景图片、
  内边框线、滚动方式……
* **列表能用滚轮翻行**，背景色/背景图右边有清除键。
* **右键菜单画布和对象树是同一套**：删除当前-xxx / 保存成控件 / 显示·隐藏（自己
  和子对象两档）/ 复制 / 粘贴 / 移到顶层·上一层·下一层·底层 / 查找对像。
  列表和表格再各加自己的（添加行列、行高列宽、间隔、滚动方向、单元尺寸）。
  页面空白处右键是：删除当前页面 / 修改背景色 / 修改背景图片。
* **粘贴只有布局收**，不收的时候按两句不同的话分别拒绝。
* **删除一律先问**，按钮就叫 `<删除>`；这个工具没有撤消，所以默认焦点在"取消"。
* **改了没存**就要新建/打开/退出时会拦一道。
* **宽高不能设成 0**，直接拦下来告诉你，而不是偷偷改成 1。
* **对话框**：[全局设置]是五条路径（多国语言文件 / 控件文件 / 图片资源目录 /
  工程目录 / 自定义控件目录）；另有 [工程缩放]、[图片编辑]、[显示列表]、
  [工程设置]、[事件动作]。

回归（无人值守，20 项）：

```
UITools.exe --tools-root <本目录> --ops-test --out 报告.txt <工程.json>
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

QtToolBin.exe  不带参数 = 弹界面
               [工程.json] [--pj-id N] [--rotate N] [--excel xls]
               [--run-resbuilder <ResBuilder.exe>] [--script bat|--no-script]
               [-o 输出目录] [--verify 参考目录]   ← 给了这些就走命令行不弹窗
               [--gui] [--cli]                    强制某种模式
               不给 json 时读当前目录的 config\ini\project.ini

ResBuilder.exe [Resbuilder.xml] [-o 输出目录] [--verify 参考目录]
               不给参数时找当前目录的 Resbuilder.xml
```

---

## 校验

```
python compat\verify_selfconsistent.py <产物目录> <固件的 include\common 目录>
```

只看产物自身，对着固件的加载路径逐条查：`UI_VERSION` 一致性、三个页表 CRC、
控件 id ↔ ename 宏的双向对应、id 位域自洽、指针落点与重定位表、
资源号范围、`.res`/`.str` 的 head_crc、以及 `ui_style.h` 硬依赖的宏在不在。
25 项，全过才算能上机。

想和工程目录里现成的那套产物逐字节比对：

```
python compat\verify_toolchain.py <本目录> <工程目录> <临时输出目录>
```

「双击 exe 能不能自己找到工程」这条也要能验 —— 它失败的样子是起来一个空窗口，不报错：

```
cd <UI工程>\tool
UITools.exe --ops-test --out r.txt      # 不给工程路径
```

工程没找到的话控件库那一串断言立刻塌下来（正常是 449 项全过）。

其余脚本见 `../tool_src/README.md` 的校验脚本表。

---

## 两件必须知道的事

**1. id 会变，资源和头文件必须一起换。**
控件 id 是 `CRC16(宏名)` + 线性探测算出来的，**确定性**的，只有增删/改名控件时
才变 —— 但和别处生成的那一套对不上。`copy_file.bat` 本来就是把 `ename.h` 和三个
资源文件一起拷过去的，照常跑没事；但**不要**手工只拷 `JL.sty` 而不更新
`style_jl02.h` —— 那样 id 对不上，界面整个不出来。

**2. 和参考产物的差异都落在不可复现的位置上。**

| 差异 | 原因 |
|---|---|
| `project.bin` 61 字节 | Time/number 控件 `char format[16]` 尾部，参考文件里是没清零的堆内存 |
| `result.bin`/`result.str` 各 4 字节 | 文件头 `resver`，参考文件里是随机值；这里改成内容 CRC32 |
| 调色板前缀顺序 | 集合一致，顺序不同。OSD1 单色屏不读调色板 |
| 几个 `.h` 里的时间戳 | — |

顺带：固件里 `ui_style_file_version_compare` / `res_file_version_compare` /
`str_file_version_compare` 三个版本校验**都没人调**，这份固件树里也没有
`res_ver.h`，所以 `resver` 对不上不会有任何后果。
