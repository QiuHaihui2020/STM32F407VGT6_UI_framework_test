@echo off
rem ===========================================================================
rem  Assemble the tool dir that lives inside a UI project: <ui project>\tool\ .
rem
rem  Usage:  mkdist_tooldir.bat [target dir] [Qt root]
rem      defaults:  ..\UIProject\tool   C:\Qt\5.15.2\msvc2019_64
rem
rem  Run build.bat first.
rem
rem  What this script puts in:
rem      UITools.exe QtToolBin.exe ResBuilder.exe  the three tools
rem      Qt5*.dll platforms\ imageformats\ styles\ Qt runtime (no env needed)
rem      compat\                                  the verification scripts
rem      README.md, self-check + clear scripts    from UITools_src\tooldir\
rem      template\                                empty project to clone
rem
rem  What is ALREADY there and is NOT touched:
rem      assets\        widgets.json widgets.d\ typecodes.ini canvas\
rem                     i18n_*.xls
rem
rem  The tool dir sits inside the UI project it serves:
rem      <ui project>\project\     the design, opened from <ui project>\
rem      <ui project>\tool\        this directory
rem  so the project reaches the tools with a plain ..\tool and nothing has a
rem  tool-directory name baked into it.
rem
rem  assets\ is version-controlled in place rather than copied in on every run:
rem  copying it from somewhere else on each build means the deployed tree can
rem  silently disagree with what is committed, and it made this script depend
rem  on a directory that has nothing to do with building. Paths and fallbacks
rem  are in src\core\AssetPaths.h.
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..\UIProject\tool
set QT_DIR=%~2
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\5.15.2\msvc2019_64
set BUILD_DIR=C:\bt\uitools

if not exist "%BUILD_DIR%\UITools.exe" (
    echo [tooldir] %BUILD_DIR%\UITools.exe not found - run build.bat first
    exit /b 1
)

echo [tooldir] target: %OUT%
if not exist "%OUT%" mkdir "%OUT%"

rem ---- the three tools + Qt runtime -----------------------------------------
rem  No >nul here. dist.bat prints exactly why a copy failed (usually a running
rem  instance holding the .exe); swallowing that leaves a STALE build in the
rem  target dir and you end up debugging a binary you did not just build.
call "%HERE%dist.bat" "%OUT%" "%QT_DIR%" || exit /b 1

rem ---- verification scripts --------------------------------------------------
robocopy "%HERE%compat" "%OUT%\compat" *.py /XD __pycache__ /NJH /NJS /NDL /NFL /NP >nul

rem ---- tool-dir extras, kept in the source tree so this stays reproducible ---
rem      README.md, the self-check script and the clear scripts
robocopy "%HERE%tooldir" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul

rem ---- the empty project the new-project script clones -----------------------
rem      Composed from the same sources the project dir uses, so a new project
rem      and an existing one never drift apart:
rem          projectdir\screen\      file-association + new-project scripts,
rem                                  project\ui-config, project\copy_file.bat
rem          projectdir\newproject\  empty project.ini, pic dir
robocopy "%HERE%projectdir\screen"     "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%HERE%projectdir\newproject" "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul

rem robocopy returns 1 for "files copied", which is success
if errorlevel 8 exit /b 1

rem ---- the assets have to be present, or the editor comes up with an empty
rem      component panel and every node fails to resolve its type code -------
if not exist "%OUT%\assets\widgets.json" (
    echo [tooldir] WARNING: %OUT%\assets\widgets.json is missing
    echo [tooldir]          the component panel will be empty - restore assets\
)

echo.
echo [tooldir] done. The tool dir sits inside the UI project, so from
echo             ^<ui project^>\project
echo           the tools are reached as:
echo             ..\tool\QtToolBin.exe --run-resbuilder ..\tool\ResBuilder.exe
exit /b 0
