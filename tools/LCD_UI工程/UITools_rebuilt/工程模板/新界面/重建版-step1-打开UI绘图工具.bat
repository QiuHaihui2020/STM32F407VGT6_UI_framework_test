@echo off
rem 对应原厂的 step1-打开UI绘图工具.bat，换成重建版的编辑器。
rem 工具目录、要打开的工程都不用写死：
rem   工具目录 = ..\..\..\UITools_rebuilt（编辑器自己按这个约定找）
rem   工程     = config\ini\project.ini 里的 projectfilename
cd /d "%~dp0project"
start "" "..\..\..\UITools_rebuilt\UITools.exe"
