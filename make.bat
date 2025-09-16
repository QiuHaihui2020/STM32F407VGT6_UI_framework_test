@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

:: 设置MDK安装路径（根据实际安装路径修改）
set UV=D:\Keil_v5\UV4\UV4.exe

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

:: 设置编译目标（根据实际需求修改）
set TARGET_NAME=RAM_Debug

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

echo Init building ...
echo >build_log.txt
%UV% -j100 -b -t %TARGET_NAME% %UV_PRO_PATH% -l %cd%\build_log.txt
type build_log.txt

echo Done.
:: pause
