@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------
:: 用法: keil_make.bat [build^|rebuild] [目标工程名]
::   参数1: build   -> 增量编译 (UV4 -b)   [默认]
::          rebuild -> 全部重新编译 (UV4 -r)
::   参数2: 编译目标工程名                  [默认 STM32F103C8T6]
:: 不传参数时使用默认值
:: ---------------------------------------------------------------

:: 设置MDK安装路径（根据实际安装路径修改）
set UV=D:\Keil_v5\UV4\UV4.exe

:: 默认编译模式（不传参数1时使用）: build=增量编译 / rebuild=全部重新编译
set DEFAULT_BUILD_MODE=build

:: 默认编译目标工程名（不传参数2时使用，根据实际需求修改）
:: 可选示例: RAM_Debug
@REM set DEFAULT_TARGET_NAME=STM32F103C8T6
@REM set DEFAULT_TARGET_NAME=flexspi_nor_debug
set DEFAULT_TARGET_NAME=STM32F407VGT6_Template

:: 设置工程文件路径
:: set UV_PRO_PATH=D:\MyWorkSpace_Test\B1X\MCU_MainApp\B1X_RT1061_MAIN\MDK\B1X_RT1061_MAIN.uvprojx
:: ​当前目录及所有子目录​​中搜索扩展名为 .uvprojx 的文件，并将​第一个找到的文件路径​​赋值给环境变量 UV_PRO_PATH
for /f "usebackq delims=" %%j in (`dir /s /b %cd%\*.uvprojx`) do (
    if exist %%j (
        set UV_PRO_PATH="%%j"
    )
    goto :break_loop
)
:break_loop

:: 解析参数1: 编译模式 (build / rebuild)，默认使用文件顶部的 DEFAULT_BUILD_MODE
set BUILD_MODE=%~1
if "%BUILD_MODE%"=="" set BUILD_MODE=%DEFAULT_BUILD_MODE%

if /i "%BUILD_MODE%"=="build" (
    set UV_BUILD_FLAG=-b
    set BUILD_DESC=增量编译
) else if /i "%BUILD_MODE%"=="rebuild" (
    set UV_BUILD_FLAG=-r
    set BUILD_DESC=全部重新编译
) else (
    echo "[错误] 无效的编译模式: %BUILD_MODE% (仅支持 build 或 rebuild)"
    pause
    exit /b 1
)

:: 解析参数2: 编译目标工程名，默认使用文件顶部的 DEFAULT_TARGET_NAME
set TARGET_NAME=%~2
if "%TARGET_NAME%"=="" set TARGET_NAME=%DEFAULT_TARGET_NAME%

:: 添加路径存在性检查
if not exist "%UV%" (
    echo "[错误] UV4.exe 未找到: %UV%"
    pause
    exit /b 1
)
if not exist "%UV_PRO_PATH%" (
    echo "[错误] 工程文件未找到: %UV_PRO_PATH%"
    pause
    exit /b 1
)

echo ---------------------------------------------------------------
echo Author:QHH

echo "project path: %UV_PRO_PATH%"
echo "UV path: %UV%"
echo "compile target: %TARGET_NAME%"
echo "build mode: %BUILD_MODE% (%BUILD_DESC%)"

echo Init building ...
echo >build_log.txt
%UV% -j100 %UV_BUILD_FLAG% -t %TARGET_NAME% %UV_PRO_PATH% -l %cd%\build_log.txt
type build_log.txt

echo Done.
:: pause
