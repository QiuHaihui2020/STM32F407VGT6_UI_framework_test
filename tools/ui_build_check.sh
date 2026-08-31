#!/bin/sh
# UI 框架单文件编译自检脚本(不依赖 Keil IDE, 直接调 armclang)
# 用法: sh tools/ui_build_check.sh [文件...]   不传参数则编译全部
set -u
CC="D:/Keil_v5/ARM/ARMCLANG/bin/armclang.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/../.ui_objs"
mkdir -p "$OUT"

INC="
-I$ROOT/User/ui_framework/include
-I$ROOT/User/ui_framework/include/ui/cpu/br27
-I$ROOT/User/ui_framework/compat
-I$ROOT/User/ui_framework/port
-I$ROOT/User/ui_framework/port/hal
-I$ROOT/Core/Inc
-I$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc
-I$ROOT/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
-I$ROOT/Drivers/CMSIS/Device/ST/STM32F4xx/Include
-I$ROOT/Drivers/CMSIS/Include
-I$ROOT/FreeRTOS
-I$ROOT/FreeRTOS/include
-I$ROOT/FreeRTOS/port/GCC/ARM_CM4F
-I$ROOT/RTT
-I$ROOT/FATFS/Target
-I$ROOT/FATFS/App
-I$ROOT/Middlewares/Third_Party/FatFs/src
"

FLAGS="--target=arm-arm-none-eabi -mcpu=cortex-m4 -mfpu=fpv4-sp-d16
-mfloat-abi=hard -c -fno-rtti -funsigned-char -fshort-enums
-fshort-wchar -gdwarf-4 -O1 -ffunction-sections -Wno-packed
-Wno-missing-variable-declarations -Wno-missing-prototypes
-Wno-missing-noreturn -Wno-sign-conversion -Wno-nonportable-include-path
-Wno-reserved-id-macro -Wno-unused-macros -Wno-documentation-unknown-command
-Wno-documentation -Wno-license-management -Wno-parentheses-equality
-DUSE_HAL_DRIVER -DSTM32F407xx -DUSE_USBD_COMPOSITE -D__MICROLIB"

if [ $# -gt 0 ]; then
    FILES="$*"
else
    FILES="$(find "$ROOT/User/ui_framework/platform" \
                  "$ROOT/User/ui_framework/lcd_drive" \
                  "$ROOT/User/ui_framework/ui_dot" \
                  "$ROOT/User/ui_framework/font" \
                  "$ROOT/User/ui_framework/ui_draw" \
                  "$ROOT/User/ui_framework/res" \
                  "$ROOT/User/ui_framework/port" -name '*.c' | sort)"
fi

fail=0; ok=0
for f in $FILES; do
    obj="$OUT/$(basename "$f" .c).o"
    if "$CC" $FLAGS $INC "$f" -o "$obj" 2> "$obj.log"; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "==== FAIL: ${f#$ROOT/} ===="
        grep -E "error:" "$obj.log" | head -8
    fi
done
echo "----------------------------------------"
echo "OK: $ok   FAIL: $fail"
