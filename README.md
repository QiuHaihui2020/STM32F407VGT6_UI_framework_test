# 可移植点阵屏 UI 框架 —— STM32 验证工程

参考杰理（JieLi）的 UI 框架重写的一套可移植点阵屏 UI 框架（`User/ui_framework/`）。
杰理的框架闭源，代码全部重写；参考它是因为它有配套 GUI 编辑器（`ui-tools.exe` 画界面、
`ResBuilder.exe` 出资源），要复用这套工具链就得认它的资源格式（`.sty`/`.res`/`.str`）
和控件模型。即：**格式和结构对齐，代码重写。**

本工程是它的第一个宿主平台：STM32F407 + SSD1306 128×64 单色 OLED，SPI + DMA 推屏，
资源放片内 Flash 模拟的 FAT 盘。

---

## 1. 硬件

### 目标芯片

| 项     | 值                                                                     |
| ------ | ---------------------------------------------------------------------- |
| MCU    | **STM32F407VET6**（LQFP100，512KB Flash，128KB SRAM + 64KB CCM） |
| 主频   | 168MHz（HSE 8MHz，PLLM=8 / PLLN=336 / PLLP=2，FLASH_LATENCY_5）        |
| 调试器 | J-Link / ST-Link                                                       |
| 日志   | **串口 USART1 = PA9**，波特率 **1000000**                  |
| USB    | **USB_OTG_FS：PA11 = DM，PA12 = DP**（资源盘走这里）             |

### OLED 接线

屏：SSD1306 128×64，4 线 SPI（自发光，**无背光脚**）。

| 信号      | 引脚          | 说明                                       |
| --------- | ------------- | ------------------------------------------ |
| SCK       | **PB3** | SPI3_SCK                                   |
| MOSI      | **PB5** | SPI3_MOSI（屏的 DIN/SDA）                  |
| CS        | **PB6** | 片选，低有效                               |
| DC        | **PB7** | 命令/数据选择，低 = 命令，高 = 数据        |
| RST       | **PB8** | 复位，低有效                               |
| MISO      | PB4           | **单色屏不用**，但 CubeMX 已占用该脚 |
| VCC / GND | —            | 3.3V                                       |

### SPI / DMA 参数

| 项       | 值                                                                    |
| -------- | --------------------------------------------------------------------- |
| 实例     | SPI3（APB1 = 42MHz）                                                  |
| 速率     | Prescaler = 2 →**21MHz**                                       |
| 模式     | Mode 0（CPOL = Low，CPHA = 1Edge），8bit，MSB first                   |
| DMA      | **DMA1_Stream5 / Channel 0**，Memory→Periph，Byte 对齐，Normal |
| DMA 中断 | `DMA1_Stream5_IRQn`，抢占优先级 6                                   |
| 超时保护 | 100ms                                                                 |

> DMA1_Stream3/4 已被 I2S2 占用，所以选 Stream5。
> 《RM0090》Table 42：SPI3_TX = DMA1 Stream5 Ch0 或 Stream7 Ch0。
>
> `DMA1_Stream5_IRQHandler` 定义在 `User/ui_framework/port/bsp/stm32f4/ui_lcd_stm32f4.c` 里，
> 靠启动文件的弱符号接住。**若哪天用 CubeMX 勾上 SPI3 的 DMA**，
> `Core/Src/stm32f4xx_it.c` 会生成同名函数 → 重复定义链接错误，删掉本层那个即可。

### 串口与 USB 接线

| 用途     | 引脚           | 说明                           |
| -------- | -------------- | ------------------------------ |
| 日志输出 | **PA9**  | USART1_TX，AF7。波特率 1000000 |
| USB D−  | **PA11** | USB_OTG_FS_DM，AF10            |
| USB D+   | **PA12** | USB_OTG_FS_DP，AF10            |

USB 是 **Full Speed 设备模式**，`vbus_sensing_enable = DISABLE`（不做 VBUS 检测），
所以只接 D+/D− 和 GND 即可，不需要额外的 VBUS 检测脚。

---

