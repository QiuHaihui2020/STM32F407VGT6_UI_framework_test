@echo off
rem ===========================================================================
rem  Called by QtToolBin after the resources are generated.
rem  Copies them into the STM32 project. Adjust the two paths below if your
rem  tree differs.
rem
rem  Layout assumed:
rem    <repo>\tools\LCD_UI...\<ui project>\project\   <- you are here
rem    ..(x1)=ui project ..(x2)=LCD_UI... ..(x3)=tools ..(x4)=<repo>
rem    (the tools live next door in <ui project>\tool\)
rem
rem  KEEP THIS FILE ASCII-ONLY (cmd.exe reads .bat in the system ANSI codepage).
rem ===========================================================================
set RES_DIR=..\..\..\JL
set HDR_DIR=..\..\..\..\User\ui_framework\include\common

if not exist "%RES_DIR%\" (
    echo [copy_file] ERROR: resource dir not found: %RES_DIR%
    goto :fail
)
if not exist "%HDR_DIR%\" (
    echo [copy_file] ERROR: header dir not found: %HDR_DIR%
    goto :fail
)

copy /Y ".\project.bin" "%RES_DIR%\JL.sty" > nul || goto :fail
copy /Y ".\result.bin"  "%RES_DIR%\JL.res" > nul || goto :fail
copy /Y ".\result.str"  "%RES_DIR%\JL.str" > nul || goto :fail
echo [copy_file] resources -^> %RES_DIR%

rem  ename.h carries the control ids. It MUST be copied together with the
rem  resources - the ids inside JL.sty are the ones defined in here.
copy /Y ".\ename.h" "%HDR_DIR%\style_jl02.h" > nul || goto :fail
echo [copy_file] id header -^> %HDR_DIR%\style_jl02.h

echo [copy_file] done.
exit /b 0

:fail
echo [copy_file] FAILED
exit /b 1
