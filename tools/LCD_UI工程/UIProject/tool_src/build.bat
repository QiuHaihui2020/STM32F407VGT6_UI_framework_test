@echo off
rem ===========================================================================
rem  Dot-matrix UI toolchain - one-shot build
rem
rem  Builds three executables:
rem      UITools.exe     layout editor (replaces ui-tools.exe)
rem  One executable with three entry points (subcommands):
rem      UITools.exe          the layout editor
rem      UITools.exe --gen    project file -> project.bin / ename.h / Resbuilder.xml
rem      UITools.exe --pack   Resbuilder.xml + bmp + xls -> result.bin / result.str
rem
rem  Usage:  build.bat [Qt root]        default: C:/Qt/5.15.2/msvc2019_64
rem
rem  Needs:  Visual Studio 2022 (MSVC) + CMake + Ninja + Qt 5.15 (msvc2019_64)
rem          Only the qtbase component is required (~33 MB):
rem              pip install aqtinstall
rem              python -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 ^
rem                     --archives qtbase --outputdir C:\Qt
rem          Slow official mirror? add: -b https://mirrors.ustc.edu.cn/qtproject
rem
rem  KEEP THIS FILE ASCII-ONLY. cmd.exe reads .bat in the system ANSI codepage;
rem  UTF-8 Chinese comments get mis-decoded and break parsing. The Chinese
rem  explanation lives in README.md instead.
rem
rem  Non-ASCII source path: this tree sits under tools\LCD_UI<chinese>\ .
rem  moc writes the header's ABSOLUTE path into the generated #include and
rem  converts it with the local 8-bit codec, which mangles the Chinese into
rem  "??" and yields C1083. CMakeLists.txt works around it with
rem  AUTOMOC_MOC_OPTIONS "-p;.".  CMake itself still mishandles a non-ASCII
rem  -S argument on some consoles, so this script creates an ASCII junction
rem  (C:\bt\src) pointing at the source tree and builds through that.
rem ===========================================================================
setlocal

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=C:/Qt/5.15.2/msvc2019_64

rem  A build dir per Qt kit. CMake caches the Qt5_DIR it found in CMakeCache.txt,
rem  so pointing -DCMAKE_PREFIX_PATH at a different Qt in the SAME dir does
rem  nothing - it silently keeps using the previous kit and you get a build that
rem  still needs the dlls, with no hint that anything went wrong.
set BUILD_DIR=C:\bt\uitools
if not exist "%QT_DIR%/bin/Qt5Core.dll" set BUILD_DIR=C:\bt\uitools-static
set LINK_DIR=C:\bt\src
set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
    echo [build] vcvars64.bat not found, edit VCVARS above
    exit /b 1
)
if not exist "%QT_DIR%/lib/cmake" (
    echo [build] Qt not found: %QT_DIR%
    echo [build] usage: build.bat C:/Qt/5.15.2/msvc2019_64
    echo [build]        build.bat C:/Qt/5.15.2-static   ^(one self-contained exe^)
    exit /b 1
)

rem ---- ASCII junction so CMake never sees the non-ASCII path ---------------
if not exist C:\bt mkdir C:\bt
if exist "%LINK_DIR%" rmdir "%LINK_DIR%"
mklink /J "%LINK_DIR%" "%~dp0." >nul || exit /b 1

call "%VCVARS%" >nul || exit /b 1

cmake -S "%LINK_DIR%" -B "%BUILD_DIR%" -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_CXX_COMPILER=cl ^
      -DCMAKE_PREFIX_PATH=%QT_DIR% || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1

echo.
echo [build] output: %BUILD_DIR%\UITools.exe
echo [build] before running, put Qt's bin on PATH:
echo         set PATH=%QT_DIR:/=\%\bin;%%PATH%%
echo         set QT_PLUGIN_PATH=%QT_DIR:/=\%\plugins
echo.
echo [build] full-chain acceptance test:
echo         python "%~dp0compat\verify_toolchain.py" %BUILD_DIR% ^<project dir^>
endlocal