**改接线只改一个文件**：`User/ui_framework/port/bsp/stm32f4/ui_board_pins.h`
（引脚）和同目录的 `ui_board_stm32f4.h`（SPI/DMA 实例）。框架层不持有任何引脚。

---

## 2. 编译与运行

### 编译

两种方式，产物相同。

#### 方式一：Keil GUI

打开 `MDK-ARM/STM32F407VGT6_Template.uvprojx`，选好使用keil界面编译工程。

工程里有两个 target（左上角下拉框切换）：

| target                     | 用途                                              |
| -------------------------- | ------------------------------------------------- |
| `STM32F407VGT6_Template` | **默认**，输出到 `MDK-ARM/FLASH_RELEASE/` |

#### 方式二：命令行脚本

`keil_make.bat` 调 Keil 的 `UV4.exe` 做无界面编译，适合改完直接看结果、或接 CI：

```
keil_make.bat                        # 增量编译，默认 target STM32F407VGT6_Template
keil_make.bat rebuild                # 全量重编
```

用法：`keil_make.bat [build|rebuild] [target 名]`，两个参数都可省略（默认值在脚本顶部）。

需要注意：

- 脚本里的 `set UV=D:\Keil_v5\UV4\UV4.exe` 要按你的 Keil 安装路径改
- 工程文件靠 `dir /s /b *.uvprojx` 在当前目录树里自动找第一个，所以**必须在
  `STM32F407VGT6_Template/` 目录下运行**
- 编译日志写到 `build_log.txt`（每次覆盖，脚本结尾会 `type` 出来）

当前基线：`Code=109556  RO-data=10440  RW-data=820  ZI-data=93324`，0 Error 0 Warning。

### 看日志

日志走 **串口**

| 项     | 值                                                                  |
| ------ | ------------------------------------------------------------------- |
| 引脚   | **PA9 = USART1_TX**（PA10 = RX，AF7）                         |
| 波特率 | **1000000（1Mbps）** —— 不是常见的 115200，串口工具要手动填 |

两个上板排查开关在 `User/ui_framework/config/ui_port_config.h`：

| 宏                       | 作用                                                              |
| ------------------------ | ----------------------------------------------------------------- |
| `UI_PORT_FS_DUMP_TREE` | 挂载成功后打印 FAT 盘的实际目录树 —— 排查"资源找不到"第一个看它 |
| `UI_PORT_PUSH_TRACE`   | 每推一帧打一行`len` / 非零字节数，用来区分三种"黑屏"            |

`UI_PORT_PUSH_TRACE` 的三种情况：没打印 = 绘制流水线没走到推屏；
`nonzero=0` = 推屏了但帧缓冲全 0（控件没画出东西）；`nonzero>0` = 数据已发出（问题在面板初始化/接线/SPI 时序）。

### 启动流程

```
main()
 ├─ MX_GPIO/DMA/SPI3/USB_DEVICE/FATFS_Init()      CubeMX 生成
 └─ app_core_init()                                User/apps/app_core.c
     ├─ task_create(app_core_function, "app_core")
     └─ os_start()                                 FreeRTOS 起调度
          └─ app_core_function()
              └─ app_ui_init() → UI_INIT(&ui_cfg_data) = lcd_ui_init()
                   └─ 内部 task_create("ui") 并等它的启动信号量
```

UI 任务在 `FreeRTOS/task_manager.c` 的注册表里：`{"ui", 优先级 4, 栈 1024 字, 队列 32}`。
任务名**必须**是 `"ui"`，框架按名字找它。

---

## 3. UI 资源制作

### 工具位置

`tools/LCD_UI工程/`

| 路径                         | 用途                                                           |
| ---------------------------- | -------------------------------------------------------------- |
| `UITools/ui-tools.exe`     | UI 绘图工具（画界面、摆控件）                                  |
| `UITools/ResBuilder.exe`   | 资源生成工具（导出 .sty/.res/.str + id 头）                    |
| `字库工具/`                | 生成`F_GB2312.PIX/.TAB`、`ascii.res`                       |
| `doc/*.pdf`                | 官方说明：UI 布局工具使用说明 / ui 配置说明 / 工具快速使用说明 |
| `ui_128_64_JL02/模式界面/` | **本工程的 UI 工程文件**（128×64，JL02 风格）           |

