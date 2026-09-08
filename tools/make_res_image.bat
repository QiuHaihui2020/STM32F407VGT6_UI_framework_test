@echo off
rem Keil Before Build hook: regenerate the FATFS disk image and
rem User/fs/res_image.c via tools/make_res_image.py.
rem Skips file rewrite when inputs are unchanged, so incremental
rem builds stay incremental. Non-zero exit code aborts the build.
setlocal
set "SCRIPT_DIR=%~dp0"

set "PY=python"
where python >nul 2>nul
if not %errorlevel%==0 set "PY=py -3"

%PY% "%SCRIPT_DIR%make_res_image.py" --quiet
exit /b %errorlevel%
