# ui_128_64_app

128×64 点阵屏的 UI 工程目录，由 `..\UIToolkit\` 里的三个工具驱动。
生成物没入库 —— 跑一次 step2 就有了。

## 目录

```
ui_128_64_app\
    clear.bat  clear.sh          清掉全部生成物
    模式界面\
        step1-打开UI绘图工具.bat      开编辑器
        step2-生成资源.bat            弹 QtToolBin 界面，点「生成资源文件(F5)」
        step3-自检.bat                21 项自洽性检查
        打开图片资源文件夹.bat
        project\
            SmallColorTFT.json        主设计（project.ini 里选的就是它）
            SmallColor_oled.json      另一套设计
            config\                   图片 + option.ini + project.ini
            backgrounds\
            Application Data\ui-config
            copy_file.bat             把产物拷进固件工程
            release.bat
            version.txt
```

## 哪些东西不入库

| 项 | 说明 |
|---|---|
| `autosave.json` | 编辑器自动存档，自己会生成 |
| `uitoolbin.bin` | 中间文件，本工具链不产 |
| `Resbuilder.dat` | 缓存，本工具链每次全量算，不需要 |
| `qtread.csv` / `imagelist.txt` | 带机器绝对路径的陈旧缓存 |
| 生成物 | 空的，跑 step2 生成 |

## 注意

**step2 会真的写你的固件树** —— 按 `project\copy_file.bat` 把三个资源拷进
`tools\JL\`、把 `ename.h` 拷成
`User\ui_framework\include\common\style_jl02.h`。第一次跑之前先
`git status` 心里有数。只想看产物不想拷，就手动跑：

```
cd 模式界面\project
..\..\..\UIToolkit\QtToolBin.exe --no-script -o <临时目录> ^
    --run-resbuilder ..\..\..\UIToolkit\ResBuilder.exe
```

**控件 id 换一套工具就会变**，所以资源和 `style_jl02.h` 必须一起换 ——
`copy_file.bat` 本来就是一起拷的，照常跑就行。细节见
`..\UIToolkit\README.md`。

这个目录是 `..\UITools_src\mkdist_project.bat` 生成的。
