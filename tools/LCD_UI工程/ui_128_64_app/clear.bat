@echo off
rem 清掉所有生成物，只留下工程源文件（json / config / 脚本）。
rem 对标原厂 ui_128_64_JL02\clear.bat，但按重建版实际会产出的文件来删。
setlocal
for /d %%S in ("%~dp0*") do (
    if exist "%%S\project" (
        echo [clear] %%~nxS
        pushd "%%S\project"
        del /q project.bin ename.h debug.txt Resbuilder.xml res_ver.h 2>nul
        del /q result.bin result.str result.h result.csv result.xml 2>nul
        del /q result_pic_index.h result_str_index.h 2>nul
        rem  原厂 clear.bat 这里还会 del config\*.png（文字预览图）——
        rem  那是原厂编辑器生成的，重建版**不产**这些图，删了就找不回来，
        rem  所以这里不删。
        popd
    )
)
echo [clear] done.
endlocal
exit /b 0