### 制作流程

在 `tools/LCD_UI工程/ui_128_64_JL02/模式界面/` 下按顺序点：

```
step1-打开UI绘图工具.bat      画界面 → 存盘生成 project/project.bin
step2-打开UI资源生成工具.bat  生成资源 → 自动调 project/copy_file.bat
打开图片资源文件夹.bat        放素材图
```

`project/copy_file.bat` 会把生成物分发到工程里：

| 生成物          | 去处                                              | 说明                                            |
| --------------- | ------------------------------------------------- | ----------------------------------------------- |
| `project.bin` | `tools/JL/JL.sty`                               | 窗口/控件布局                                   |
| `result.bin`  | `tools/JL/JL.res`                               | 图片资源                                        |
| `result.str`  | `tools/JL/JL.str`                               | 字符串图片                                      |
| `ename.h`     | `User/ui_framework/include/common/style_jl02.h` | 控件/窗口 id 哈希表，**生成文件，别手改** |

`style_jl02.h` 里是 `PAGE_0..PAGE_10` 和每个控件的哈希 id，
`include/common/ui_style.h` include 它并映射成 `ID_WINDOW_*` 这类名字。

> `copy_file.bat` 必须保持**纯 ASCII** —— cmd.exe 按系统 ANSI 代码页读 `.bat`，
> UTF-8 中文注释会乱码到破坏解析。

---

## 4. 把资源放进片内 Flash

资源盘是**片内 Flash 模拟的 FAT 盘**，通过 **USB MSC** 挂到 PC 上直接拖文件。

### Flash 分区（VET6 512KB）

配置在 `User/fs/flash_disk.h`：

| 扇区   | 地址                           | 大小            | 用途                                                   |
| ------ | ------------------------------ | --------------- | ------------------------------------------------------ |
| S0..S4 | `0x08000000`..`0x0801FFFF` | 128KB           | 程序代码                                               |
| S5     | `0x08020000`..`0x0803FFFF` | 128KB           | FAT 写入**中转扇区**（`FLASH_DISK_SWAP_ADDR`） |
| S6..S7 | `0x08040000`..`0x0807FFFF` | **256KB** | **FAT 资源盘**（`FLASH_DISK_BASE_ADDR`）       |

扇区大小 512 字节（`FLASH_DISK_SECTOR_SIZE`，必须与 `ffconf.h` 的 `_MAX_SS` 一致，
有 `#error` 卡住）。当前是**读写模式**（`FLASH_DISK_READONLY = 0`），
所以需要中转扇区：Flash 只能按扇区擦，改一个 512B 逻辑扇区得先把整个物理扇区读到中转区。

### 操作步骤

1. 烧固件，插 USB（**USB_OTG_FS：PA11 = D−，PA12 = D+**）
2. PC 上出现一个约 **256KB 的 U 盘**（首次可能提示需要格式化 → 格成 FAT，簇大小用默认）
3. 按下面的布局把文件拷进去
4. 弹出 U 盘、复位单板

### 盘上必须的布局

```
0:/JL/JL.res          <- tools/JL/JL.res
0:/JL/JL.str          <- tools/JL/JL.str
0:/JL/JL.sty          <- tools/JL/JL.sty
0:/font/ascii.res     <- tools/font/ascii.res
0:/font/F_GB2312.PIX  <- tools/font/F_GB2312.PIX   (需要中文时)
0:/font/F_GB2312.TAB  <- tools/font/F_GB2312.TAB   (需要中文时)
```

> ⚠ **`UI_PORT_RES_ROOT` 是盘根 `"0:"`，不能多一层目录。**
> 框架自己拼 `RES_PATH"JL/JL.res"`，写成 `"0:/ui"` 会变成 `0:/ui/JL/JL.res`，
> 与盘上对不上 → `resfile_open` 全部失败、界面全黑。
>
> `_USE_LFN = 0`，所以盘上是 8.3 短文件名（全大写）。
> 路径对不上时打开 `UI_PORT_FS_DUMP_TREE` 看实际目录树，别靠猜。

