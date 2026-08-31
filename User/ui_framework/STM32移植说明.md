# 点阵屏 UI 框架 —— STM32F407 移植说明

从 JL703N/BR27 移植到 STM32F407VGT6 + FreeRTOS + FATFS。

**当前状态**：47 个 `.c` 全部参与编译，Keil AC6 构建 **0 error / 0 warning**，
链接期符号全部闭合。资源文件尚未放入，未上板实测。

```
Program Size: Code=124720  RO-data=9844  RW-data=820  ZI-data=93084
Total RO Size (Code + RO Data)  134564 (131.41 kB)
```

---

## 1. 目录结构：三个「换平台只改这里」的落点

移植后新增两个目录，框架原有的 `platform/ ui_dot/ font/ res/ ui_draw/ lcd_drive/`
只做了少量必要修改（见第 5 节）。

```
User/ui_framework/
├── port/                       ← 【新增】平台适配层
│   ├── ui_port_config.h        ★ 配置总入口：裁剪开关 / 屏参 / 引脚 / 资源路径
│   ├── ui_style.h              ★ 应用侧窗口 ID（要与资源文件对齐，见第 4 节）
│   ├── ui_port.h                 硬件桥接声明（框架侧看到的 spi_*/gpio_*）
│   ├── ui_port_shim.c            上述声明 → ui_hal_* 的薄封装（无芯片代码）
│   ├── ui_port_fs_fatfs.c      ◆ 文件系统：resfile_* → FATFS
│   ├── ui_port_log.c             日志 / 断言落点
│   ├── ui_port_misc.c            CRC16 / ASCII 工具 / 哈希
│   ├── ui_port_registry.c        显式注册表（控件 / 风格 / 推屏接口）
│   ├── ui_port_stubs.c           本移植不实现的功能（音箱业务 / flash 直存）
│   └── hal/
│       ├── ui_hal.h            ● 硬件抽象接口（6 个函数）
│       └── ui_hal_stm32f4.c    ● STM32F407 实现：SPI3 + DMA1_Stream5 + GPIO
└── compat/                     ← 【新增】杰理 SDK 头文件的等价物（13 个扁平头）
    ├── jl_typedef.h  jl_list.h  jl_rect.h  jl_ascii.h  jl_math.h  jl_crc.h
    ├── jl_fs.h       ◆ 文件系统抽象接口（唯一的存储边界）
    ├── jl_os_api.h     纯转发头 → FreeRTOS/task_manager.h
    ├── jl_debug.h      日志 / ASSERT
    ├── jl_lcd_drive.h  屏驱结构体与推屏接口
    ├── jl_ui_api.h     对外 API（应用层用这个）
    ├── jl_res_config.h 资源路径
    └── jl_app_stub.h   音箱业务侧占位
```

**三个换平台落点**：

| 换什么 | 只改 | 工作量 |
| --- | --- | --- |
| ● 换 MCU | `hal/ui_hal_<新芯片>.c` + `ui_port_config.h` 的引脚 | 6 个函数 |
| ◆ 换 RTOS | `FreeRTOS/task_manager.{h,c}`（不在本目录） | ~10 个函数 |
| ◆ 换文件系统 | `ui_port_fs_fatfs.c` | 10 个函数 |

`compat/` 是扁平目录，不复刻杰理 SDK 的路径形状——框架源码的 `#include`
已改为直接引用这些扁平名。

---

## 2. 硬件抽象层（`hal/ui_hal.h`）

整个框架与芯片之间只有这 6 个函数：

| 分类 | 接口 |
| --- | --- |
| SPI | `ui_hal_spi_init` / `send_byte` / `send_block` / `wait_done` |
| GPIO | `ui_hal_pin_write` |
| CPU | `ui_hal_memory_barrier` |

CMSIS / HAL 库的依赖**全部收敛在 `ui_hal_stm32f4.c` 一个文件**，框架代码与
`port/` 其余文件都不含芯片头。

关中断 / 临界区 / 延时 / 喂狗 / `zalloc` **有意不在 HAL 里** —— 它们是系统服务
而不是显示外设，统一由 `FreeRTOS/task_manager.{h,c}` 提供。这样换 MCU 时不会
顺带把 OS 代码拖过去，换 RTOS 时也只动 task_manager。

