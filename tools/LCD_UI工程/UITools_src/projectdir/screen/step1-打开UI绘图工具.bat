@echo off
rem 开编辑器。对标原厂同名脚本，换成重建版。
rem 工具目录和要打开的工程都不用传：
rem   工具目录 = ..\..\..\UIToolkit（编辑器按这个约定自己找）
rem   工程     = project\config\ini\project.ini 里的 projectfilename
cd /d "%~dp0project"
if not exist "..\..\..\UIToolkit\UITools.exe" (
    echo [step1] 找不到 ..\..\..\UIToolkit\UITools.exe
    echo [step1] 先在 UITools_src 里跑 build.bat 和 mkdist_tooldir.bat
    pause
    exit /b 1
)
start "" "..\..\..\UIToolkit\UITools.exe"
