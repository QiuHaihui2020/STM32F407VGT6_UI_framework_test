@echo off
rem 清掉全部生成物，只留下工程源文件（json / config / 脚本）。
rem 按本工具链实际会产出的文件来删 —— **不用通配符**，
rem 免得顺手删掉 version.txt、config\*.png 这些回不来的东西。
setlocal
if not exist "%~dp0..\project" (
    echo [clear] 上一级没有 project\ 目录
    exit /b 1
)
pushd "%~dp0..\project"
del /q project.bin ename.h debug.txt Resbuilder.xml res_ver.h 2>nul
del /q result.bin result.str result.h result.csv result.xml 2>nul
del /q result_pic_index.h result_str_index.h 2>nul
rem  同类脚本这里还会 del config\*.png（文字预览图）——
rem  本工具链**不产**这些图，删了就找不回来，所以这里不删。
popd
echo [clear] done.
endlocal
exit /b 0
