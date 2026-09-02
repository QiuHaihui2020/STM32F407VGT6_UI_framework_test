# Portable Dot-Matrix UI Framework — STM32 Validation Project

> **Language / 语言:** [中文](README.md) · [English](README_EN.md)

A portable dot-matrix UI framework (`User/ui_framework/`), rewritten from scratch with
JieLi's UI framework as the reference. JieLi's framework is closed source, so all code here
is original; it is used as a reference because it comes with a GUI editor (`ui-tools.exe`
to draw screens, `ResBuilder.exe` to build resources). Reusing that toolchain means matching
its resource formats (`.sty` / `.res` / `.str`) and widget model. In short:
**formats and structure are aligned, the code is rewritten.**

This project is its first host platform: STM32F407 + SSD1306 128×64 mono OLED, pushed over
SPI + DMA, with resources stored on a FAT volume emulated in on-chip Flash.

---

## 1. Hardware

### Target MCU

| Item     | Value                                                                    |
| -------- | ------------------------------------------------------------------------ |
| MCU      | **STM32F407VET6** (LQFP100, 512KB Flash, 128KB SRAM + 64KB CCM)          |
| Clock    | 168MHz (HSE 8MHz, PLLM=8 / PLLN=336 / PLLP=2, FLASH_LATENCY_5)           |
| Debugger | J-Link / ST-Link                                                         |
| Log      | **UART USART1 = PA9**, baud rate **1000000**                             |
| USB      | **USB_OTG_FS: PA11 = DM, PA12 = DP** (resource volume goes through this) |

### OLED Wiring

Panel: SSD1306 128×64, 4-wire SPI (self-emissive, **no backlight pin**).

