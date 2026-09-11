#!/usr/bin/env bash
#
# imx8qm-mkflashbin.sh - build the i.MX 8QuadMax boot container with wolfBoot
# as BL33.
#
# wolfBoot replaces U-Boot in the NXP boot container. The boot ROM runs on the
# SCU Cortex-M4, loads SCFW (which trains DDR), SECO authenticates the
# container, ATF BL31 comes up at EL3 and enters BL33 - wolfBoot - at
# 0x80020000, the same address u-boot.bin links at on this SoC.
#
#   ROM (SCU) -> SCFW -> SECO -> ATF BL31 -> wolfBoot (BL33) -> signed payload
#
# There is no storage driver in the base config, so the signed payload and
# (for a Linux boot) the DTB are bundled into the BL33 image at the
# hal/imx8qm.h offsets and read straight out of DRAM:
#
#   [ wolfBoot | pad 0x200000 | signed payload | pad 0x300000 | DTB ]
#
# The bundle is then handed to imx-mkimage as the BL33 input, producing
# flash.bin. Write flash.bin to the boot medium at a 32 KB offset for SD/eMMC,
# or at offset 0 for FlexSPI NOR.
#
# Usage:
#   IMX_MKIMAGE=/path/to/imx-mkimage \
#   IMX_FIRMWARE=/path/to/dir/with/scfw+seco+bl31 \
#     tools/scripts/imx8qm/imx8qm-mkflashbin.sh [DTB]
#
# Build variants are make options, not separate configs. Pass them in MAKE_ARGS,
# e.g. for the Linux FIT build:
#   MAKE_ARGS="DISK_SDCARD=1 IMX8QM_MMU=1 EL2_HYPERVISOR=1 BOOT_EL1=1" \
#     tools/scripts/imx8qm/imx8qm-mkflashbin.sh imx8qm-mek.dtb
#
# Required in $IMX_FIRMWARE (all from the NXP BSP; none are redistributable):
#   scfw_tcm.bin                 System Controller firmware
#   mx8qmb0-ahab-container.img   SECO firmware container (B0 silicon)
#   bl31.bin                     ATF BL31, must fit the 128 KB slot
#
# soc.mak turns these into the container: u-boot.bin (here, wolfBoot) gets a
# hash appended, is placed at offset 128 KB after bl31.bin to form
# u-boot-atf.bin, and that blob is loaded at 0x80000000 - which is precisely
# why wolfBoot links at 0x80020000.
set -e
cd "$(dirname "$0")/../../.."

# NOTE: this overwrites .config, wolfboot.bin, and the signing keys in the
# working tree (it runs "make keysclean" so wolfBoot and the payload are built
# against one freshly generated key).

PAYLOAD_OFFSET=$((0x200000))   # IMX8QM_BUNDLE_OFFSET (hal/imx8qm.h)
DTB_OFFSET=$((0x300000))       # IMX8QM_DTB_OFFSET (hal/imx8qm.h)
BL33_MAX=$((0x400000))         # IMX8QM_BL33_MAX_SIZE (hal/imx8qm.h)
CROSS_COMPILE="${CROSS_COMPILE:-aarch64-none-elf-}"
CONFIG="${CONFIG:-config/examples/imx8qm-mek.config}"
# Extra make variables selecting a build variant (DISK_SDCARD=1, IMX8QM_MMU=1,
# ...). Deliberately unquoted at the call site so it word-splits.
MAKE_ARGS="${MAKE_ARGS:-}"

# DTB path: pass as arg 1. Optional - without it the bundle carries only the
# signed payload, which is what the bare-metal test-app config needs.
DTB="${1:-}"
if [ -n "$DTB" ]; then
    [ -f "$DTB" ] || { echo "ERROR: DTB not found: $DTB" >&2; exit 1; }
    # An FDT starts with the magic d0 0d fe ed; catch a wrong file before it is
    # baked into the image and only fails on the board.
    if [ "$(od -An -tx1 -N4 "$DTB" | tr -d ' \n')" != "d00dfeed" ]; then
        echo "ERROR: not a device tree blob (bad FDT magic): $DTB" >&2; exit 1
    fi
fi

