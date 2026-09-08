# ui-tools.exe 逆向报告

对象：`tools/LCD_UI工程/UITools/ui-tools.exe`
目的：原厂只交付了 exe，源码从来没给过；本次把能还原的结构全部还原出来，
并据此重写一个能编译、能跑、能读写同样工程文件的 Qt 工程。

---

## 1. 二进制事实

| 项 | 值 |
|---|---|
| 格式 | PE32（`pei-i386`），入口 `0x00401480` |
| 大小 | 20 057 600 B |
| 框架 | **Qt 5.9.3** |
| 编译器 | mingw-w64 / GCC（`.eh_frame` + winpthreads 路径串） |
| 链接 | **静态**（12.5 MB `.text`，无外部 Qt DLL 依赖） |
| 符号 | 已 strip（`nm` 报 no symbols），无 PDB、无源码路径残留 |

节区：

```
.text     0x00C8E298  @0x00401000     .rdata   0x00429F20  @0x01096000
.data     0x000058C4  @0x01090000     .qtmetad 0x00000B60  @0x014C0000
.eh_fram  0x0024CF30  @0x014C1000     .rsrc    0x00010B70  @0x01721000
```

同目录其余四个 exe 同源：`QtToolBin.exe`(18 MB) / `QTToolJson 1.1.exe`(15 MB) /
`UI工程源文件升级工具.exe`(17 MB) 都是 Qt5+mingw 静态；`ResBuilder.exe`(1 MB)
是 MSVC 原生程序，不是 Qt。

**为什么不做反编译**：静态链接的 Qt C++ 程序，Ghidra/IDA 出来的是几十万行伪代码，
模板与信号槽全被打散、内联，产物不可编译也不可维护。真正可还原且有工程价值的是
**结构**——Qt 把它完整地留在了二进制里（见下）。所以本次走的是
"挖结构 + 按结构重写"，不是"反汇编翻译"。

---

## 2. 从 moc 元数据还原类结构

Qt 的 `Q_OBJECT` 宏会给每个类生成 `staticMetaObject`，里面完整保存了
**类名、基类、全部 signals/slots 的名字与参数类型、properties、enums 及其取值**。
这些数据在 `.rdata` 里是明文结构，strip 删不掉。

`re/moc_scan.py` 干两件事：

1. 扫 `QByteArrayData` 数组（16 B/项：`ref=-1, size, alloc=0, offset`，
   offset 相对自身地址）找出所有 moc 字面量块；
2. 扫 6 指针的 `QMetaObject`（superdata / stringdata / data / static_metacall /
   related / extradata），按 revision 7 解码 `qt_meta_data`。

结果：**stringdata 块 492 个，staticMetaObject 387 个，387 个全部解码成功**，
其中应用自有类 **43 个**（其余是 Qt 自带）。完整接口见
[`RECOVERED_CLASSES.txt`](RECOVERED_CLASSES.txt)。

> 两个踩过的坑，记在这里免得重走：
> 1. moc 会发 `size == 0` 的空串字面量，扫描器若要求 `size > 0`，
>    一个块会被切成好几段（曾把 `MainWindow` 只切出 2 项）。
> 2. Qt5 的 `QMetaType::Void` 是 **43** 不是 0（`QVariant`=41、`QModelIndex`=42）。
>    表写错的话所有 void 槽都会显示成返回 `QVariant`。

### 类层次（43 个）

```
QMainWindow ── MainWindow                    onChangeBackgroud / onDobuleClickedImage
QObject     ── CanvasManager                 13 槽 + Q_PROPERTY(QSize mPageSize)
QFrame      ── ScenesScreen                  onChangedBackgroundColor
QWidget     ── FormResizer ── BaseForm ──┬── NewLayer
                                         ├── NewLayout
                                         ├── NewFrame
                                         ├── NewList
                                         └── NewGrid
QWidget     ── SizeHandleRect                signal mouseButtonReleased(QRect,QRect)
QWidget     ── Backgroud / GridHelpLine / HVLineWidget / RuleWidget / FileEdit
QWidget     ── BaseProperty ──┬── ComProperty
                              └── CssProperty
QDockWidget ── TreeDock / PageView
QGroupBox   ── CompoentControls / Border / Position
QTabWidget  ── PropertyTab
QScrollArea ── BaseScrollArea
QPushButton ── DragButton
QDialog     ── BaseDialog ──┬── ActionList
                            └── ProgressDlg
QDialog     ── ProjectDialog / ConfigProject / I18nLanguage / GlobalSettings
               / ZoomProject / MenuItemDialog / ImageListView / ImageFileDialog
               / BusyIndicator / findDlg
```