### 本板接线（改 `ui_port_config.h`）

| 信号 | 引脚 | 说明 |
| --- | --- | --- |
| SCK | PB3 | SPI3，CubeMX 已配 |
| MOSI | PB5 | SPI3 |
| CS | PB6 | 可改 |
| DC | PB7 | 低=命令 高=数据，可改 |
| RST | PB8 | 可改 |
| DMA | DMA1_Stream5 Ch0 | Stream3/4 已被 I2S2 占用 |

> SSD1306 自发光，无背光脚，`UI_PORT_PIN_BL_ENABLE = 0`。

---

## 3. 关键决策与原因

### 3.1 注册表：段收集 → 显式表

原厂靠 GNU ld 收集 `.control_ops` / `.ui_style` / `.lcd_if_info` 三个段。
本移植改为 `port/ui_port_registry.c` 里的三张显式表，原因：

1. armlink 没有 GNU ld 的 `PROVIDE`，造不出段边界符号；
2. **段收集漏一项是静默故障** —— 编译链接全过，只是界面某类元素整块不显示；
   显式表漏了是编译期未定义符号；
3. 换编译器 / 链接器不用再改一次。

代价：新增控件时必须往 `g_control_ops_table[]` 加一行。

### 3.2 CRC16：实测反推，不是猜的

`res/resfile.c` 用 `CRC16()` 校验 `.res` 文件里存的 `head_crc`，算法不一致
则每张图都加载失败（界面全空）。原厂 CRC16 实现在预编译库里，源码不可得。

做法：拿 703 SDK 的 `cpu/br27/tools/{JL_OLED,JL,ui_resource}/JL.res`
共 **892 个 `RES_BMP_T` 表项**逐个比对，暴力搜索参数空间，得到唯一匹配：

> **CRC-16/XMODEM**：多项式 `0x1021`，初值 `0x0000`，无输入/输出反转，MSB first
> —— 892 全中，0 失败。

若换了资源打包工具版本，建议重新验一次。

### 3.3 文件系统：单层，不叠封装

`jl_fs.h` 声明 10 个函数，`ui_port_fs_fatfs.c` 直接实现在 FATFS 上。
没有 `resfile → jl_fs → FATFS` 这种中间层。句柄用**静态池**（8 个）而非
`malloc`，池满会打印告警，句柄泄漏在调试期就暴露。

### 3.4 消息队列：修好了工程 API，而不是包一层

工程 `task_manager.h` 原来把消息头塞进一个字:
`header = (argc & Q_ARGC_MASK) | (type & Q_TYPE_MASK)` —— **type 只保留高 12 位**。
凡是用小整数当消息类型的代码（703 UI 框架的 `UI_MSG_*` 取值 0~7 就是）都会被
整个抹掉，接收方永远看到 type == 0，**每条消息都派发错**。

处理方式是**改工程自己的 API**，不是在旁边加一层 `_jl` 包装：

- 消息头改成**两个字**：`[w0 = type][w1 = argc]`，type 独占一个字（完整 32 位）；
- `os_taskq_pend` 返回时 `argv[0]` 就是**纯 type**，参数从 `argv[1]` 开始
  —— 与杰理 `os_taskq_pend` 的布局一致，框架的 `switch (msg[0])` 直接可用；
- `Q_MSG` / `Q_CALLBACK` 这类高位常量照旧，`Q_TYPE(argv[0])` 仍然成立，
  所以 `app_core.c` 的 `Q_TYPE(msg[0]) != Q_MSG` 不用改；
- `Q_ARGC(argv[0])` 随之失效并移除（参数个数只在队列内部使用）。

顺带修掉的两处：

- `os_taskq_post_msg` / `post_msg_front` / `post_type` **原先各自拼了一遍消息头**，
  现在统一走 `taskq_send_msg()` 一个静态函数，格式只有一处定义；
- `os_taskq_post_msg_front` 原实现用 `malloc` 存参数数组，而它的注释说明可在
  **中断里调用** —— 中断里 malloc 是不该做的。改成栈上定长数组
  （`TASKQ_ARGC_MAX = 16`）。