| Signal    | Pin     | Note                                             |
| --------- | ------- | ------------------------------------------------ |
| SCK       | **PB3** | SPI3_SCK                                         |
| MOSI      | **PB5** | SPI3_MOSI (the panel's DIN/SDA)                  |
| CS        | **PB6** | Chip select, active low                          |
| DC        | **PB7** | Command/data select, low = command, high = data  |
| RST       | **PB8** | Reset, active low                                |
| MISO      | PB4     | **Unused on a mono panel**, but CubeMX claims it |
| VCC / GND | —       | 3.3V                                             |

### SPI / DMA Parameters

| Item          | Value                                                             |
| ------------- | ----------------------------------------------------------------- |
| Instance      | SPI3 (APB1 = 42MHz)                                               |
| Speed         | Prescaler = 2 → **21MHz**                                         |
| Mode          | Mode 0 (CPOL = Low, CPHA = 1Edge), 8-bit, MSB first               |
| DMA           | **DMA1_Stream5 / Channel 0**, Memory→Periph, byte aligned, Normal |
| DMA interrupt | `DMA1_Stream5_IRQn`, preemption priority 6                        |
| Timeout guard | 100ms                                                             |

> DMA1_Stream3/4 are already taken by I2S2, hence Stream5.
> RM0090 Table 42: SPI3_TX = DMA1 Stream5 Ch0 or Stream7 Ch0.
>
> `DMA1_Stream5_IRQHandler` is defined in `User/ui_framework/port/bsp/stm32f4/ui_lcd_stm32f4.c`
> and caught by the weak symbol in the startup file. **If you ever enable DMA for SPI3 in CubeMX**,
> `Core/Src/stm32f4xx_it.c` will generate a function with the same name → duplicate-definition
> link error; just delete the one in this layer.

### UART and USB Wiring

| Purpose    | Pin      | Note                              |
| ---------- | -------- | --------------------------------- |
| Log output | **PA9**  | USART1_TX, AF7. Baud rate 1000000 |
| USB D−     | **PA11** | USB_OTG_FS_DM, AF10               |
| USB D+     | **PA12** | USB_OTG_FS_DP, AF10               |

USB runs as a **Full Speed device**, with `vbus_sensing_enable = DISABLE` (no VBUS detection),
so only D+/D− and GND need to be connected — no extra VBUS sense pin required.

---

**Changing the wiring only touches one file**: `User/ui_framework/port/bsp/stm32f4/ui_board_pins.h`
(pins) plus `ui_board_stm32f4.h` in the same directory (SPI/DMA instances).
The framework layer holds no pin information at all.

---

## 2. Build and Run

### Build

Two ways, same output.

#### Option 1: Keil GUI

Open `MDK-ARM/STM32F407VGT6_Template.uvprojx` and build from the Keil UI.

The project has two targets (top-left dropdown):

| Target                   | Purpose                                              |
| ------------------------ | ---------------------------------------------------- |
| `STM32F407VGT6_Template` | **Default**, output goes to `MDK-ARM/FLASH_RELEASE/` |

#### Option 2: Command-line script

`keil_make.bat` drives Keil's `UV4.exe` headlessly — handy for checking results right after
an edit, or for CI:

```
keil_make.bat                        # incremental build, default target STM32F407VGT6_Template
keil_make.bat rebuild                # full rebuild
```

Usage: `keil_make.bat [build|rebuild] [target name]`; both arguments are optional
(defaults are at the top of the script).

Things to watch out for:

- `set UV=D:\Keil_v5\UV4\UV4.exe` in the script must match your Keil install path
- The project file is located by `dir /s /b *.uvprojx`, which picks the first hit in the
  current directory tree — so you **must run it from the `STM32F407VGT6_Template/` directory**
- The build log is written to `build_log.txt` (overwritten each run; the script `type`s it at the end)

Current baseline: `Code=109556  RO-data=10440  RW-data=820  ZI-data=93324`, 0 errors 0 warnings.

### Reading the Log

Logs go out over **UART**.

| Item | Value                                                                            |
| ---- | -------------------------------------------------------------------------------- |
| Pin  | **PA9 = USART1_TX** (PA10 = RX, AF7)                                             |
| Baud | **1000000 (1Mbps)** — not the usual 115200, so enter it manually in your terminal |

Two on-target troubleshooting switches live in `User/ui_framework/config/ui_port_config.h`:

| Macro                  | Effect                                                                                                          |
| ---------------------- | --------------------------------------------------------------------------------------------------------------- |
| `UI_PORT_FS_DUMP_TREE` | Prints the real directory tree of the FAT volume after mount — the first thing to check for "resource not found" |
| `UI_PORT_PUSH_TRACE`   | Prints one line per pushed frame with `len` / non-zero byte count, to tell three kinds of "black screen" apart   |

The three `UI_PORT_PUSH_TRACE` cases: nothing printed = the draw pipeline never reached the push
stage; `nonzero=0` = the frame was pushed but the framebuffer is all zeros (widgets drew nothing);
`nonzero>0` = data was sent (so the problem is panel init / wiring / SPI timing).

### Startup Flow

```
main()
 ├─ MX_GPIO/DMA/SPI3/USB_DEVICE/FATFS_Init()      generated by CubeMX
 └─ app_core_init()                                User/apps/app_core.c
     ├─ task_create(app_core_function, "app_core")
     └─ os_start()                                 FreeRTOS scheduler starts
          └─ app_core_function()
              └─ app_ui_init() → UI_INIT(&ui_cfg_data) = lcd_ui_init()
                   └─ internally task_create("ui") and waits on its startup semaphore
```

The UI task is in the registry in `FreeRTOS/task_manager.c`:
`{"ui", priority 4, stack 1024 words, queue 32}`.
The task name **must** be `"ui"` — the framework looks it up by name.

---

## 3. Authoring UI Resources

### Where the Tools Are

`tools/LCD_UI工程/`

| Path                       | Purpose                                                            |
| -------------------------- | ------------------------------------------------------------------ |
| `UITools/ui-tools.exe`     | UI editor (draw screens, place widgets)                            |
| `UITools/ResBuilder.exe`   | Resource builder (exports .sty/.res/.str + the id header)          |
| `字库工具/`                | Generates `F_GB2312.PIX/.TAB` and `ascii.res`                      |
| `doc/*.pdf`                | Vendor docs: UI layout tool guide / UI configuration / quick start |
| `ui_128_64_JL02/模式界面/` | **This project's UI project files** (128×64, JL02 style)           |

### Workflow

Under `tools/LCD_UI工程/ui_128_64_JL02/模式界面/`, run these in order:

```
step1-打开UI绘图工具.bat      draw the screens -> saving produces project/project.bin
step2-打开UI资源生成工具.bat  build resources  -> automatically calls project/copy_file.bat
打开图片资源文件夹.bat        drop image assets here
```

`project/copy_file.bat` distributes the build products into the project:

| Product       | Destination                                     | Note                                                               |
| ------------- | ----------------------------------------------- | ------------------------------------------------------------------ |
| `project.bin` | `tools/JL/JL.sty`                               | Window / widget layout                                             |
| `result.bin`  | `tools/JL/JL.res`                               | Image resources                                                    |
| `result.str`  | `tools/JL/JL.str`                               | String images                                                      |
| `ename.h`     | `User/ui_framework/include/common/style_jl02.h` | Widget/window id hash table — **generated file, do not hand-edit** |

`style_jl02.h` contains `PAGE_0..PAGE_10` and the hash id of every widget;
`include/common/ui_style.h` includes it and maps those to names like `ID_WINDOW_*`.

> `copy_file.bat` must stay **pure ASCII** — cmd.exe reads `.bat` files using the system ANSI
> code page, and UTF-8 comments turn into mojibake severe enough to break parsing.

---

## 4. Getting Resources into On-Chip Flash

The resource volume is a **FAT volume emulated in on-chip Flash**, exposed to the PC over
**USB MSC** so files can be dragged onto it directly.

### Flash Layout (VET6, 512KB)

Configured in `User/fs/flash_disk.h`:

| Sectors | Address                    | Size      | Purpose                                            |
| ------- | -------------------------- | --------- | -------------------------------------------------- |
| S0..S4  | `0x08000000`..`0x0801FFFF` | 128KB     | Program code                                       |
| S5      | `0x08020000`..`0x0803FFFF` | 128KB     | FAT write **swap sector** (`FLASH_DISK_SWAP_ADDR`) |
| S6..S7  | `0x08040000`..`0x0807FFFF` | **256KB** | **FAT resource volume** (`FLASH_DISK_BASE_ADDR`)   |

Sector size is 512 bytes (`FLASH_DISK_SECTOR_SIZE`; it must match `_MAX_SS` in `ffconf.h`,
enforced by an `#error`). The volume is currently in **read-write mode**
(`FLASH_DISK_READONLY = 0`), which is why the swap sector is needed: Flash can only be erased
per physical sector, so modifying one 512B logical sector means reading the whole physical
sector into the swap area first.

### Steps

1. Flash the firmware, plug in USB (**USB_OTG_FS: PA11 = D−, PA12 = D+**)
2. A roughly **256KB removable drive** shows up on the PC (the first time it may ask to be
   formatted → format as FAT, default cluster size)
3. Copy the files in using the layout below
4. Eject the drive and reset the board

### Required Layout on the Volume

```
0:/JL/JL.res          <- tools/JL/JL.res
0:/JL/JL.str          <- tools/JL/JL.str
0:/JL/JL.sty          <- tools/JL/JL.sty
0:/font/ascii.res     <- tools/font/ascii.res
0:/font/F_GB2312.PIX  <- tools/font/F_GB2312.PIX   (only if you need Chinese)
0:/font/F_GB2312.TAB  <- tools/font/F_GB2312.TAB   (only if you need Chinese)
```

> ⚠ **`UI_PORT_RES_ROOT` is the volume root `"0:"` — no extra directory level.**
> The framework builds paths itself as `RES_PATH"JL/JL.res"`, so setting it to `"0:/ui"`
> yields `0:/ui/JL/JL.res`, which does not exist on the volume → every `resfile_open` fails
> and the screen stays black.
>
> `_USE_LFN = 0`, so the volume uses 8.3 short filenames (all uppercase).
> When paths do not line up, enable `UI_PORT_FS_DUMP_TREE` and look at the real tree
> instead of guessing.

### USB Configuration

Composite device (`USE_USBD_COMPOSITE`), currently **MSC only**:

```
USB_DEVICE/Target/usbd_conf.h
    USBD_MSC_CMPSIT_ENABLE    1     <- enabled
    USBD_CDC_CMPSIT_ENABLE    0
    USBD_AUDIO_CMPSIT_ENABLE  0
```

MSC's storage backend is the same `User/fs/flash_disk.c` (called from
`USB_DEVICE/App/usbd_storage_if.c`), so the volume seen over USB and the one seen by FatFs
are the same piece of Flash.

