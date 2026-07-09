#!/bin/bash
# Run the aarch64_efi wolfboot.efi in QEMU aarch64 UEFI for fast iteration/debug.
# No hardware / USB stick needed: a scratch dir is exposed to UEFI as a FAT ESP.
#
#   ./tools/scripts/aarch64-efi-qemu.sh            # run, auto-launch wolfboot.efi
#   ./tools/scripts/aarch64-efi-qemu.sh --gdb      # freeze at reset for gdb (-s -S)
#   ./tools/scripts/aarch64-efi-qemu.sh --fresh-vars # reset the UEFI NVRAM store
#
# In another shell for --gdb:
#   gdb-multiarch wolfboot.elf -ex 'target remote :1234'
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ESP="$ROOT/tools/qemu-esp"
CODE=/usr/share/AAVMF/AAVMF_CODE.fd
VARS_SRC=/usr/share/AAVMF/AAVMF_VARS.fd

for f in "$CODE" "$VARS_SRC"; do
    [ -r "$f" ] || { echo "Missing $f -- install qemu-efi-aarch64"; exit 1; }
done
command -v qemu-system-aarch64 >/dev/null || { echo "Install qemu-system-arm"; exit 1; }

mkdir -p "$ESP"
cp "$ROOT/wolfboot.efi" "$ESP"/
cp "$ROOT/aarch64_efi-stage/kernel.img" "$ESP"/ 2>/dev/null || true
# optional Linux kernel command line (M3): read by wolfboot as \cmdline.txt
cp "$ROOT/aarch64_efi-stage/cmdline.txt" "$ESP"/ 2>/dev/null || true
# auto-run wolfboot.efi from the UEFI shell
printf 'fs0:\r\nwolfboot.efi\r\n' > "$ESP/startup.nsh"

# Parse flags (order-independent).
GDB_ARGS=""
FRESH_VARS=0
for arg in "$@"; do
    case "$arg" in
        --gdb)        GDB_ARGS="-s -S" ;;
        --fresh-vars) FRESH_VARS=1 ;;
    esac
done

# Preserve the UEFI variable store (including any Secure Boot key enrollment)
# across runs; recreate it only when missing or when --fresh-vars is given.
VARS="$ROOT/tools/qemu-esp/AAVMF_VARS.fd"
if [ ! -f "$VARS" ] || [ "$FRESH_VARS" = "1" ]; then
    cp "$VARS_SRC" "$VARS"
fi

if [ -n "$GDB_ARGS" ]; then
    echo "GDB mode: qemu frozen. In another shell:"
    echo "  gdb-multiarch $ROOT/wolfboot.elf -ex 'target remote :1234'"
fi

exec qemu-system-aarch64 \
    -machine virt -cpu cortex-a72 -m 1024 -smp 1 \
    -drive if=pflash,format=raw,file="$CODE",readonly=on \
    -drive if=pflash,format=raw,file="$VARS" \
    -drive format=raw,file=fat:rw:"$ESP" \
    -net none -nographic $GDB_ARGS