几处值得记的观察：

- **`CanvasManager` 是 `QObject` 不是 `QWidget`** —— 它是"文档 + 动作"中枢，
  菜单/工具栏直接连它的槽。重写时照搬了这个划分。
- **`BaseForm::ObjTypes`** 的取值 `T_NewLayer=0 … T_NewGrid=4` 与 `.sty` 里的
  控件 type 有对应关系，是连接编辑器与二进制格式的关键枚举。
- 原程序自带的拼写错误一律保留：类名 `Compoent`**s**`Controls`（少个 n）、
  `Backgroud`、槽名 `onDobuleClickedImage`、形参名 `idex`。改了就对不上元数据。
- `FormResizer` / `SizeHandleRect` 这两个名字来自 **Qt Designer 自带的
  `qdesigner_internal`**（`formresizer.cpp` / `widgetselection.cpp`）。
  二进制里它们是全局符号、不带命名空间 —— 原作者把那份代码抄进了工程。
- `CloseButton`(qtabbar.cpp) 和 `DefaultStateTransition`(qstatemachine.cpp)
  虽然不以 Q 开头，但属于 Qt 私有类，已从重建清单里剔除。

---

## 3. qrc 资源

`re/qrc3.py` 还原了 rcc 的三张表（data / name / struct），
从 `.rdata` 里完整导出 **77 个 PNG**，路径与原程序一致：`:/icon/icons/*.png`。

定位过程值得记一笔：rcc 三张表在本二进制里的排布是
**struct(0x0109D060) → name(0x0109D740) → data(0x0109E180)**，
和 rcc 生成的 .cpp 里的书写顺序不同，按"data 在最前"去找会扑空。
可靠的锚点是：连续 ≥8 项 `nameOffset` 都能命中名字表的那段就是 struct 表；
再从中回溯 `nameOff==0 && flags==2 && firstChild==1` 的根项。
条目是 22 字节（rcc version 2，尾部 8 字节时间戳）。

根 → 前缀 `icon` → 目录 `icons` → 77 个文件。已放进 `resources/icons/`，
`resources/uitools.qrc` 保持同样的前缀，代码里的 `:/icon/icons/xxx.png`
与原程序逐字相同。

---

## 4. 界面（.ui）还原

`.ui` 被 uic 编译成了 C++，`.ui` 文件本身不在二进制里；但 uic 生成的
`setObjectName(QStringLiteral("..."))` 把**控件名按原顺序**留在了 `.rdata`。
`re/strlit_scan.py` 扫出 442 条 QStringLiteral，其中六个对话框的控件清单完整可读：

| 对话框 | 恢复出的控件名（原顺序） |
|---|---|
| `findDlg` | verticalLayout, horizontalLayout, label, lineEdit, horizontalLayout_2, pushButton |
| `ZoomProject` | verticalLayout, lab_oldh, horizontalLayout, spinBoxH, buttonBox, groupBox, gridLayout, lab_oldw, spinBoxW, label_6, label_3, label_2, horizontalLayout_2, label |
| `I18nLanguage` | layoutWidget, formLayout, label_4, txtfontsize, verticalLayout_3, label, itemWidget, verticalLayout, item_selectall, item_dselectall, item_re, horizontalLayout, label_2, btn_ok, btnCancel, verticalLayout_2, label_3, itemSelected, horizontalLayout_2, btnUp(`:/icon/icons/go-up.png`), btnDown(`:/icon/icons/go-down.png`) |
| `ConfigProject` | buttonBox, verticalLayout_4, label_2, langWidget, verticalLayout_2, lang_selectall, lang_dselectall, lang_re, layoutWidget, verticalLayout, openfile(`:/icon/icons/fileopen.png`), view_lang, filestatus, layoutWidget1, horizontalLayout, line |
| `ProjectDialog` | buttonBox, label_3, prjname, layoutWidget2, verticalLayout, pushButton(`:/icon/icons/fileopen.png`), view_lang, filestatus, groupBox, layoutWidget, gridLayout, label, spinBox, label_2, spinBox_2, layoutWidget1, horizontalLayout |
| `GlobalSettings` | verticalLayout, label_4, `background-color: rgb(223, 28, 28);`, treeWidget, buttonBox |

