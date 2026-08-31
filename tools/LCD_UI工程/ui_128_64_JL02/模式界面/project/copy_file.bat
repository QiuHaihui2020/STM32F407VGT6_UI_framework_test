@echo off
rem ===========================================================================
rem  Called by the UI resource tool (ResBuilder) after generating resources.
rem  Copies the generated files into the STM32 project.
rem
rem  NOTE: keep this file ASCII-only. cmd.exe reads .bat in the system ANSI
rem  codepage; UTF-8 comments get mis-decoded and break parsing.
rem  Chinese explanation lives in User/ui_framework/port/ui_style.h.
rem
rem  This script sits in:
rem    STM32F407VGT6_Template\tools\LCD_UI...\ui_128_64_JL02\...\project\
rem  Going up:  ..(x1) ..(x2) ..(x3)=tools  ..(x4)  ..(x5)=STM32F407VGT6_Template
rem
rem  Differences from the original JL703 version (different tree layout):
rem    resources : tools\ui_resource\        ->  tools\JL\
rem    id header : apps\soundbox\include\ui\ ->  User\ui_framework\port\
rem                             style_JL02.h                 style_jl02.h
rem ===========================================================================

set RES_DIR=..\..\..\..\JL
set PORT_DIR=..\..\..\..\..\User\ui_framework\port

if not exist "%RES_DIR%\" (
    echo [copy_file] ERROR: resource dir not found: %RES_DIR%
    goto :fail
)
if not exist "%PORT_DIR%\" (
    echo [copy_file] ERROR: port dir not found: %PORT_DIR%
    goto :fail
)

rem ---- three resource files -------------------------------------------------
rem  Source names are fixed by the tool, do not rename:
rem    project.bin = window / control layout  -> JL.sty
rem    result.bin  = images                   -> JL.res
rem    result.str  = string images            -> JL.str
copy /Y ".\project.bin" "%RES_DIR%\JL.sty" > nul
if errorlevel 1 goto :fail
copy /Y ".\result.bin" "%RES_DIR%\JL.res" > nul
if errorlevel 1 goto :fail
copy /Y ".\result.str" "%RES_DIR%\JL.str" > nul
if errorlevel 1 goto :fail
echo [copy_file] resources -^> %RES_DIR%

rem ---- control / window id header ------------------------------------------
rem  ename.h holds the hashed ids (PAGE_0..PAGE_10 and every control).
rem  port\ui_style.h includes it and maps them to ID_WINDOW_* names.
rem  GENERATED FILE - do not edit by hand.
copy /Y ".\ename.h" "%PORT_DIR%\style_jl02.h" > nul
if errorlevel 1 goto :fail
echo [copy_file] id header -^> %PORT_DIR%\style_jl02.h

echo [copy_file] done.
echo [copy_file] NOTE: resources are staged in tools\JL\ ; they still need to
echo [copy_file]       be placed on the internal-flash FATFS disk. Target path
echo [copy_file]       is UI_PORT_RES_ROOT in port\ui_port_config.h.
exit /b 0

:fail
echo [copy_file] FAILED - check the paths and source files above.
exit /b 1