框架侧只剩 1 处需要适配：`post_ui_msg` 的返回值极性
（工程 `pdPASS(1)`=成功，杰理 `0`=成功），一行三目运算解决。

## 4. 还没做完的事

### 4.1 资源文件（阻塞项）

框架需要以下文件放进内部 Flash 的 FATFS：

```
0:/ui/JL/JL.res     图片资源
0:/ui/JL/JL.str     字符串图片
0:/ui/JL/JL.sty     窗口/控件布局
0:/ui/font/...      字库（.PIX / .TAB）
```

可从 703 SDK 直接取：`cpu/br27/tools/JL_OLED/*`、`cpu/br27/tools/font/*`
（`JL_OLED` 就是 128×64 单色屏那一套）。
路径根在 `ui_port_config.h` 的 `UI_PORT_RES_ROOT`。

注意 FATFS 当前 `_USE_LFN = 0`（只支持 8.3 短文件名），上面的路径都合规。

### 4.2 应用侧 UI 风格（`port/ui_style.h` + 风格 .c）

现在 `g_ui_style_table[]` 是**空表**。后果是明确的：界面能画出来，但
所有控件都没有事件回调——静态显示正常，按键/刷新不响应。

补齐步骤：
1. 用资源工具导出的窗口/控件 ID 替换 `port/ui_style.h` 里的占位值
   （`ID_WINDOW_BT` / `ID_WINDOW_MUSIC` / `ID_WINDOW_VMENU` 现在是猜的）；
2. 新建风格 .c，用 `UI_STYLE_HANDLERS_BEGIN/END`（见 `include/ui/ui_core.h`）
   写事件表；
3. 在 `ui_port_registry.c` 的 `g_ui_style_table[]` 里加一行。

### 4.3 上板实测

以下路径**只过了编译，没上板验证**：
- SSD1306 初始化命令序列与 SPI 时序
- DMA 推屏（8 个 page × 128 字节）
- FATFS 资源读取
- UI 任务与消息队列联通

### 4.4 `RAM_Debug` 目标装不下

该目标的 scatter 把代码+数据全放进 `0x20000000` 起的 112KB SRAM，
而现在代码就 125KB。

> **这是移植前就存在的问题**：已用 `git stash` 回退到移植前实测，
> 同样报 432 个 `L6406E: No space in execution regions`。
> 不是本次引入的。

要用该目标调试的话：把 UI 那 7 个分组从 `RAM_Debug` 目标里排除，
或在该目标下置 `TCFG_UI_ENABLE 0`。日常用 `STM32F407VGT6_Template`
（FLASH_RELEASE）目标即可。

---

## 5. 改动了框架源码的地方

移植原则是尽量不动框架，但下面这些是**必须改**的。

### 5.1 真 Bug：`char` 符号性（最重要）

pi32 的 `char` 有符号，**ARM ABI 下 `char` 无符号**。框架里有 4 个字段
用 `-1` 当哨兵值：

| 位置 | 原类型 | 后果（ARM 上） |
| --- | --- | --- |
| `ui_grid.h` `hi_index` | `char` | `if (hi_index >= 0)` **恒为真** → `item[255]` **数组越界** |
| `ui_grid.h` `touch_index` | `char` | 同上 |
| `ui_grid.h` `onfocus` | `char` | `if (onfocus != -1)` 恒为真 → 高亮逻辑反了 |
| `control.h` `highlight_index` | `char` | `== -1` 恒为假 |

**根因**在工程 `Core/Inc/typedef.h`：`typedef char s8;` —— `s8` 这个名字
意味着有符号，但裸 `char` 在 ARM 上是无符号的。这同时让
`ui_slider.c` 的限幅代码失效（`s8 sub = persent - step; (sub > 0) ? sub : 0`
下溢后变成 250，限幅形同虚设）。

处理：
- `Core/Inc/typedef.h`：`typedef char s8` → `typedef signed char s8`
  （全工程 `s8` 的使用点只有 UI 框架那 8 处，改动无副作用）；
- 上面 4 个字段 `char` → `s8`。宽度仍是 1 字节，**结构体布局不变**
  （这些结构体与资源文件二进制绑定）。

> 编译器只警告了其中 2 处，`hi_index >= 0` 那处是**静默**的。

