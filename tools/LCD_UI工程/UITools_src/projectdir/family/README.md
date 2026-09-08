# ui_128_64_JL02_rebuilt

原厂 `ui_128_64_JL02\` 的**同位副本**，区别只有一个：驱动它的是
`..\UITools_rebuilt\`（重建版工具），不是 `..\UITools\`（原厂）。

设计文件、图片、`copy_file.bat` 都是从原厂工程原样拷过来的，所以两边应该
产出等价的资源。生成物没拷 —— 跑一次 step2 就有了。

## 目录

```
ui_128_64_JL02_rebuilt\
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

## 和原厂 ui_128_64_JL02\ 的差别

| 项 | 原厂 | 这里 |
|---|---|---|
| step1 指向 | `..\..\..\UITools\ui-tools.exe` | `..\..\..\UITools_rebuilt\UITools.exe` |
| step2 | 开 QtToolBin 界面，手点「生成资源文件(F5)」 | 一样：弹界面，手点。想不弹窗一条命令跑完就用命令行模式 |
| step3 | 无 | 新增，产物自洽性检查 |
| `autosave.json` | 有（5 MB 编辑器自动存档） | 没拷，编辑器自己会生成 |
| `uitoolbin.bin` | 有（4.6 MB，原厂 QtToolBin 的中间文件） | 重建版不产这个东西 |
| `Resbuilder.dat` | 有（原厂 ResBuilder 的缓存） | 重建版不需要 |
| `qtread.csv` / `imagelist.txt` | 有（另一台机器路径的陈旧缓存） | 没拷 |
| 生成物 | 留着上次的 | 空的，跑 step2 生成 |

## 注意

**step2 会真的写你的固件树** —— 按 `project\copy_file.bat` 把三个资源拷进
`tools\JL\`、把 `ename.h` 拷成
`User\ui_framework\include\common\style_jl02.h`。第一次跑之前先
`git status` 心里有数。只想看产物不想拷，就手动跑：

```
cd 模式界面\project
..\..\..\UITools_rebuilt\QtToolBin.exe --no-script -o <临时目录> ^
    --run-resbuilder ..\..\..\UITools_rebuilt\ResBuilder.exe
```

**控件 id 和原厂不一样**，所以资源和 `style_jl02.h` 必须一起换 ——
`copy_file.bat` 本来就是一起拷的，照常跑就行。细节见
`..\UITools_rebuilt\README.md`。

这个目录是 `..\UITools_src\mkdist_project.bat` 生成的。
