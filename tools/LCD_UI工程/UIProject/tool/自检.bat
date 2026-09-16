@echo off
rem 生成完跑一遍自检：产物内部自洽吗、固件加载得了吗。
rem 查 UI_VERSION 一致性、三个页表 CRC、id<->ename 双向对应、id 位域、
rem 指针落点与重定位表、资源号范围、.res/.str 完整性、固件需要的宏。
rem
rem 需要 python3，也需要源码树 —— 校验脚本在 ..\tool_src\compat\，那是开发资产，
rem 不跟着工具目录发出去。而且这些检查要对着固件的头文件做，本来就只在这棵树里
rem 才有意义。
setlocal
set CHK=%~dp0..\tool_src\compat\verify_selfconsistent.py
cd /d "%~dp0..\project"
set FWHDR=..\..\..\..\User\ui_framework\include\common

if not exist "%CHK%" (
    echo [自检] 找不到 %CHK%
    echo [自检] 校验脚本在源码树里，这份工程是单独拷出来的就没有
    pause
    exit /b 1
)
if not exist project.bin (
    echo [自检] 还没有 project.bin —— 先在编辑器里点工具栏的「资源导出」生成一遍
    pause
    exit /b 1
)
where python >nul 2>nul || (
    echo [自检] 没找到 python，跳过
    pause
    exit /b 0
)
python "%CHK%" . "%FWHDR%"
echo.
pause
endlocal
