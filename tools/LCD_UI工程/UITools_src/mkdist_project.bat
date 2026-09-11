@echo off
rem ===========================================================================
rem  Build "ui_128_64_app" - a full working UI project laid out exactly
rem  The 128x64 dot-matrix UI project dir, driven by UIToolkit\.
rem
rem  Usage:  mkdist_project.bat [target dir] [source project family]
rem      defaults:  ..\ui_128_64_app   ..\ui_128_64_JL02
rem
rem  Copied from the source project (the actual design + assets):
rem      <screen>\project\*.json          the UI designs
rem      <screen>\project\config\         images, option.ini, project.ini
rem      <screen>\project\backgrounds\
rem      <screen>\project\copy_file.bat  release.bat  version.txt
rem
rem  NOT copied - caches / intermediates / generated output. This
rem  toolchain regenerates all of it, so shipping stale copies only confuses:
rem      autosave.json     editor autosave, 5 MB
rem      uitoolbin.bin     QtToolBin intermediate, 4.6 MB
rem      Resbuilder.dat    ResBuilder cache
rem      qtread.csv  imagelist.txt        stale caches with another PC's paths
rem      project.bin ename.h debug.txt Resbuilder.xml res_ver.h
rem      result.* result_pic_index.h result_str_index.h    <- step2 makes these
rem
rem  Overlaid from UITools_src\projectdir\ (so this stays reproducible):
rem      family\   -> target root      clear.bat, clear.sh, README.md
rem      screen\   -> every <screen>\  step1/step2/step3 scripts,
rem                                    project\Application Data\ui-config
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..\ui_128_64_app
set SRC=%~2
if "%SRC%"=="" set SRC=%HERE%..\ui_128_64_JL02

if not exist "%SRC%" (
    echo [project] source project family not found: %SRC%
    exit /b 1
)
if not exist "%HERE%projectdir\screen" (
    echo [project] missing %HERE%projectdir\screen
    exit /b 1
)

echo [project] source: %SRC%
echo [project] target: %OUT%

rem ---- per screen: copy the project payload, minus caches and generated ----
for /d %%S in ("%SRC%\*") do (
    if exist "%%S\project" (
        echo [project]   screen: %%~nxS
        robocopy "%%S\project" "%OUT%\%%~nxS\project" /E ^
            /XF autosave.json uitoolbin.bin Resbuilder.dat qtread.csv imagelist.txt ^
                project.bin ename.h debug.txt Resbuilder.xml res_ver.h ^
                result.bin result.str result.h result.csv result.xml ^
                result_pic_index.h result_str_index.h ^
            /XD "Application Data" ^
            /NJH /NJS /NDL /NFL /NP >nul
        if errorlevel 8 exit /b 1
        robocopy "%HERE%projectdir\screen" "%OUT%\%%~nxS" /E /NJH /NJS /NDL /NFL /NP >nul
        if errorlevel 8 exit /b 1
    )
)

rem ---- family-level scripts -------------------------------------------------
robocopy "%HERE%projectdir\family" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1

echo.
echo [project] done. In each screen dir there are now step1 / step2 / step3
echo [project] scripts pointing at ..\..\..\UIToolkit\ .
exit /b 0
