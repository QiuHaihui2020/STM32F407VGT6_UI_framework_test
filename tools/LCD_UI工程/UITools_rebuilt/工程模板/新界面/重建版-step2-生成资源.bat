@echo off
rem 对应原厂的 step2-打开UI资源生成工具.bat。
rem 原厂那个是打开 QtToolBin 的界面让你点"生成资源文件(F5)"；
rem 重建版是命令行工具，直接一路做完：
rem   工程 json -> project.bin + ename.h + Resbuilder.xml + debug.txt
rem           -> result.bin/.str + 5 个头文件
rem           -> 调 copy_file.bat 拷进固件工程
rem 参数全部来自 config\ini\project.ini，和原厂读的是同一份配置。
setlocal
cd /d "%~dp0project"
set TOOLS=..\..\..\UITools_rebuilt

if not exist "%TOOLS%\QtToolBin.exe" (
    echo [step2] 找不到 %TOOLS%\QtToolBin.exe
    echo [step2] 先在 UITools_src 里跑 build.bat 和 mkdist_tooldir.bat
    pause
    exit /b 1
)
if not exist Resbuilder.xml if exist "%TOOLS%\Resbuilder.xml" copy /Y "%TOOLS%\Resbuilder.xml" . >nul

"%TOOLS%\QtToolBin.exe" --run-resbuilder "%TOOLS%\ResBuilder.exe"
set RC=%ERRORLEVEL%
echo.
if not "%RC%"=="0" (
    echo [step2] 失败，返回码 %RC%
) else (
    echo [step2] 完成。资源已生成并按 copy_file.bat 拷贝。
)
pause
endlocal
exit /b %RC%
