@echo off
rem ===========================================================================
rem  Seed a UI project dir from an existing project.
rem
rem  Usage:  mkdist_project.bat [target dir] [source project] [force]
rem      defaults:  ..   ..\..\ui_128_64_JL02
rem
rem  Shape produced (the tools go in with mkdist_tooldir.bat):
rem      <ui project>\
rem          file-association scripts, new-project script
rem          project\      the design - this is what you open
rem          tool\         exes + Qt + assets + self-check + clear
rem
rem  DESTRUCTIVE, so it refuses an existing target unless you pass "force" as
rem  the third argument. Once someone has edited the design in the editor,
rem  re-running this copies the source project's json back over it and that
rem  work is gone. Seeding is a one-time step.
rem
rem  Copied from the source project (the design + its assets):
rem      <screen>\project\*.json          the UI designs
rem      <screen>\project\config\         images, project.ini
rem      <screen>\project\version.txt
rem
rem  NOT copied - caches / intermediates / generated output. This
rem  toolchain regenerates all of it, so shipping stale copies only confuses:
rem      autosave.json     editor autosave, 5 MB
rem      uitoolbin.bin     intermediate from another toolchain, 4.6 MB
rem      Resbuilder.dat    packer cache
rem      qtread.csv  imagelist.txt        stale caches with another PC's paths
rem      project.bin ename.h debug.txt Resbuilder.xml res_ver.h
rem      result.* result_pic_index.h result_str_index.h    <- step2 makes these
rem      copy_file.bat     the overlay below brings this toolchain's own
rem      release.bat       wildcard deletes (del *.txt / *.xml / config\*.png)
rem                        that also wipe version.txt and the text preview
rem                        images - tool\clear.bat does the job precisely
rem
rem  Overlaid from tool_src\projectdir\ (so this stays reproducible):
rem      screen\   -> target root   file-association + new-project scripts,
rem                                 project\Application Data\ui-config,
rem                                 project\copy_file.bat
rem      family\   -> target root   README.md
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..
set SRC=%~2
if "%SRC%"=="" set SRC=%HERE%..\..\ui_128_64_JL02
set FORCE=%~3

if not exist "%SRC%" (
    echo [project] source project not found: %SRC%
    exit /b 1
)
if not exist "%HERE%projectdir\screen" (
    echo [project] missing %HERE%projectdir\screen
    exit /b 1
)
if exist "%OUT%" if not "%FORCE%"=="force" (
    echo [project] target already exists: %OUT%
    echo [project] re-seeding would overwrite the design in it with %SRC% .
    echo [project] pass "force" as the 3rd argument if that is really what you want.
    exit /b 1
)

echo [project] source: %SRC%
echo [project] target: %OUT%

rem ---- the design payload, minus caches and generated ------------------------
rem  The source keeps its screens one level down (<source>\<screen>\project);
rem  here the design sits directly in <target>\project, so take the first
rem  screen that actually has one.
set FOUND=
for /d %%S in ("%SRC%\*") do (
    if exist "%%S\project" if not defined FOUND (
        set FOUND=%%S
        echo [project]   from: %%~nxS
        robocopy "%%S\project" "%OUT%\project" /E ^
            /XF autosave.json uitoolbin.bin Resbuilder.dat qtread.csv imagelist.txt ^
                project.bin ename.h debug.txt Resbuilder.xml res_ver.h ^
                result.bin result.str result.h result.csv result.xml ^
                result_pic_index.h result_str_index.h copy_file.bat release.bat ^
            /XD "Application Data" ^
            /NJH /NJS /NDL /NFL /NP >nul
        if errorlevel 8 exit /b 1
    )
)
if not defined FOUND (
    echo [project] no ^<screen^>\project found under %SRC%
    exit /b 1
)

rem ---- overlays --------------------------------------------------------------
robocopy "%HERE%projectdir\screen" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1
robocopy "%HERE%projectdir\family" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul
if errorlevel 8 exit /b 1

echo.
echo [project] done. Now run mkdist_tooldir.bat to put the tools in
echo [project] %OUT%\tool , then open the project from %OUT% .
exit /b 0
