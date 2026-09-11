# UIProject —— 128×64 点阵屏的 UI 工程

一个目录装下全部：设计、它用的工具、以及两三个偶尔点一次的脚本。
生成物没入库 —— 在编辑器里点一次「资源导出」就有了。

## 怎么打开

三种方式，随便哪种：

1. **双击 `tool\UITools.exe`** —— 不给参数，它自己找 `project\` 里的工程。
2. **双击 `project\SmallColorTFT.uiproj`** —— 先跑一次本目录的
   `注册文件关联.bat`（只写当前用户的注册表，不需要管理员）。跑完工程文件的
   图标也会变成编辑器的图标。
3. **拖进编辑器窗口** —— 工程文件、`project\`、或者整个本目录都认。

> 工程文件的后缀是 `.uiproj`，内容就是 json —— 换后缀是为了能关联到本工具上
> （关联 `.json` 会抢走系统里所有 json 文件的双击）。既有的 `.json` 工程照样
> 能开，两种后缀都认。

打开之后，**生成资源点工具栏的「资源导出」**，跑的是完整那条链
（QtToolBin → ResBuilder → `copy_file.bat`）。

## 目录

```
UIProject\
    注册文件关联.bat  取消文件关联.bat   一次性：让双击 .uiproj 能打开
    新建工程.bat                        在旁边复制出一个新的 UI 工程
    project\                            **工程本体**
        SmallColorTFT.uiproj            主设计（project.ini 里选的就是它）
        SmallColor_oled.uiproj          另一套设计
        config\ini\project.ini           工程配置
        config\pic_lcd\                  图片
        Application Data\ui-config       编辑器设置（预览配色、预览文字在这儿）
        copy_file.bat                   把产物拷进固件工程
        version.txt
    tool\                               工具，见 tool\README.md
        UITools.exe QtToolBin.exe ResBuilder.exe + Qt 运行库
        assets\                          控件库 / 类型码 / 背景图 / 多国语言表
        compat\                          校验脚本
        template\                        新建工程用的空工程
        自检.bat                         25 项自洽性检查（要 python3）
        clear.bat  clear.sh              清掉全部生成物
```

工程里的相对路径只有一条约定：**工具在 `..\tool`**。没有任何地方写死工具
目录叫什么名字，整个目录改名、搬走都不影响（关联脚本除外 —— 注册表里存的是
绝对路径，搬完重跑一次 `注册文件关联.bat`）。

## 哪些东西不入库

| 项 | 说明 |
|---|---|
| `autosave.json` | 编辑器自动存档，自己会生成 |
| `uitoolbin.bin` | 中间文件，本工具链不产 |
| `Resbuilder.dat` | 缓存，本工具链每次全量算，不需要 |
| `qtread.csv` / `imagelist.txt` | 带机器绝对路径的陈旧缓存 |
| 生成物 | 空的，点「资源导出」生成 |

## 注意

**生成资源会真的写你的固件树** —— 按 `project\copy_file.bat` 把三个资源拷进
`tools\JL\`、把 `ename.h` 拷成
`User\ui_framework\include\common\style_jl02.h`。第一次跑之前先
`git status` 心里有数。只想看产物不想拷，就走命令行：

```
cd project
..\tool\QtToolBin.exe --no-script -o <临时目录> ^
    --run-resbuilder ..\tool\ResBuilder.exe
```

**控件 id 换一套工具就会变**，所以资源和 `style_jl02.h` 必须一起换 ——
`copy_file.bat` 本来就是一起拷的，照常跑就行。细节见 `tool\README.md`。

清生成物用 `tool\clear.bat`，它按实际产物**逐个文件**删。别用通配符删这个
目录 —— `version.txt` 和 `config\*.png` 不是生成物，删了找不回来。

这个目录当初是 `tool_src\mkdist_project.bat` 从既有工程种出来的。
**别再跑一次** —— 那会把这里的设计覆盖回种子工程的样子（脚本本身也会拦）。