### 5.2 pi32 专有内联汇编

| 原代码 | 位置 | 替换为 |
| --- | --- | --- |
| `asm("csync")` | `ui_pushScreen_manager.c` | `ui_hal_memory_barrier()` |
| `%0 = rets` | `lcd_ui_api.c` ×3 | `__builtin_return_address(0)` |
| `%0 = cnum` | `ui_core_api.c` ×4 / `ui_core_dot.c` ×2 | 常量 `0`（单核） |
| `always_inline_when_const_args` | `ui_synthesis_oled.c` ×2 | 删除（armclang 不识别） |

### 5.3 伪造 `va_list`

框架用 `(void *)&msg[2]` 伪造一个 `va_list` 传给 `ui_message_handler()`。
pi32 的 `va_list` 是裸指针所以能跑；**ARM AAPCS 的 `va_list` 是结构体**，
既编不过也不可能对。

改为显式的 `const int *` 参数数组，`va_arg(argptr, int)` → `*argptr++`。
语义完全一致（参数本来都是 int 宽度）。涉及 `ui.h`、`lcd_ui_api.c` 的
`do_msg_handler` 与 `ui_message_handler`。

### 5.4 `FILE` 与 `RESFILE` 合并

框架的 font / lyrics 层用杰理的 `FILE` 类型，与 stdio 的 `FILE` 冲突。
框架自己的注释已写明「两种句柄在这里被当同一个指针用」，
故统一改名为 `RESFILE`，少一个类型。

### 5.5 `font_sd_fclose` 的双重关闭

原框架特意保留了 `resfile_close()` 之后那次多余的 `fclose()`，理由是
「`resfile_*` 闭源，无从确认是否已关掉底层句柄」。

**本移植已去掉**：`resfile_*` 现在由 `ui_port_fs_fatfs.c` 实现，不再是黑盒
—— `resfile_close()` 已经 `f_close` 并释放了句柄池槽位。再调 stdio 的
`fclose()` 会把「指向句柄池结构体的指针」当 `FILE*` 用，是明确的未定义行为。
歧义消失，保留反而是错的。

### 5.6 头文件自包含 / include 路径

- 全部指向杰理 SDK 的 `#include` 改为 `compat/` 的扁平名；
- 13 个文件补 `#include "jl_debug.h"`（用了 `ASSERT` 却靠别处间接带入）；
- `ui_core.h` 补 `jl_list.h` / `jl_fs.h`（直接用了 `struct list_head`
  与 `struct vfs_attr`，原厂靠 .c 里先 include `system/includes.h` 才凑巧编过）；
- `ui_core_dot.c` 补 `ui_core_redraw_old` 前置声明（C99 起隐式声明是告警）；
- `jl_list.h` 的 `typeof` → `__typeof__`（Keil 是 C99 严格模式，`uGnu=0`）。

### 5.7 踩过的坑：头文件卫哨撞名

`jl_list.h` 原卫哨是 `LIST_H`，**与 FreeRTOS 的 `list.h` 相同** ——
`jl_os_api.h` 先拉进 `FreeRTOS.h` 定义了 `LIST_H`，导致 `jl_list.h`
整个被跳过，报一堆 `struct list_head` incomplete。
同类风险还有 `jl_math.h` 的 `__MATH_H__`（撞标准 `<math.h>`）。
已统一规范为 `__JL_XXX_H__`。

---

## 6. ⚠ Flash 分区：中转扇区与代码区重叠

`MDK-ARM/FLASH_RELEASE/STM32F407_FLASH_RELEASE.sct` 现在把代码区设为 **256KB**：

```
LR_IROM1 0x08000000 0x0040000      ; 0x08000000 - 0x0803FFFF (S0..S5)
```

而 `User/fs/flash_disk.h` 目前是：

```c
#define FLASH_DISK_READONLY     (0)             /* 读写模式 */
#define FLASH_DISK_SWAP_ADDR    (0x08020000UL)  /* 中转扇区 S5 */
#define FLASH_DISK_SWAP_ENABLE  (1)
#define FLASH_DISK_BASE_ADDR    (0x08040000UL)  /* 磁盘 S6..S7 */
```

对照一下：