### USB 配置

复合设备（`USE_USBD_COMPOSITE`），当前**只启用 MSC**：

```
USB_DEVICE/Target/usbd_conf.h
    USBD_MSC_CMPSIT_ENABLE    1     <- 启用
    USBD_CDC_CMPSIT_ENABLE    0
    USBD_AUDIO_CMPSIT_ENABLE  0
```

MSC 的存储后端是同一个 `User/fs/flash_disk.c`（`USB_DEVICE/App/usbd_storage_if.c` 调它），
所以 USB 看到的盘和 FatFs 看到的是同一片 Flash。

> ⚠ **别在 USB 连着写盘的同时让 UI 读资源** —— 两边都在动同一片 Flash，
> 而 Flash 擦写期间 CPU 取指会停顿。改资源时的正确顺序是：拷完 → 弹出 → 复位。

---

## 5. 软件依赖

| 组件                         | 版本 / 说明                                                                                                     |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------- |
| **FreeRTOS**           | V9.0.0。tick 1000Hz，堆 64KB（`heap_4`），最大优先级 32                                                       |
| **FatFs**              | Middlewares 里的 ChaN FatFs。`_MAX_SS = 512`，`_USE_LFN = 0`，`_FS_LOCK` 需与 `UI_RES_MAX_OPEN`(8) 一致 |
| **STM32 HAL**          | STM32F4xx HAL Driver（CubeMX 生成）                                                                             |
| **USB Device Library** | ST 的 STM32_USB_Device_Library + CompositeBuilder，MSC 类                                                       |
| **串口日志**           | USART1 (PA9) @ 1000000，DMA2_Stream7 发送。`RTT/log_debug.c` 里重定向 `printf`                              |
| 编译器                       | Keil MDK-ARM，ArmClang（AC6），C99，优化`-O3`（`Optim 7`）                                                  |

预定义宏：`USE_HAL_DRIVER, STM32F407xx, USE_USBD_COMPOSITE`

### 框架对外的依赖面

UI 框架不直接碰 HAL，它只要求宿主工程提供三样东西：

| 依赖               | 由谁提供                    | 接口                                                                    |
| ------------------ | --------------------------- | ----------------------------------------------------------------------- |
| **屏硬件**   | `port/bsp/stm32f4/`       | `port/bsp/ui_lcd_if.h` —— 控制线拉高拉低 + SPI 数据通道             |
| **资源存储** | `port/res/fatfs/`         | `port/res/ui_res_backend.h` —— 8 个后端函数                         |
| **OS 服务**  | `FreeRTOS/task_manager.c` | `port/rtos/jl_os_api.h` —— 任务/队列/信号量/临界区/延时/喂狗/zalloc |

---

## 6. 资源占用

数据来自 `MDK-ARM/FLASH_RELEASE/STM32F407_FLASH_RELEASE.map`（ArmClang，`-O3`），
只统计 `User/ui_framework/` 下的目标文件；结构体大小用 `arm-none-eabi-gcc -mcpu=cortex-m4`
实测 `sizeof`；窗口样式数据大小由解析 `tools/JL/JL.sty` 得到。

### 6.1 Flash / 静态 RAM

| 模块                          |            Code |        RO Data |       RW Data |        ZI Data |
| ----------------------------- | --------------: | -------------: | ------------: | -------------: |
| `liba/ui_dot`（控件树）     |           24910 |            935 |            16 |            300 |
| `lcd_drive/middle`（引擎）  |           16286 |           2743 |           145 |            700 |
| `liba/font`（字模/编码）    |           10598 |            714 |             0 |            112 |
| `liba/res`（资源读取）      |            3744 |            438 |             4 |           4697 |
| `port/bsp`（STM32F4）       |             750 |              0 |             0 |            101 |
| `port/res`（FatFs）         |             628 |              0 |             0 |              0 |
| `liba/common`               |             302 |             48 |             0 |              0 |
| `lcd_drive`（SSD1306 屏驱） |               8 |            295 |           220 |              0 |
| `config`                    |              32 |             44 |            20 |              0 |
| **合计**                | **57258** | **5217** | **405** | **5910** |

