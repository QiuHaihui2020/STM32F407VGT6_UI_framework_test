@echo off
rem 对应原厂的 step2-打开UI资源生成工具.bat：弹出 QtToolBin 的界面，
rem 在界面上点「生成资源文件(F5)」才开始生成。
rem 界面上那几项（JSON文件 / 工程ID / 不重新生成资源文件 / 调用脚本 / 旋转 /
rem 版本配置）就是 project\config\ini\project.ini 里的内容，点生成时写回去
rem —— 和原厂同一份配置。
rem
rem 不想弹窗、想一条命令跑完的话，用命令行模式：
rem   cd project
rem   ..\..\..\UITools_rebuilt\QtToolBin.exe --run-resbuilder ..\..\..\UITools_rebuilt\ResBuilder.exe
cd /d "%~dp0project"
set TOOLS=..\..\..\UITools_rebuilt

if not exist "%TOOLS%\QtToolBin.exe" (
    echo [step2] 找不到 %TOOLS%\QtToolBin.exe
    echo [step2] 先在 UITools_src 里跑 build.bat 和 mkdist_tooldir.bat
    pause
    exit /b 1
)
if not exist Resbuilder.xml if exist "%TOOLS%\Resbuilder.xml" copy /Y "%TOOLS%\Resbuilder.xml" . >nul

start "" "%TOOLS%\QtToolBin.exe"