# Portable file size (GNU coreutils vs BSD/macOS stat).
filesize() {
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

# Pad a file out to $2 bytes by appending zeros.
#
# NOT "dd if=/dev/null of=f seek=N": that relies on GNU dd truncating the
# output to the seek offset, which extends the file. BSD/macOS dd does not
# truncate, so with an empty input it writes nothing and leaves the file at its
# original size - the payload would then be concatenated directly after the
# wolfBoot core instead of at IMX8QM_BUNDLE_OFFSET, and the board would find
# padding where it expects the signed image. Appending an explicit zero gap
# behaves the same everywhere.
pad_to() {
    local cur pad
    cur=$(filesize "$1")
    pad=$(( $2 - cur ))
    if [ "$pad" -lt 0 ]; then
        echo "ERROR: $1 is already $cur B, past the $2 B pad target" >&2
        exit 1
    fi
    [ "$pad" -eq 0 ] && return 0
    dd if=/dev/zero bs=1024 count=$(( pad / 1024 )) >> "$1" 2>/dev/null
    dd if=/dev/zero bs=1 count=$(( pad % 1024 )) >> "$1" 2>/dev/null
}

cp "$CONFIG" .config
# A failure here would silently reuse stale keys, so do not swallow it.
make keysclean >/dev/null
make clean >/dev/null
# Build wolfBoot and the signed payload together so they share one key.
# shellcheck disable=SC2086  # MAKE_ARGS must word-split into separate options
make CROSS_COMPILE="$CROSS_COMPILE" $MAKE_ARGS wolfboot.bin test-app/image_v1_signed.bin

core_sz=$(filesize wolfboot.bin)
[ "$core_sz" -le "$PAYLOAD_OFFSET" ] || { echo "ERROR: wolfBoot ($core_sz B) > 0x$(printf %x $PAYLOAD_OFFSET)" >&2; exit 1; }
app_sz=$(filesize test-app/image_v1_signed.bin)
[ $((PAYLOAD_OFFSET + app_sz)) -le "$DTB_OFFSET" ] || { echo "ERROR: payload overruns DTB offset" >&2; exit 1; }

cp wolfboot.bin bundle.bin
pad_to bundle.bin "$PAYLOAD_OFFSET"
cat test-app/image_v1_signed.bin >> bundle.bin
if [ -n "$DTB" ]; then
    pad_to bundle.bin "$DTB_OFFSET"
    cat "$DTB" >> bundle.bin
fi

# Check the size budget before overwriting wolfboot.bin, so a rejected bundle
# leaves the plain bootloader binary intact.
total=$(filesize bundle.bin)
if [ "$total" -gt "$BL33_MAX" ]; then
    rm -f bundle.bin
    echo "ERROR: bundle ($total B) exceeds the BL33 cap (0x$(printf %x $BL33_MAX))" >&2; exit 1
fi
mv bundle.bin wolfboot.bin

echo "BL33 bundle -> wolfboot.bin: $total bytes"
echo "  wolfBoot core: $core_sz B"
echo "  signed payload @ +0x$(printf %x $PAYLOAD_OFFSET): $app_sz B"
if [ -n "$DTB" ]; then
    echo "  DTB            @ +0x$(printf %x $DTB_OFFSET): $(filesize "$DTB") B"
fi

# --- Container assembly ------------------------------------------------------
if [ -z "${IMX_MKIMAGE:-}" ] || [ -z "${IMX_FIRMWARE:-}" ]; then
    echo
    echo "IMX_MKIMAGE / IMX_FIRMWARE not set: stopping after the BL33 bundle."
    echo "Set both and re-run to produce flash.bin, or pass wolfboot.bin to"
    echo "imx-mkimage yourself in place of u-boot.bin."
    exit 0
fi

for f in scfw_tcm.bin mx8qmb0-ahab-container.img bl31.bin; do
    [ -f "$IMX_FIRMWARE/$f" ] || { echo "ERROR: missing $IMX_FIRMWARE/$f" >&2; exit 1; }
done

OUT="$IMX_MKIMAGE/iMX8QM"
mkdir -p "$OUT"
cp "$IMX_FIRMWARE/scfw_tcm.bin" "$OUT/"
cp "$IMX_FIRMWARE/mx8qmb0-ahab-container.img" "$OUT/"
cp "$IMX_FIRMWARE/bl31.bin" "$OUT/"
# imx-mkimage's iMX8QM target expects the BL33 input to be named u-boot.bin;
# soc.mak derives u-boot-hash.bin and u-boot-atf.bin from it.
cp wolfboot.bin "$OUT/u-boot.bin"

make -C "$IMX_MKIMAGE" SOC=iMX8QM flash
cp "$OUT/flash.bin" flash.bin
echo "flash.bin: $(filesize flash.bin) bytes"
echo
echo "Write it with tools/scripts/imx8qm/imx8qm-flash.sh, or by hand:"
echo "  SD/eMMC: dd if=flash.bin of=/dev/sdX bs=1k seek=32"
echo "  FlexSPI: program at offset 0"