- **Flash（ROM）= Code + RO + RW = 62880 B ≈ 61.4 KB**，占整个固件 120372 B 的 52%
- **静态 RAM = RW + ZI = 6315 B ≈ 6.2 KB**

静态 RAM 的大头是 `liba/res` 的 4697 B —— `ui_res_core.c` 里 `s_files[UI_RES_MAX_OPEN]`，
8 个 FatFs `FIL` 句柄池（`_MAX_SS = 512` 下每个 580 B）。把 `UI_RES_MAX_OPEN` 调小可直接换 RAM。

单文件最大的三个：`ui_grid.o` 9350 B、`ui_synthesis_oled.o` 8416 B、`ui_core_dot.o` 6510 B。
只做 ASCII 界面时，`font_gbk/big5/ksc/sjis` 四个可裁，省 5506 B。

### 6.2 动态 RAM（FreeRTOS heap_4，本工程配 64KB）

**常驻部分**，UI 起来就占，不随界面变：

| 项                                      |              大小 |
| --------------------------------------- | ----------------: |
| `"ui"` 任务栈（1024 字）+ TCB         |           ~4.2 KB |
| `"ui"` 消息队列（32 × 4B + 结构）    |            ~208 B |
| `ui_core_dot` 延时调用池（30 × 16B） |             480 B |
| `ui_core_api` 延时调用池（10 × 16B） |             160 B |
| **小计**                          | **~5.0 KB** |

**每显示一个窗口**再占：

| 项                                             |                                                                              大小 |
| ---------------------------------------------- | --------------------------------------------------------------------------------: |
| 窗口样式数据`jlui_malloc(window.len)`        |                                        PAGE_0 4454 / PAGE_1 8538 / PAGE_2 11422 B |
| 指针重定位表（读完立即释放，只是瞬时峰值）     |                                                                 318 / 610 / 862 B |
| `struct window`                              |                                                                             100 B |
| 每个 layer：结构 260 + 显存 1024 +`fbuf` 256 |                                                 1540 B（本工程每窗口 1 个 layer） |
| 每个 layout                                    |                                                                              88 B |
| 控件对象                                       | pic 84 / text 128 / battery 100 / number 176 / time 176 / grid 316 / slider 384 B |

控件是**按可见性懒分配**的：`layer_init` / `layout_init` 遇到 `invisible` 直接返回，
隐藏的菜单不占堆。所以峰值取决于当前窗口里真正可见的那棵子树。

以最大的 PAGE_2 估算：11422 + 862 + 100 + 1540 + 可见控件（十几个，约 2 KB）≈ **16 KB**，
加常驻 5 KB → **UI 框架运行期堆占用约 20 KB**，64KB 的堆余量充足。

> 要拿实测值：打开 `lcd_drive/middle/ui_resources_manager.c` 顶部的 `UI_BUF_CALC` 宏，
> 每次 `jlui_malloc`/`jlui_free` 都会打印当前累计；或直接调 `xPortGetMinimumEverFreeHeapSize()`。

## 7. 目录结构

```
STM32F407VGT6_Template/
├── Core/                   CubeMX 生成: main.c, 时钟, GPIO, SPI3, 中断
├── Drivers/                STM32F4xx HAL + CMSIS
├── FATFS/                  FatFs 接入层
│   ├── App/fatfs.c             卷对象 USERFatFS / 盘符 USERPath
│   └── Target/user_diskio.c    diskio -> User/fs/flash_disk.c
├── FreeRTOS/               内核 + task_manager.c(任务注册表 + 杰理 OS 兼容桥接)
├── Middlewares/            FatFs 源码 + USB Device Library
├── USB_DEVICE/             USB 复合设备(当前只开 MSC)
├── RTT/                    分级日志宏 + printf 重定向(走串口) + SEGGER RTT 源码(未启用)
├── User/
│   ├── apps/app_core.c         应用主任务, UI 初始化入口
│   ├── fs/flash_disk.c         片内 Flash 模拟磁盘(FatFs 与 USB MSC 共用)
│   ├── key/  led/  test/       外设测试
│   └── ui_framework/           ★ UI 框架本体, 见下
├── tools/
│   ├── LCD_UI工程/             UI 绘图 / 资源生成 / 字库工具
│   ├── JL/                     生成的 UI 资源(JL.sty/res/str)
│   └── font/                   字库(ascii.res, F_GB2312.PIX/TAB)
├── docs/                   硬件资料(原理图 PCB, 数据手册)
├── MDK-ARM/                Keil 工程
└── keil_make.bat           命令行编译
```

