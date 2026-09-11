@echo off
rem ===========================================================================
rem  Assemble "UIToolkit" - the ready-to-use tool directory
rem  directory, laid out exactly the same way so the existing relative-path
rem  convention (..\..\..\UITools\) keeps working.
rem
rem  Usage:  mkdist_tooldir.bat [target dir] [asset source dir] [Qt root]
rem      defaults:  ..\UIToolkit   ..\UITools   C:\Qt\5.15.2\msvc2019_64
rem
rem  Run build.bat first.
rem
rem  What goes in:
rem      UITools.exe QtToolBin.exe ResBuilder.exe  the three tools
rem      Qt5*.dll platforms\ imageformats\ styles\ Qt runtime (no env needed)
rem      config\ini\option.ini                    control type-code table
rem      control\                                 control library + ex\ templates
rem      backgrounds\                             canvas backgrounds
rem      Application Data\ui-config               editor defaults
rem      *.xls                                    multi-language table
rem      Resbuilder.xml                           template, copied into a project
rem                                               that has none yet
rem      re\                                      the verification scripts
rem      README.md, new-project script, project template
rem                                               from UITools_src\tooldir\
rem
rem  The config assets are COPIED into the target (about 250 KB total) so
rem  the target dir is self-contained: you can delete or rename the source
rem  UITools\ and everything still works.
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..\UIToolkit
set FACT=%~2
if "%FACT%"=="" set FACT=%HERE%..\UITools
set QT_DIR=%~3
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\5.15.2\msvc2019_64
set BUILD_DIR=C:\bt\uitools

if not exist "%BUILD_DIR%\UITools.exe" (
    echo [tooldir] %BUILD_DIR%\UITools.exe not found - run build.bat first
    exit /b 1
)
if not exist "%FACT%\control\control.json" (
    echo [tooldir] asset source dir not found: %FACT%
    exit /b 1
)

echo [tooldir] target: %OUT%
if not exist "%OUT%" mkdir "%OUT%"

rem ---- the three tools + Qt runtime -----------------------------------------
rem  No >nul here. dist.bat prints exactly why a copy failed (usually a running
rem  instance holding the .exe); swallowing that leaves a STALE build in the
rem  target dir and you end up debugging a binary you did not just build.
call "%HERE%dist.bat" "%OUT%" "%QT_DIR%" || exit /b 1

rem ---- config assets ---------------------------------------------------------
robocopy "%FACT%\config"            "%OUT%\config"            /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%FACT%\control"           "%OUT%\control"           /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%FACT%\backgrounds"       "%OUT%\backgrounds"       /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%FACT%\Application Data"  "%OUT%\Application Data"  /E /NJH /NJS /NDL /NFL /NP >nul
copy /Y "%FACT%\Resbuilder.xml" "%OUT%" >nul
copy /Y "%FACT%\*.xls"          "%OUT%" >nul

rem ---- verification scripts --------------------------------------------------
robocopy "%HERE%re" "%OUT%\re" *.py /XD __pycache__ /NJH /NJS /NDL /NFL /NP >nul

rem ---- tool-dir extras, kept in the source tree so this stays reproducible ---
rem      README.md and the new-project script
robocopy "%HERE%tooldir" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul

rem ---- the empty project template that the new-project script clones --------
rem      Composed from the same sources the project dir uses, so the
rem      step scripts never drift between "new project" and "existing project":
rem          projectdir\screen\      step1/2/3 + ui-config
rem          projectdir\newproject\  empty project.ini, copy_file.bat, pic dir
robocopy "%HERE%projectdir\screen"     "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%HERE%projectdir\newproject" "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul

rem robocopy returns 1 for "files copied", which is success
if errorlevel 8 exit /b 1

echo.
echo [tooldir] done. Relative-path layout is:
echo             cd ^<project^>
echo             ..\..\..\UIToolkit\QtToolBin.exe --run-resbuilder ..\..\..\UIToolkit\ResBuilder.exe
exit /b 0