> ⚠ **Do not let the UI read resources while the USB host is writing to the volume** — both
> sides are touching the same Flash, and CPU instruction fetch stalls during erase/program.
> The correct order when updating resources is: copy → eject → reset.

---

## 5. Software Dependencies

| Component              | Version / Notes                                                                                                          |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| **FreeRTOS**           | V9.0.0. 1000Hz tick, 64KB heap (`heap_4`), max priority 32                                                               |
| **FatFs**              | ChaN FatFs from Middlewares. `_MAX_SS = 512`, `_USE_LFN = 0`; `_FS_LOCK` must match `UI_RES_MAX_OPEN` (8)                 |
| **STM32 HAL**          | STM32F4xx HAL Driver (CubeMX generated)                                                                                  |
| **USB Device Library** | ST's STM32_USB_Device_Library + CompositeBuilder, MSC class                                                              |
| **UART logging**       | USART1 (PA9) @ 1000000, TX over DMA2_Stream7. `printf` is redirected in `RTT/log_debug.c`                                 |
| Compiler               | Keil MDK-ARM, ArmClang (AC6), C99, optimization `-O3` (`Optim 7`)                                                        |

Predefined macros: `USE_HAL_DRIVER, STM32F407xx, USE_USBD_COMPOSITE`

### What the Framework Requires from the Host

