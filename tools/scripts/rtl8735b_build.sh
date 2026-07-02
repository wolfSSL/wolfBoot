#!/usr/bin/env bash
# Build wolfBoot for the RealTek RTL8735B (AmebaPro2), SDK backend.
#
# `make TARGET=rtl8735b` compiles every wolfBoot object (including the RealTek
# SDK driver chain folded into hal/rtl8735b.o), but the final wolfboot.elf link
# needs the SDK SoC libraries (liboutsrc.a, libsoc_ntz.a) and the ROM symbol
# table (romsym_is.so), which live in the SDK build tree -- the default in-tree
# link cannot resolve them. This script compiles the objects, then performs that
# SDK-resolved final link. Run tools/scripts/amebapro2_package.sh afterward to
# produce the flashable flash_ntz.bin.
#
# Override via environment:
#   AMEBA_SDK   RealTek ameba-rtos-pro2 checkout
#   ASDK_PATH   ASDK 10.3.0 toolchain bin dir
set -e

AMEBA_SDK="${AMEBA_SDK:-$HOME/GitHub/ameba-rtos-pro2}"
ASDK_PATH="${ASDK_PATH:-$HOME/ameba-pro2-workspace/asdk/asdk-10.3.0/linux/newlib/bin}"
SDKREL="$AMEBA_SDK/project/realtek_amebapro2_v0_example/GCC-RELEASE"
CC="$ASDK_PATH/arm-none-eabi-gcc"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

# Temp files, cleaned on exit -- avoids clobbering parallel runs and the risk of
# writing through a pre-created symlink at a fixed /tmp path.
MAKE_ERR="$(mktemp)"
OBJS_FILE="$(mktemp)"
LINK_LD="$(mktemp)"
trap 'rm -f "$MAKE_ERR" "$OBJS_FILE" "$LINK_LD"' EXIT

if [ ! -d "$SDKREL/application/output" ]; then
    echo "error: SDK build output not found: $SDKREL/application/output" >&2
    echo "       set AMEBA_SDK and build the RealTek SDK first." >&2
    exit 1
fi

cp config/examples/rtl8735b.config .config

# Compile all objects. The default link of wolfboot.elf is expected to fail here
# (unresolved SDK/ROM symbols); capture the object list from its [LD] line.
make TARGET=rtl8735b 2>"$MAKE_ERR" \
    | grep -A1 '\[LD\] wolfboot.elf' | tail -1 > "$OBJS_FILE" || true
OBJS="$(cat "$OBJS_FILE")"
if ! echo "$OBJS" | grep -q 'hal/rtl8735b.o'; then
    echo "error: object compile failed (no hal/rtl8735b.o in link line). make stderr:" >&2
    tail -8 "$MAKE_ERR" >&2
    exit 1
fi

# Build the final linker script in a temp file rather than mutating the
# generated config/target.ld in place: prepend the ROM symbol table include.
# ld resolves the INCLUDE via the -L paths below, where romsym_is.so lives.
{ echo 'INCLUDE "romsym_is.so"'; cat config/target.ld; } > "$LINK_LD"

"$CC" -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=softfp \
    -ffreestanding -nostartfiles --specs=nosys.specs \
    -T "$LINK_LD" \
    -L "$SDKREL/ROM/GCC" -L "$SDKREL/application/output" \
    $OBJS \
    -Wl,--start-group \
        "$SDKREL/application/output/liboutsrc.a" \
        "$SDKREL/application/output/libsoc_ntz.a" \
        -lc -lgcc -lnosys -lm \
    -Wl,--end-group \
    -Wl,--gc-sections \
    -o wolfboot.elf

echo "built: wolfboot.elf ($(stat -c %s wolfboot.elf) bytes)"
echo "next:  tools/scripts/amebapro2_package.sh wolfboot.elf"
