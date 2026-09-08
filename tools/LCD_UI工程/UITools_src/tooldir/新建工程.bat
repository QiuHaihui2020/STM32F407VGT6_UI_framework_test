@echo off
rem 从 template\ 拉一份空工程出来，目录结构与原厂 ui_128_64_JL02\ 一致：
rem
rem   <工程名>\
rem       <界面名>\
rem           重建版-step1-打开UI绘图工具.bat
rem           重建版-step2-生成资源.bat
rem           重建版-step3-自检.bat
rem           project\
rem               config\ini\project.ini      工程配置（json 名 / 工程ID / 旋转 / 收尾脚本）
rem               config\pic_lcd\             图片放这儿
rem               Application Data\ui-config  编辑器默认值
rem               copy_file.bat               把产物拷进固件工程
rem
rem 用法: 新建工程.bat <工程名> [界面名]
setlocal
if "%~1"=="" (
    echo 用法: 新建工程.bat ^<工程名^> [界面名]
    echo   例: 新建工程.bat ui_128_64_MY 模式界面
    exit /b 1
)
set NAME=%~1
set SCREEN=%~2
if "%SCREEN%"=="" set SCREEN=模式界面

set DST=%~dp0..\%NAME%\%SCREEN%
if exist "%DST%" (
    echo [新建工程] %DST% 已存在，换个名字或先删掉
    exit /b 1
)
robocopy "%~dp0template" "%DST%" /E /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1

echo [新建工程] 建好了: %DST%
echo [新建工程] 接下来:
echo     1. 把图片放进 %SCREEN%\project\config\pic_lcd\
echo     2. 双击 重建版-step1-打开UI绘图工具.bat，用"新建工程"建页面
echo     3. 双击 重建版-step2-生成资源.bat
endlocal
rem robocopy 成功也返回 1，别让它漏成本脚本的退出码
exit /b 0
