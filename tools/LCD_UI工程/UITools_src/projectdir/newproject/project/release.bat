@echo off
rem Wipe every generated file, leaving only the project sources.
rem
rem NOTE: no "del config\*.png" here. Those are string-preview images
rem that this toolchain does not produce, so deleting them loses them for good.
del /q project.bin ename.h debug.txt Resbuilder.xml res_ver.h 2>nul
del /q result.bin result.str result.h result.csv result.xml 2>nul
del /q result_pic_index.h result_str_index.h 2>nul