The UI framework never touches the HAL directly. It only asks the host project for three things:

| Dependency           | Provided by               | Interface                                                                                  |
| -------------------- | ------------------------- | ------------------------------------------------------------------------------------------ |
| **Panel hardware**   | `port/bsp/stm32f4/`       | `port/bsp/ui_lcd_if.h` — control-line toggling + the SPI data channel                      |
| **Resource storage** | `port/res/fatfs/`         | `port/res/ui_res_backend.h` — 8 backend functions                                          |
| **OS services**      | `FreeRTOS/task_manager.c` | `port/rtos/jl_os_api.h` — tasks/queues/semaphores/critical sections/delays/watchdog/zalloc |

---

## 6. Footprint

Numbers come from `MDK-ARM/FLASH_RELEASE/STM32F407_FLASH_RELEASE.map` (ArmClang, `-O3`),
counting only object files under `User/ui_framework/`. Struct sizes were measured with
`arm-none-eabi-gcc -mcpu=cortex-m4` (`sizeof`), and window style-data sizes come from
parsing `tools/JL/JL.sty`.

### 6.1 Flash / Static RAM

| Module                          |      Code |  RO Data | RW Data | ZI Data |
| ------------------------------- | --------: | -------: | ------: | ------: |
| `liba/ui_dot` (widget tree)     |     24910 |      935 |      16 |     300 |
| `lcd_drive/middle` (engine)     |     16286 |     2743 |     145 |     700 |
| `liba/font` (glyphs / encoding) |     10598 |      714 |       0 |     112 |
| `liba/res` (resource reading)   |      3744 |      438 |       4 |    4697 |
| `port/bsp` (STM32F4)            |       750 |        0 |       0 |     101 |
| `port/res` (FatFs)              |       628 |        0 |       0 |       0 |
| `liba/common`                   |       302 |       48 |       0 |       0 |
| `lcd_drive` (SSD1306 panel drv) |         8 |      295 |     220 |       0 |
| `config`                        |        32 |       44 |      20 |       0 |
| **Total**                       | **57258** | **5217** | **405** | **5910** |

