@echo off
rem ===========================================================================
rem  Package the three tools into a self-contained folder.
rem
rem  Usage:  dist.bat [output dir] [Qt root]
rem          default output: C:\bt\dist
rem          default Qt    : C:\Qt\5.15.2\msvc2019_64
rem
rem  Run build.bat first. The result runs with NO environment setup at all -
rem  no PATH, no QT_PLUGIN_PATH - so it can be dropped anywhere next to the
rem  tools and used directly. ~21 MB.
rem
rem  If your Qt install has windeployqt.exe, that is the canonical way:
rem      windeployqt --release --no-translations UITools.exe
rem  An aqtinstall "qtbase only" install has no windeployqt, hence the manual
rem  copy below. The plugin set is the minimum these three tools touch:
rem      platforms\qwindows.dll        required, Qt refuses to start without it
rem      imageformats\*.dll            jpg/gif/ico (bmp and png are built into QtGui)
rem      styles\qwindowsvistastyle.dll native look, optional but ugly without it
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
setlocal

set OUT=%~1
if "%OUT%"=="" set OUT=C:\bt\dist
set QT_DIR=%~2
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\5.15.2\msvc2019_64
rem  Static Qt (no bin\Qt5Core.dll) builds into its own dir and needs none of
rem  the runtime copying below - the exe carries everything.
set BUILD_DIR=C:\bt\uitools
set QT_STATIC=
if not exist "%QT_DIR%\bin\Qt5Core.dll" (
    set BUILD_DIR=C:\bt\uitools-static
    set QT_STATIC=1
)

if not exist "%BUILD_DIR%\UITools.exe" (
    echo [dist] %BUILD_DIR%\UITools.exe not found - run build.bat first
    exit /b 1
)
if not exist "%QT_DIR%\lib\cmake" (
    echo [dist] Qt not found: %QT_DIR%
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

rem  Dynamic Qt only: plugins go under runtime\ instead of three dirs next to
rem  the exe. Qt finds them through qt.conf (written below) - without that it
rem  only looks in .\platforms and refuses to start ("could not find or load
rem  the Qt platform plugin windows"). The three Qt5*.dll themselves must stay
rem  beside the exe: they are implicitly linked, so Windows resolves them
rem  before any of our code runs and qt.conf has no say in it.
if not defined QT_STATIC (
    if not exist "%OUT%\runtime"              mkdir "%OUT%\runtime"
    if not exist "%OUT%\runtime\platforms"    mkdir "%OUT%\runtime\platforms"
    if not exist "%OUT%\runtime\imageformats" mkdir "%OUT%\runtime\imageformats"
    if not exist "%OUT%\runtime\styles"       mkdir "%OUT%\runtime\styles"
)

rem  A running instance holds its own .exe open and the copy fails. When this
rem  script's output is piped away that failure is invisible and the target dir
rem  silently keeps an OLD build - which then looks like "my fix did nothing".
rem  So name the file and say what to do.
rem  One exe now: the resource generator and the packer are subcommands
rem  (--gen / --pack), see src\main.cpp dispatchSubcommand().
copy /Y "%BUILD_DIR%\UITools.exe" "%OUT%" >nul
if errorlevel 1 (
    echo [dist] ERROR: cannot copy UITools.exe into %OUT%
    echo [dist]        Close any running UITools, then run this again.
    echo [dist]        Otherwise the target dir keeps an OLD build and you
    echo [dist]        will be chasing ghosts.
    exit /b 1
)

if defined QT_STATIC goto :static_done

copy /Y "%QT_DIR%\bin\Qt5Core.dll"    "%OUT%" >nul || exit /b 1
copy /Y "%QT_DIR%\bin\Qt5Gui.dll"     "%OUT%" >nul || exit /b 1
copy /Y "%QT_DIR%\bin\Qt5Widgets.dll" "%OUT%" >nul || exit /b 1

copy /Y "%QT_DIR%\plugins\platforms\qwindows.dll" "%OUT%\runtime\platforms" >nul || exit /b 1
for %%F in (qjpeg qgif qico) do (
    if exist "%QT_DIR%\plugins\imageformats\%%F.dll" (
        copy /Y "%QT_DIR%\plugins\imageformats\%%F.dll" "%OUT%\runtime\imageformats" >nul
    )
)
if exist "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" (
    copy /Y "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" "%OUT%\runtime\styles" >nul
)

rem  Tells Qt where the plugins went. Paths are relative to the exe.
> "%OUT%\qt.conf" echo [Paths]
>>"%OUT%\qt.conf" echo Plugins = runtime

rem ---- MSVC runtime: only needed on machines without the VC++ redistributable
for /f "delims=" %%D in ('dir /b /s "%VCToolsRedistDir%x64\Microsoft.VC*.CRT\msvcp140.dll" 2^>nul') do (
    copy /Y "%%~dpD*.dll" "%OUT%" >nul
    goto :crt_done
)
echo [dist] NOTE: MSVC runtime not copied. If the target PC lacks the
echo [dist]       "Visual C++ 2015-2022 Redistributable (x64)", install it there.
:crt_done
goto :runtime_done

:static_done
rem  Static build: the exe carries Qt, the plugins and the CRT. Clear out
rem  whatever a previous dynamic dist left behind, or the target dir keeps
rem  showing dlls that nothing loads any more.
del /q "%OUT%\Qt5*.dll" "%OUT%\qt.conf" "%OUT%\msvcp140*.dll" ^
       "%OUT%\vcruntime140*.dll" "%OUT%\concrt140.dll" 2>nul
if exist "%OUT%\runtime" rmdir /s /q "%OUT%\runtime"
echo [dist] static Qt: one self-contained exe, no dll and no qt.conf
:runtime_done

echo.
echo [dist] done -^> %OUT%
echo [dist] runs with no environment setup; verify with:
echo         %OUT%\UITools.exe --dialog-smoke ^<project dir^>
echo         %OUT%\UITools.exe --gen ^<project file^> --run-resbuilder
endlocal