### `User/ui_framework/` 的分层

```
ui_framework/
├── include/                全部框架头文件(含 jl_* 兼容头)
├── liba/                   框架内核 —— 与工具链资源格式强绑定, 改动前先看文件头注释
│   ├── ui_dot/                 控件树 + 脏矩形重绘引擎(16 个文件)
│   ├── font/                   字模取模 + 多语言编码转换
│   ├── res/                    资源读取 / 解压 / 缓存表 + ui_res_core.c
│   ├── ui_draw/                圆弧 / 图像解码
│   └── common/                 杂项库函数 + 移植存根
├── lcd_drive/
│   ├── oled_spi_ssd1306_128x64.c   屏驱(一块屏一个文件)
│   └── middle/                     UI 引擎: 任务/推屏调度/资源管理/绘制原语
├── config/                 移植配置总入口 + 控件注册表
└── port/                   ★ 移植层 —— 换 MCU / 换文件系统只动这里
    ├── bsp/
    │   ├── ui_lcd_if.h             接口(MCU 无关)
    │   └── stm32f4/                实现 + 板级接线
    ├── res/
    │   ├── ui_res_backend.h        接口(介质无关)
    │   └── fatfs/                  FatFs 后端
    └── rtos/jl_os_api.h            OS 桥接
```

---

## 8. 移植到别的平台

框架和硬件之间只有三条边界，各自一个接口文件、一个实现目录。

### 换 MCU

1. 复制 `port/bsp/stm32f4/` 为 `port/bsp/<你的芯片>/`
2. 实现 `port/bsp/ui_lcd_if.h` 里的 12 个函数：
   - 控制线：`ui_lcd_cs/dc/rst/bl/power(level)`、`ui_lcd_te_read()`、`ui_lcd_has_backlight()`
   - 数据通道：`ui_lcd_write_byte/write_block/wait_done`、`ui_lcd_memory_barrier`、`ui_lcd_init`
3. 改 `ui_board_pins.h` 的接线、`ui_board_<芯片>.h` 的外设实例

框架**不持有任何引脚**：它只说"把 CS 拉低"，引脚编码规则是 port 层内部约定，
换 MCU 可以整套换掉，接口一个字不动。

### 换文件系统

1. 复制 `port/res/fatfs/` 为 `port/res/<介质>/`
2. 实现 `port/res/ui_res_backend.h` 里的 8 个函数（mount/open/read/seek/tell/size/close/dump_tree），实代码约 100 行
3. 在 `ui_res_backend.h` 的 `CTX_SIZE` 表加一行、改 `config/ui_port_config.h` 的 `UI_RES_BACKEND_*` 宏

### 换 RTOS

改 `FreeRTOS/task_manager.c`，`port/rtos/jl_os_api.h` 转发到它。
需要提供：任务/消息队列/信号量/互斥量/软定时器、临界区（`local_irq_disable/enable`）、
延时（`os_time_dly` / `delay_2ms`）、喂狗（`wdt_clear`）、`zalloc`。

### 换屏

改 `lcd_drive/`（一块屏一个文件，靠 `REGISTER_LCD_DEVICE()` 注册）+
`config/ui_port_config.h` 的 `UI_PORT_LCD_WIDTH/HEIGHT`。
屏参真值只在配置头里一处，屏驱从它推导。

### Keil 文件列表与 Makefile

工程用 Keil 的 `MDK-ARM/*.uvprojx` 文件列表；`User/ui_framework/Makefile` 是给
GCC/Make 工程用的 include 片段（**Keil 不读它**），同时充当"这棵树由哪些文件组成"的清单。
改目录结构要**两边都更新**。
