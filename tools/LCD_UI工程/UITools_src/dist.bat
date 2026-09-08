@echo off
rem ===========================================================================
rem  Package the three rebuilt tools into a self-contained folder.
rem
rem  Usage:  dist.bat [output dir] [Qt root]
rem          default output: C:\bt\dist
rem          default Qt    : C:\Qt\5.15.2\msvc2019_64
rem
rem  Run build.bat first. The result runs with NO environment setup at all -
rem  no PATH, no QT_PLUGIN_PATH - so it can be dropped next to the factory
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
set BUILD_DIR=C:\bt\uitools

if not exist "%BUILD_DIR%\UITools.exe" (
    echo [dist] %BUILD_DIR%\UITools.exe not found - run build.bat first
    exit /b 1
)
if not exist "%QT_DIR%\bin\Qt5Core.dll" (
    echo [dist] Qt not found: %QT_DIR%
    exit /b 1
)

if not exist "%OUT%"              mkdir "%OUT%"
if not exist "%OUT%\platforms"    mkdir "%OUT%\platforms"
if not exist "%OUT%\imageformats" mkdir "%OUT%\imageformats"
if not exist "%OUT%\styles"       mkdir "%OUT%\styles"

rem  A running instance holds its own .exe open and the copy fails. When this
rem  script's output is piped away that failure is invisible and the target dir
rem  silently keeps an OLD build - which then looks like "my fix did nothing".
rem  So name the file and say what to do.
for %%E in (UITools QtToolBin ResBuilder) do (
    copy /Y "%BUILD_DIR%\%%E.exe" "%OUT%" >nul
    if errorlevel 1 (
        echo [dist] ERROR: cannot copy %%E.exe into %OUT%
        echo [dist]        Close any running UITools / QtToolBin / ResBuilder,
        echo [dist]        then run this again. Otherwise the target dir keeps
        echo [dist]        an OLD build and you will be chasing ghosts.
        exit /b 1
    )
)

copy /Y "%QT_DIR%\bin\Qt5Core.dll"    "%OUT%" >nul || exit /b 1
copy /Y "%QT_DIR%\bin\Qt5Gui.dll"     "%OUT%" >nul || exit /b 1
copy /Y "%QT_DIR%\bin\Qt5Widgets.dll" "%OUT%" >nul || exit /b 1

copy /Y "%QT_DIR%\plugins\platforms\qwindows.dll" "%OUT%\platforms" >nul || exit /b 1
for %%F in (qjpeg qgif qico) do (
    if exist "%QT_DIR%\plugins\imageformats\%%F.dll" (
        copy /Y "%QT_DIR%\plugins\imageformats\%%F.dll" "%OUT%\imageformats" >nul
    )
)
if exist "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" (
    copy /Y "%QT_DIR%\plugins\styles\qwindowsvistastyle.dll" "%OUT%\styles" >nul
)

rem ---- MSVC runtime: only needed on machines without the VC++ redistributable
for /f "delims=" %%D in ('dir /b /s "%VCToolsRedistDir%x64\Microsoft.VC*.CRT\msvcp140.dll" 2^>nul') do (
    copy /Y "%%~dpD*.dll" "%OUT%" >nul
    goto :crt_done
)
echo [dist] NOTE: MSVC runtime not copied. If the target PC lacks the
echo [dist]       "Visual C++ 2015-2022 Redistributable (x64)", install it there.
:crt_done

echo.
echo [dist] done -^> %OUT%
echo [dist] runs with no environment setup; verify with:
echo         %OUT%\UITools.exe --dialog-smoke ^<project dir^>
echo         %OUT%\QtToolBin.exe ^<project.json^> --run-resbuilder %OUT%\ResBuilder.exe
endlocal
