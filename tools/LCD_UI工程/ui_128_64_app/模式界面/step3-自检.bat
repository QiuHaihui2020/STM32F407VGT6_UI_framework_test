@echo off
rem 生成完跑一遍自检：产物内部自洽吗、固件加载得了吗。
rem 查 UI_VERSION 一致性、三个页表 CRC、id<->ename 双向对应、id 位域、
rem 指针落点与重定位表、资源号范围、.res/.str 完整性、固件需要的宏。
rem 需要 python3。
setlocal
cd /d "%~dp0project"
set TOOLS=..\..\..\UIToolkit
set FWHDR=..\..\..\..\..\User\ui_framework\include\common

if not exist project.bin (
    echo [step3] 还没有 project.bin，先跑 step2
    pause
    exit /b 1
)
where python >nul 2>nul || (
    echo [step3] 没找到 python，跳过自检
    pause
    exit /b 0
)
python "%TOOLS%\re\verify_selfconsistent.py" . "%FWHDR%"
echo.
pause
endlocal
