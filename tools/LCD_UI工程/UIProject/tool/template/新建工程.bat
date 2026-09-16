@echo off
rem 复制出一个新的 UI 工程目录，建在本目录旁边。
rem
rem 一个 UI 工程目录是自足的：工程本身 + 它用的工具，删掉整个目录就干净了。
rem
rem   <工程名>\
rem       注册文件关联.bat  取消文件关联.bat  新建工程.bat
rem       project\
rem           config\ini\project.ini      工程配置（json 名 / 工程ID / 旋转 / 收尾脚本）
rem           config\pic_lcd\             图片放这儿
rem           Application Data\ui-config  编辑器设置
rem           copy_file.bat               把产物拷进固件工程
rem       tool\                           exe + Qt + 自检/clear + template
rem
rem 控件库和类型码表编在 UITools.exe 里，所以新工程里看不到它们 —— 不用管。
rem 要改文案的多国语言表（*.xls）得自己放进 project\，它是工程素材。
rem
rem 用法: 新建工程.bat <工程名>
setlocal
if "%~1"=="" (
    echo 用法: 新建工程.bat ^<工程名^>
    echo   例: 新建工程.bat ui_128_64_MY
    exit /b 1
)
set NAME=%~1
set HERE=%~dp0
set DST=%HERE%..\%NAME%

if exist "%DST%" (
    echo [新建工程] %DST% 已存在，换个名字或先删掉
    exit /b 1
)
if not exist "%HERE%tool\template" (
    echo [新建工程] 找不到 %HERE%tool\template
    exit /b 1
)

echo [新建工程] 拉工程骨架...
robocopy "%HERE%tool\template" "%DST%"      /E /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1
echo [新建工程] 拷工具（约 22 MB）...
robocopy "%HERE%tool"           "%DST%\tool" /E /XD __pycache__ /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1

echo [新建工程] 建好了: %DST%
echo [新建工程] 接下来:
echo     1. 把图片放进 %NAME%\project\config\pic_lcd\
echo     2. 把多国语言表（*.xls）放进 %NAME%\project\
echo     3. 双击 %NAME%\tool\UITools.exe，用"新建工程"建页面并保存
echo     4. 点工具栏的「资源导出」生成资源
echo     5. 按需改 project\copy_file.bat 里的两个路径
endlocal
rem robocopy 成功也返回 1，别让它漏成本脚本的退出码
exit /b 0
