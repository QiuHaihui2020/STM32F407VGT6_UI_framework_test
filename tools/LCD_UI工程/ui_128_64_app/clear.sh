#!/bin/sh
# 和 clear.bat 一样，给 Git Bash / WSL 用。
set -e
cd "$(dirname "$0")"
for s in */; do
    [ -d "$s/project" ] || continue
    echo "[clear] $s"
    cd "$s/project"
    rm -f project.bin ename.h debug.txt Resbuilder.xml res_ver.h
    rm -f result.bin result.str result.h result.csv result.xml
    rm -f result_pic_index.h result_str_index.h
    # 不删 config/*.png（文字预览图）：本工具链不产这些图，删了找不回来。
    cd - >/dev/null
done
echo "[clear] done."