| 区域 | 地址范围 | 状态 |
| --- | --- | --- |
| 代码区 | `0x08000000 – 0x0803FFFF` | 当前占用 131.4KB / 256KB |
| **中转扇区** | `0x08020000 – 0x0803FFFF` | ⚠ **落在代码区内部** |
| 磁盘区 | `0x08040000 – 0x0807FFFF` | 不冲突 |

`flash_disk.h` 自己的注释也写明了这两种搭配：

```
FLASH_DISK_READONLY = 1: 代码 S0..S5(256KB) | 磁盘 S6..S7(256KB)
FLASH_DISK_READONLY = 0: 代码 S0..S4(128KB) | 中转 S5 | 磁盘 S6..S7
```

即 **256KB 代码区对应的是 `READONLY = 1`**。现在是 256KB + `READONLY = 0`，
一旦真的往 FAT 磁盘写入，`FlashDisk_Write()` 会擦写 `0x08020000` ——
**那是正在运行的固件所在的扇区**。

> 实际运行中 UI 框架只读资源（`resfile_open` 一律用 `FA_READ`，
> `resfile_write` 直接返回不支持），所以现在**不会**触发。
> 但这是个一旦有人调 `f_write` 就变砖的隐患。

两种收尾方式，取其一：

1. **改成只读磁盘（推荐，且匹配当前 scatter）**
   `flash_disk.h` 置 `FLASH_DISK_READONLY = 1`。
   资源本来就是离线做好的 FAT 镜像烧进去、运行期只读，正好对上；
   顺带省掉 128KB 中转扇区和擦写寿命消耗。

2. **保留读写磁盘**
   则代码区必须退回 128KB（scatter 改回 `0x0020000`）。
   但当前代码已 131.4KB，**装不下** —— 需要先裁功能，可裁的部分：

   | 模块 | 占用 |
   | --- | --- |
   | `ui_grid.o`（九宫格控件） | 15.0 KB |
   | `lyrics.o`（歌词，本移植 flash 直存已 stub） | 4.1 KB |
   | `font_other_language.o`（泰语等，已 stub） | 4.2 KB |
   | `ui_rotate.o` | 3.6 KB |
   | `ui_circle.o` | 2.5 KB |
   | `font_sjis.o` / `font_ksc.o` / `font_big5.o`（日/韩/繁） | 4.0 KB |
   | 合计可裁 | **约 33 KB** |


---

## 7. 编译与自检

### Keil（正式路径）

```
keil_make.bat rebuild STM32F407VGT6_Template
```

或直接 UV4：

```
D:\Keil_v5\UV4\UV4.exe -j100 -r -t STM32F407VGT6_Template MDK-ARM\STM32F407VGT6_Template.uvprojx -l build_log.txt
```

### 单文件快速自检（不开 IDE）

`tools/ui_build_check.sh` 直接调 armclang 逐个编译，改一个文件时比全量快：

```sh
sh tools/ui_build_check.sh                              # 全部 47 个
sh tools/ui_build_check.sh User/ui_framework/port/ui_port_os.c   # 指定文件
```

### 链接期符号闭合检查

```sh
cd ../.ui_objs
"D:/Keil_v5/ARM/ARMCLANG/bin/armlink.exe" --cpu=Cortex-M4.fp.sp *.o -o /dev/null 2>&1 \
  | grep -oE 'Undefined symbol [A-Za-z_][A-Za-z0-9_]*' | sort -u
```

正常结果：只剩 41 个由工程本身提供的符号（`HAL_*` / `f_*` / FreeRTOS / RTT），
**不应出现任何杰理残留符号**。

> ⚠ 注意 `tools/ui_build_check.sh` 默认走 armclang 的 gnu 模式，比 Keil 宽松。
> 它通过**不代表** Keil 能过（`typeof` 那次就是这么漏掉的）。以 Keil 结果为准。

---

---

## 9. ⚠ CubeMX 重新生成后必须做的事

本工程 `.ioc` 里 `ProjectManager.TargetToolchain = MDK-ARM V5.32`，CubeMX
**会重写 `MDK-ARM/*.uvprojx`**。移植过程中对工程做的改动分三类：

### 9.1 会被冲掉 —— 必须重做