这些名字不是装饰：`on_openfile_clicked` / `on_lang_selectall_clicked` /
`on_pushButton_clicked` 这类槽是 Qt 的 **connectSlotsByName** 自动连接，
槽名反过来证明了控件名。两边互相印证，可信度很高。

`MainWindow.ui` 只有 `centralWidget / menuBar / mainToolBar / statusBar / gridLayout`
五个对象 —— 说明主界面几乎全是代码搭的。

---

## 5. 文件格式

见 [`FILE_FORMATS.md`](FILE_FORMATS.md)。要点：

- `.sty` 容器格式**已完全验证**，`re/sty_roundtrip.py` 对 `tools/JL/JL.sty`
  做拆解-重组，24498 字节逐字节一致；
- 控件负载布局与固件 `control.h` 的结构体逐字节吻合（Text 48 B 已逐字段核对）；
- `.res` / `.str` 的布局在固件 `liba/res/resfile.c` 里有现成解析器；
- **控件 ID 低 16 位的哈希算法未解出**，排除范围见 FILE_FORMATS.md 第 6 节。

---

## 6. 还原度 / 未决项

### 已还原且可验证

- 43 个应用类的类名、继承链、全部 signals/slots/properties/enums —— 逐字精确；
- 77 个 qrc 图标 —— 原始字节；
- 6 个对话框的控件构成 —— 名字与顺序；
- 工程 json / 控件库 json 的完整 schema —— 有真实样本佐证；
- `.sty` 容器与控件头/负载布局 —— 往返字节一致。

### 无法还原（物理上不可能）

**每个槽的函数体**。它们是编译并内联优化过的机器码，不存在"还原成原始 C++"
这回事。重建工程里这些槽有两种状态：

- `src/ui/*.cpp` —— 按行为重写，能真正干活（打开/保存工程、画布编辑、
  树/页面/属性联动、`.sty` 解析）；
- `src/gen/*.cpp` —— 由 `re/gen_classes.py` 生成的骨架，签名对、函数体是
  `// TODO(UITools)`。哪些类属于这一类，见 README。

### 未决项

1. **控件 ID 低 16 位的哈希** —— 见 FILE_FORMATS.md 第 6 节的完整排除清单。
   不阻塞工具链自洽。
2. **`element_event_action` 的布局** —— 事件动作编辑（`BaseForm::onActionDialog`、
   `ActionList` 对话框）因此没实现。
3. **css 块与控件索引表的内部结构** —— 只确定了边界（所以往返保真），
   内部字段没拆。这是"从工程 json 全新生成 `.sty`"的最后一块拼图。
4. **`.res` / `.str` 的写入** —— 读的规范齐了（固件里就有解析器），
   写入侧（含 RLE / QuickLZ 压缩）没实现。

### 复现方法

所有结论都能重跑：

```bash
cd re
python moc_scan.py    <exe> uitools_meta.json   # 类结构
python report.py      uitools_meta.json full    # 可读接口清单
python strlit_scan.py <exe> qstr.txt            # QStringLiteral（控件名/界面文案）
python qrc3.py        <exe> 0x0109d740 out/     # qrc 图标
python sty_dump.py      ../../../JL/JL.sty      # .sty 结构
python sty_roundtrip.py ../../../JL/JL.sty      # .sty 往返校验
python hash_brute2.py <ename.h>                 # ID 哈希暴搜（当前全未命中）
```