- **Flash (ROM) = Code + RO + RW = 62880 B ≈ 61.4 KB**, i.e. 52% of the full 120372 B firmware
- **Static RAM = RW + ZI = 6315 B ≈ 6.2 KB**

Static RAM is dominated by the 4697 B in `liba/res` — `s_files[UI_RES_MAX_OPEN]` in
`ui_res_core.c`, a pool of 8 FatFs `FIL` handles (580 B each with `_MAX_SS = 512`).
Lowering `UI_RES_MAX_OPEN` buys RAM back directly.

The three largest single files: `ui_grid.o` 9350 B, `ui_synthesis_oled.o` 8416 B,
`ui_core_dot.o` 6510 B. For an ASCII-only UI, `font_gbk/big5/ksc/sjis` can all be dropped,
saving 5506 B.

### 6.2 Dynamic RAM (FreeRTOS heap_4, 64KB in this project)

**Resident part** — allocated once the UI is up, independent of which screen is shown:

| Item                                        |        Size |
| ------------------------------------------- | ----------: |
| `"ui"` task stack (1024 words) + TCB        |     ~4.2 KB |
| `"ui"` message queue (32 × 4B + struct)     |      ~208 B |
| `ui_core_dot` deferred-call pool (30 × 16B) |       480 B |
| `ui_core_api` deferred-call pool (10 × 16B) |       160 B |
| **Subtotal**                                | **~5.0 KB** |

**Per displayed window**, on top of that:

| Item                                                                     |                                                                              Size |
| ------------------------------------------------------------------------ | --------------------------------------------------------------------------------: |
| Window style data, `jlui_malloc(window.len)`                             |                                        PAGE_0 4454 / PAGE_1 8538 / PAGE_2 11422 B |
| Pointer relocation table (freed right after reading; transient peak only) |                                                                 318 / 610 / 862 B |
| `struct window`                                                          |                                                                             100 B |
| Per layer: struct 260 + frame buffer 1024 + `fbuf` 256                   |                                                  1540 B (1 layer per window here) |
| Per layout                                                               |                                                                              88 B |
| Widget objects                                                           | pic 84 / text 128 / battery 100 / number 176 / time 176 / grid 316 / slider 384 B |

Widgets are **allocated lazily by visibility**: `layer_init` / `layout_init` return immediately
when they hit `invisible`, so hidden menus cost no heap. The peak therefore depends on the
subtree that is actually visible in the current window.

Estimating with the largest page, PAGE_2: 11422 + 862 + 100 + 1540 + visible widgets
(a dozen or so, ~2 KB) ≈ **16 KB**; plus the 5 KB resident part →
**about 20 KB of heap while the UI framework is running**, comfortable inside the 64KB heap.

> To measure it for real: enable the `UI_BUF_CALC` macro at the top of
> `lcd_drive/middle/ui_resources_manager.c` — every `jlui_malloc`/`jlui_free` then prints the
> running total; or just call `xPortGetMinimumEverFreeHeapSize()`.

## 7. Directory Structure

```
STM32F407VGT6_Template/
├── Core/                   CubeMX generated: main.c, clocks, GPIO, SPI3, interrupts
├── Drivers/                STM32F4xx HAL + CMSIS
├── FATFS/                  FatFs integration layer
│   ├── App/fatfs.c             volume object USERFatFS / drive letter USERPath
│   └── Target/user_diskio.c    diskio -> User/fs/flash_disk.c
├── FreeRTOS/               kernel + task_manager.c (task registry + JieLi-style OS bridge)
├── Middlewares/            FatFs sources + USB Device Library
├── USB_DEVICE/             USB composite device (MSC only for now)
├── RTT/                    leveled log macros + printf redirection (to UART) + SEGGER RTT sources (unused)
├── User/
│   ├── apps/app_core.c         application main task, UI init entry point
│   ├── fs/flash_disk.c         on-chip Flash emulated disk (shared by FatFs and USB MSC)
│   ├── key/  led/  test/       peripheral tests
│   └── ui_framework/           ★ the UI framework itself, see below
├── tools/
│   ├── LCD_UI工程/             UI editor / resource builder / font tools
│   ├── JL/                     generated UI resources (JL.sty/res/str)
│   └── font/                   fonts (ascii.res, F_GB2312.PIX/TAB)
├── docs/                   hardware material (schematics, PCB, datasheets)
├── MDK-ARM/                Keil project
└── keil_make.bat           command-line build
```

