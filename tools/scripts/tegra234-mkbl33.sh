#!/usr/bin/env bash
#
# tegra234-mkbl33.sh - build the Tegra234 bare-metal BL33 image.
#
# wolfBoot runs as BL33 (the "cpubl" slot, loaded at 0x272000000) and boots a
# signed test-app at EL2 (the base tegra234.config; no EL2->EL1 drop). There is
# no storage driver, so the payload and the device tree are bundled into the
# BL33 image at the hal/tegra234.h offsets and read straight out of DRAM:
#
#   [ wolfBoot | pad 0x200000 | signed test-app | pad 0x300000 | DTB ]
#
# The result overwrites wolfboot.bin: flash it to the A_cpu-bootloader
# partition, or load it non-persistently with the L4T "flash.sh --rcm-boot"
# flow. For the EL2->EL1 + DTB-in-x0 Linux boot use tegra234-mkpoc.sh instead.
set -e
cd "$(dirname "$0")/../.."

# NOTE: this overwrites .config, wolfboot.bin, and the signing keys in the
# working tree (it runs "make keysclean" so wolfBoot and the payload are built
# against one freshly generated key).

PAYLOAD_OFFSET=$((0x200000))   # TEGRA234_BUNDLE_OFFSET (hal/tegra234.h)
DTB_OFFSET=$((0x300000))       # TEGRA234_DTB_OFFSET (hal/tegra234.h)
MB2_CPUBL_MAX=$((0x400000))    # TEGRA234_CPUBL_MAX_SIZE (hal/tegra234.h)
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

# DTB path: pass as arg 1, or set L4T to your Linux_for_Tegra directory.
DTB="${1:-${L4T:+$L4T/kernel/dtb/tegra234-p3768-0000+p3767-0005-nv.dtb}}"
if [ -z "$DTB" ]; then
    echo "ERROR: no DTB given. Pass a DTB path as arg 1, or set L4T to your" >&2
    echo "       Linux_for_Tegra directory." >&2
    exit 1
fi
[ -f "$DTB" ] || { echo "ERROR: DTB not found: $DTB" >&2; exit 1; }
# An FDT starts with the magic d0 0d fe ed; catch a wrong file before it is
# baked into the image and only fails on the board.
if [ "$(od -An -tx1 -N4 "$DTB" | tr -d ' \n')" != "d00dfeed" ]; then
    echo "ERROR: not a device tree blob (bad FDT magic): $DTB" >&2; exit 1
fi

# Portable file size (GNU coreutils vs BSD/macOS stat).
filesize() {
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

# Pad a file out to $2 bytes ("truncate" is GNU-only).
pad_to() {
    dd if=/dev/null of="$1" bs=1 seek="$2" 2>/dev/null
}

cp config/examples/tegra234.config .config
# A failure here would silently reuse stale keys, so do not swallow it.
make keysclean >/dev/null
make clean >/dev/null
# Build wolfBoot and the signed test-app together so they share one key.
make CROSS_COMPILE="$CROSS_COMPILE" wolfboot.bin test-app/image_v1_signed.bin

core_sz=$(filesize wolfboot.bin)
[ "$core_sz" -le "$PAYLOAD_OFFSET" ] || { echo "ERROR: wolfBoot ($core_sz B) > 0x$(printf %x $PAYLOAD_OFFSET)" >&2; exit 1; }
app_sz=$(filesize test-app/image_v1_signed.bin)
[ $((PAYLOAD_OFFSET + app_sz)) -le "$DTB_OFFSET" ] || { echo "ERROR: payload overruns DTB offset" >&2; exit 1; }

cp wolfboot.bin bundle.bin
pad_to bundle.bin "$PAYLOAD_OFFSET"
cat test-app/image_v1_signed.bin >> bundle.bin
pad_to bundle.bin "$DTB_OFFSET"
cat "$DTB" >> bundle.bin

# Check the size budget before overwriting wolfboot.bin, so a rejected bundle
# leaves the plain bootloader binary intact.
total=$(filesize bundle.bin)
if [ "$total" -gt "$MB2_CPUBL_MAX" ]; then
    rm -f bundle.bin
    echo "ERROR: bundle ($total B) exceeds MB2 CPUBL cap (0x$(printf %x $MB2_CPUBL_MAX))" >&2; exit 1
fi
mv bundle.bin wolfboot.bin

echo "BL33 bundle -> wolfboot.bin: $total bytes"
echo "  wolfBoot core: $core_sz B"
echo "  signed test-app @ +0x$(printf %x $PAYLOAD_OFFSET): $app_sz B"
echo "  DTB             @ +0x$(printf %x $DTB_OFFSET): $(filesize "$DTB") B"
echo "  (under MB2 4 MB CPUBL cap: OK)"