| 内容 | 恢复办法 |
| --- | --- |
| Keil 工程里 7 个 `UI/*` 分组（45 个 `.c`）| `python tools/ui_add_to_keil.py` |
| 5 条 UI 头文件搜索路径 | 同上（脚本一次搞定，幂等可重跑）|

漏做的表现：编译一堆 `file not found`，或链接缺一大片 UI 符号。

### 9.2 已固化进 `.ioc` —— 不会丢

| 内容 | `.ioc` 里的固化项 |
| --- | --- |
| `_FS_LOCK = 8`（FatFs 同时打开文件数）| `FATFS._FS_LOCK=8` + 加进了 `FATFS.IPParameters` |
| `_USE_LFN = 0`、`_CODE_PAGE = 936` | 原本就在 `.ioc` 里 |

> `FATFS/Target/ffconf.h` 是 CubeMX 生成的，直接改会被覆盖 ——
> 所以值必须同时写进 `.ioc` 的 `FATFS.IPParameters` 列表里才算固化。

### 9.3 不在 CubeMX 管理范围 —— 安全

- `FreeRTOS/`（`task_manager.*`、`FreeRTOSConfig.h`）—— `.ioc` 里没有
  `FREERTOS` 段，这套 FreeRTOS 是手工加的，CubeMX 不碰
- `Core/Inc/typedef.h` —— 自定义文件，不是 CubeMX 生成的那批
  （生成的是 `main.h` / `gpio.h` / `spi.h` / `dma.h` / `i2s.h` / `usart.h` /
  `stm32f4xx_hal_conf.h` / `stm32f4xx_it.h`）
- `User/` 下全部内容，包括 `User/ui_framework/`
- `MDK-ARM/FLASH_RELEASE/*.sct` —— 只在 Keil 里勾了「用目标对话框的内存布局」
  时才会被 uVision 重新生成；本工程用的是自定义 scatter

### 9.4 `.ioc` 管理的外设

`DMA FATFS I2S2 NVIC RCC SPI3 SYS USART1 USB_DEVICE USB_OTG_FS`

⚠ UI 用的 **DMA1_Stream5**（SPI3_TX 推屏）**不是** CubeMX 配的 ——
它在 `port/hal/ui_hal_stm32f4.c` 的 `Ui_Hal_SpiDmaInit()` 里手工初始化，
中断入口 `DMA1_Stream5_IRQHandler` 也定义在那个文件里。

所以在 CubeMX 里**不要**给 SPI3 加 DMA 请求：加了 CubeMX 会在
`Core/Src/dma.c` 里生成一份 `hdma_spi3_tx`，和 HAL 层那份冲突
（两个 `__HAL_LINKDMA` 抢同一个 `hspi3.hdmatx`，以及重复的 IRQHandler 定义）。


---

## 10. 对照原 `移植接口清单.md`

原清单列的 69 个外部依赖，落点如下：

| 原清单分类 | 落点 |
| --- | --- |
| OS / 任务调度（14） | 全部在 `FreeRTOS/task_manager.{h,c}`（含新增的杰理兼容桥接层）|
| 内存（3） | `malloc/free` 用 FreeRTOS heap；`zalloc` 在 `task_manager.c` |
| libc（8） | Keil 标准库 |
| 定时器 / 看门狗（5） | `sys_timer_*` 现成；`wdt_clear/wdt_clr/delay_2ms` 在 `task_manager.c` |
| **硬件适配层（8）** | `ui_port_shim.c` → `hal/ui_hal_stm32f4.c` |
| 资源文件 VFS（8） | `ui_port_fs_fatfs.c` |
| Flash 直存（4） | `ui_port_stubs.c`，恒返回失败（不做歌词索引回写） |
| 段收集符号（6） | 改为 `ui_port_registry.c` 的显式表 |
| 原厂就缺定义的（4） | `ui_port_stubs.c` 补齐（泰语 3 个 + `norflash_hardware_read_watch`） |

原清单第 8 节提到的 4 个「原厂就缺定义」符号，在 703 上靠 LTO 丢弃才链接得过；
Keil 不做那种激进消除，所以必须补真实符号，已在 `ui_port_stubs.c` 实现。