### Layering inside `User/ui_framework/`

```
ui_framework/
├── include/                all framework headers (including the jl_* compatibility headers)
├── liba/                   framework core -- tightly bound to the toolchain resource formats,
│   │                       read the file-header comments before changing anything
│   ├── ui_dot/                 widget tree + dirty-rectangle redraw engine (16 files)
│   ├── font/                   glyph extraction + multi-language encoding conversion
│   ├── res/                    resource reading / decompression / cache table + ui_res_core.c
│   ├── ui_draw/                arcs / image decoding
│   └── common/                 misc library functions + porting stubs
├── lcd_drive/
│   ├── oled_spi_ssd1306_128x64.c   panel driver (one file per panel)
│   └── middle/                     UI engine: task, push scheduling, resource mgmt, drawing primitives
├── config/                 central porting configuration + widget registry
└── port/                   ★ porting layer -- swapping MCU / filesystem only touches this
    ├── bsp/
    │   ├── ui_lcd_if.h             interface (MCU agnostic)
    │   └── stm32f4/                implementation + board wiring
    ├── res/
    │   ├── ui_res_backend.h        interface (medium agnostic)
    │   └── fatfs/                  FatFs backend
    └── rtos/jl_os_api.h            OS bridge
```

---

## 8. Porting to Another Platform

There are only three boundaries between the framework and the hardware, each with one
interface file and one implementation directory.

### Swapping the MCU

1. Copy `port/bsp/stm32f4/` to `port/bsp/<your chip>/`
2. Implement the 12 functions in `port/bsp/ui_lcd_if.h`:
   - Control lines: `ui_lcd_cs/dc/rst/bl/power(level)`, `ui_lcd_te_read()`, `ui_lcd_has_backlight()`
   - Data channel: `ui_lcd_write_byte/write_block/wait_done`, `ui_lcd_memory_barrier`, `ui_lcd_init`
3. Adjust the wiring in `ui_board_pins.h` and the peripheral instances in `ui_board_<chip>.h`

The framework **holds no pin information**: it only ever says "pull CS low". The pin encoding
scheme is an internal convention of the port layer and can be replaced wholesale when changing
MCU, without touching a single line of the interface.

### Swapping the Filesystem

1. Copy `port/res/fatfs/` to `port/res/<medium>/`
2. Implement the 8 functions in `port/res/ui_res_backend.h`
   (mount/open/read/seek/tell/size/close/dump_tree) — about 100 lines of real code
3. Add a row to the `CTX_SIZE` table in `ui_res_backend.h` and update the `UI_RES_BACKEND_*`
   macros in `config/ui_port_config.h`

### Swapping the RTOS

Modify `FreeRTOS/task_manager.c`; `port/rtos/jl_os_api.h` forwards to it.
You need to supply: tasks / message queues / semaphores / mutexes / software timers,
critical sections (`local_irq_disable/enable`), delays (`os_time_dly` / `delay_2ms`),
watchdog kick (`wdt_clear`), and `zalloc`.

### Swapping the Panel

Modify `lcd_drive/` (one file per panel, registered via `REGISTER_LCD_DEVICE()`) plus
`UI_PORT_LCD_WIDTH/HEIGHT` in `config/ui_port_config.h`.
The panel geometry has a single source of truth in the config header; the panel driver
derives everything from it.

### Keil File List vs. Makefile

The project uses the file list in Keil's `MDK-ARM/*.uvprojx`. `User/ui_framework/Makefile`
is an include fragment for GCC/Make projects (**Keil does not read it**) and doubles as the
manifest of "which files make up this tree". When the directory structure changes,
**update both**.
