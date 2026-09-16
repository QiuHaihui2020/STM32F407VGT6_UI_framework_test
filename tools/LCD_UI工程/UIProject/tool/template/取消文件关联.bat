@echo off
rem 取消 .uiproj 的关联（撤销 注册文件关联.bat 写的那几项）。
setlocal
reg delete "HKCU\Software\Classes\.uiproj" /f >nul 2>nul
reg delete "HKCU\Software\Classes\UITools.Project" /f >nul 2>nul
ie4uinit.exe -show >nul 2>nul
echo [关联] 已取消。
echo [关联] 资源管理器里"打开方式"的记录（UserChoice）是系统自己存的，
echo        这里不动它；需要的话在文件右键里重新选一次。
pause
endlocal
exit /b 0
