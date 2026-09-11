@echo off
rem 一次性初装：把 .uiproj 关联到本工程自带的编辑器 —— 之后双击工程文件就能打开，
rem 文件图标也会变成编辑器的图标。
rem
rem 只写 HKCU（当前用户），**不需要管理员**，也不会影响别的用户。
rem 只碰 .uiproj：关联 .json 会抢走系统里所有 json 文件的双击，那是不能干的。
rem
rem 整个工程目录改名、搬到别的盘之后要重新跑一次（注册表里存的是绝对路径）。
setlocal
set EXE=%~dp0tool\UITools.exe
if not exist "%EXE%" (
    echo [关联] 找不到 %EXE%
    echo [关联] 先在 UITools_src 里跑 build.bat 和 mkdist_tooldir.bat
    pause
    exit /b 1
)

reg add "HKCU\Software\Classes\.uiproj" /ve /d "UITools.Project" /f >nul || goto :fail
reg add "HKCU\Software\Classes\UITools.Project" /ve /d "UI 工程" /f >nul || goto :fail
rem  DefaultIcon 的 ",0" = exe 里第一个图标组，也就是编辑器自己那个图标。
rem  图标组里 16/24/32/48/64/128/256 七个尺寸都有，资源管理器各种视图下都清晰。
reg add "HKCU\Software\Classes\UITools.Project\DefaultIcon" /ve /d "\"%EXE%\",0" /f >nul || goto :fail
reg add "HKCU\Software\Classes\UITools.Project\shell\open\command" /ve /d "\"%EXE%\" \"%%1\"" /f >nul || goto :fail

rem  资源管理器的图标缓存不会自己更新 —— 不刷的话文件还是白纸图标，
rem  看起来像"没关联上"。这一句让它重建缓存。
ie4uinit.exe -show >nul 2>nul

echo [关联] 好了。双击 project\*.uiproj 用这个打开：
echo        %EXE%
echo [关联] 文件图标也会变成这个 exe 的图标。
echo.
echo [关联] 图标要是还没变，在 project\ 里按 F5 刷新；再不行注销重登一次。
echo [关联] 如果双击弹"选择打开方式"，是系统里存过别的选择：
echo        在文件上右键 - 打开方式 - 选择其他应用 - 勾"始终"，选一次即可。
pause
endlocal
exit /b 0

:fail
echo [关联] 写注册表失败
pause
exit /b 1
