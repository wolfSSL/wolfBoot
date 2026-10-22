#!/bin/bash
#
# Make the RX71M flash images from the CC-RX e2 studio builds:
#   wolfboot.bin                code flash 0xFFFF0000-0xFFFFFFFF
#   app_RenesasRX01_v1_signed.bin, app_RenesasRX01_v2_signed.bin
#                               boot (0xFFE00000) / update (0xFFC00000) images
#
# Only code flash sections are copied, so the option-setting memory that
# the BSP places at 0x00120040 never ends up in an image.
#
# Usage: make_images.sh <RX_ELF_BIN_DIR> <SIGNING_KEY> [OUTPUT_DIR]
#   RX_ELF_BIN_DIR  folder with rx-elf-objcopy (GCC for Renesas RX)
#   SIGNING_KEY     the private key whose public part is in src/keystore.c
#   OUTPUT_DIR      default: the current directory
#

set -e

if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "Usage: $0 <RX_ELF_BIN_DIR> <SIGNING_KEY> [OUTPUT_DIR]"
    exit 1
fi

HERE=$(cd "$(dirname "$0")" && pwd)
WOLFBOOT_DIR=$(cd "$HERE/../../../.." && pwd)
OBJCOPY="$1/rx-elf-objcopy"
OBJDUMP="$1/rx-elf-objdump"
KEY="$2"
OUT="${3:-.}"
SIGN="$WOLFBOOT_DIR/tools/keytools/sign"
BOOT_X="$HERE/wolfBoot/HardwareDebug/wolfBoot.x"
APP_X="$HERE/app_RenesasRX01/HardwareDebug/app_RenesasRX01.x"

for f in "$OBJCOPY" "$OBJDUMP" "$SIGN"; do
    if [ ! -x "$f" ] && [ ! -x "$f.exe" ]; then
        echo "Not found: $f (for sign: make -C tools/keytools)"
        exit 1
    fi
done
for f in "$KEY" "$BOOT_X" "$APP_X"; do
    if [ ! -f "$f" ]; then
        echo "Not found: $f"
        exit 1
    fi
done

# Print "<vma> <section>" for each section with contents in [lo, hi)
sections() {
    "$OBJDUMP" -h "$1" | awk -v lo=$(($2)) -v hi=$(($3)) '
        function hex(s,  i, v) {
            v = 0
            s = tolower(s)
            for (i = 1; i <= length(s); i++)
                v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
            return v
        }
        $1 ~ /^[0-9]+$/ { name = $2; size = hex($3); vma = hex($4); next }
        name != "" && /CONTENTS/ && size > 0 && vma >= lo && vma < hi {
            printf "%.0f %s\n", vma, name
        }
        { name = "" }'
}

# Copy the code flash sections of $1 in [$3, $4) to binary file $2; the
# lowest section must start at $3
extract() {
    local args=() first="" vma name
    while read -r vma name; do
        args+=(-j "$name")
        if [ -z "$first" ] || [ "$vma" -lt "$first" ]; then
            first=$vma
        fi
    done < <(sections "$1" "$3" "$4")
    if [ ${#args[@]} -eq 0 ]; then
        echo "No sections in $1 between $3 and $4"
        exit 1
    fi
    if [ "$first" -ne $(($3)) ]; then
        printf "%s starts at 0x%X, not at %s\n" "$1" "$first" "$3"
        exit 1
    fi
    "$OBJCOPY" -O binary --gap-fill 0xFF "${args[@]}" "$1" "$2"
}

mkdir -p "$OUT"

echo "wolfBoot: $BOOT_X"
extract "$BOOT_X" "$OUT/wolfboot.bin" 0xFFFF0000 0x100000000
if [ "$(wc -c < "$OUT/wolfboot.bin")" -ne 65536 ]; then
    echo "wolfboot.bin must cover 0xFFFF0000-0xFFFFFFFF (65536 bytes)"
    exit 1
fi

echo "Application: $APP_X"
extract "$APP_X" "$OUT/app_RenesasRX01.bin" 0xFFE00100 0xFFFFFF80
# Header and application must end before the two partition flag sectors
APP_MAX=$((0x1D0000 - 2 * 0x8000 - 256))
if [ "$(wc -c < "$OUT/app_RenesasRX01.bin")" -gt $APP_MAX ]; then
    echo "app_RenesasRX01.bin is larger than $APP_MAX bytes"
    exit 1
fi

for v in 1 2; do
    "$SIGN" --ecc256 "$OUT/app_RenesasRX01.bin" "$KEY" $v
done

echo
echo "Program with Renesas Flash Programmer (code flash only):"
echo "  wolfboot.bin                   at FFFF0000"
echo "  app_RenesasRX01_v1_signed.bin  at FFE00000 (boot partition)"
echo "  app_RenesasRX01_v2_signed.bin  at FFC00000 (update partition)"
