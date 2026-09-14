@echo off
rem ===========================================================================
rem  Assemble the tool dir that lives inside a UI project: <ui project>\tool\ .
rem
rem  Usage:  mkdist_tooldir.bat [target dir] [Qt root]
rem      defaults:  ..\tool   C:\Qt\5.15.2\msvc2019_64
rem
rem  Run build.bat first.
rem
rem  What this script puts in:
rem      UITools.exe                              editor + --gen + --pack
rem      Qt5*.dll  qt.conf  runtime\               Qt runtime (no env needed)
rem      README.md, self-check + clear scripts    from tool_src\tooldir\
rem      template\                                empty project to clone
rem
rem  What is NOT here any more, and why:
rem      the control library, the type-code table and the new-project skeleton
rem      are COMPILED INTO the exe (resources/assets.qrc) - they are read-only,
rem      so leaving them lying around was just noise. A file of the same name
rem      on disk still wins over the built-in one, so customising is unaffected
rem      (src\core\AssetPaths.h).
rem      compat\ stays in tool_src\ - the verification scripts are a
rem      development asset, not something the person drawing screens needs.
rem
rem  What DOES stay on disk, because it gets written during normal work:
rem      <project>\i18n_*.xls           the multi-language table (you edit it,
rem                                     and the packer opens it BY PATH)
rem      assets\widgets.d\              "save as widget" writes here
rem      assets\canvas\                 you drop background jpgs in here
rem      Both appear the first time they are used; nothing pre-creates them.
rem
rem  The tool dir sits inside the UI project it serves:
rem      <ui project>\project\     the design, opened from <ui project>\
rem      <ui project>\tool\        this directory
rem  so the project reaches the tools with a plain ..\tool and nothing has a
rem  tool-directory name baked into it.
rem
rem  The read-only data lives in resources/assets/ and is compiled in, so this
rem  script does not move it around at all. Copying such data from somewhere
rem  else on every build is how a deployed tree ends up silently disagreeing
rem  with what is committed.
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..\tool
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

rem ---- tool-dir extras, kept in the source tree so this stays reproducible ---
rem      README.md, the self-check script and the clear scripts
robocopy "%HERE%tooldir" "%OUT%" /E /NJH /NJS /NDL /NFL /NP >nul

rem ---- the empty project the new-project script clones -----------------------
rem      Composed from the same sources an existing project uses, so a new
rem      project and an existing one never drift apart:
rem          projectdir\screen\      file-association + new-project scripts,
rem                                  project\ui-config, project\copy_file.bat
rem          projectdir\newproject\  empty project.ini, pic dir
rem
rem      newproject\ is copied SECOND and overwrites same-named files. It used
rem      to carry its own project\ui-config with the pre-reorg UIToolkit paths,
rem      which silently clobbered the correct one from screen\ - every new
rem      project then shipped dead paths. Keep ui-config in screen\ only:
rem      one copy, no overwrite, no drift.
rem
rem      This one is read-only and would qualify for going into the exe, but
rem      its file names are Chinese and CMake/ninja cannot handle non-ASCII
rem      paths in a .qrc (the dependency comes out as "??????.bat" and the
rem      build fails - same family as the moc problem in build.bat). Mirroring
rem      them under ASCII names would leave two copies to drift apart, so it
rem      stays a real directory.
robocopy "%HERE%projectdir\screen"     "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul
robocopy "%HERE%projectdir\newproject" "%OUT%\template" /E /NJH /NJS /NDL /NFL /NP >nul

rem robocopy returns 1 for "files copied", which is success
if errorlevel 8 exit /b 1

echo.
echo [tooldir] done. The tool dir sits inside the UI project, so from
echo             ^<ui project^>\project
echo           the tools are reached as:
echo             ..\tool\UITools.exe --gen --run-resbuilder
exit /b 0
