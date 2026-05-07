#!/bin/bash
# Sign and stage a ZC702 Linux kernel for wolfBoot.
#
# Two modes (chosen by the APPENDED env var):
#
#   APPENDED=1 (default, recommended) - appends the DTB to the zImage and
#       signs the concatenation as one image. The kernel finds the DTB at the
#       end of itself via CONFIG_ARM_APPENDED_DTB. Required because the ARMv7
#       zImage decompressor is observed to lose r2 (the DTB physical pointer
#       wolfBoot passed in) before it reaches the decompressed kernel head.S
#       on Zynq-7000 - the kernel ends up with __atags_pointer = 0 and never
#       parses chosen.bootargs / chosen.stdout-path. Appending the DTB is
#       independent of r2.
#
#   APPENDED=0 - signs the zImage alone and stages the DTB raw at
#       WOLFBOOT_DTS_BOOT_ADDRESS. wolfBoot reads it via PART_DTS_BOOT and
#       relocates to WOLFBOOT_LOAD_DTS_ADDRESS, then passes that pointer in
#       r2 per the ARM Linux boot ABI. Useful for kernels/decompressors
#       that do preserve r2 correctly.
#
# Inputs (env):
#   ZIMAGE    - path to ARM zImage (default: ../linux-xlnx/arch/arm/boot/zImage)
#   DTB       - path to .dtb       (default: ../linux-xlnx/arch/arm/boot/dts/zynq-zc702.dtb)
#   VERSION   - image version      (default: 1)
#   APPENDED  - 0 or 1             (default: 1)
#
# Kernel must be built with:
#   APPENDED=1 -> CONFIG_ARM_APPENDED_DTB=y, CONFIG_ARM_ATAG_DTB_COMPAT=y
#   APPENDED=0 -> bootargs / stdout-path baked into the DTB ahead of time

set -e
# Parse .config (Makefile syntax: NAME ?= value or NAME = value) for the
# small set of variables we actually need. Whitelist parsing rather than
# eval'ing the file content - avoids shell-injection risk if the .config
# was copied from somewhere untrusted, and surfaces typos cleanly.
config_get() {
    local key="$1"
    awk -v k="$key" '
        $0 ~ "^"k"[[:space:]]*\\??=" {
            sub(/^[^=]+=/, "")
            sub(/^[[:space:]]+/, "")
            sub(/[[:space:]]+$/, "")
            print
            exit
        }
    ' .config 2>/dev/null
}
WOLFBOOT_PARTITION_BOOT_ADDRESS=$(config_get WOLFBOOT_PARTITION_BOOT_ADDRESS)
WOLFBOOT_DTS_BOOT_ADDRESS=$(config_get WOLFBOOT_DTS_BOOT_ADDRESS)
WOLFBOOT_PARTITION_SIZE=$(config_get WOLFBOOT_PARTITION_SIZE)

SIGN_TOOL="./tools/keytools/sign"
KEY="wolfboot_signing_private_key.der"

ZIMAGE="${ZIMAGE:-../linux-xlnx/arch/arm/boot/zImage}"
DTB="${DTB:-../linux-xlnx/arch/arm/boot/dts/zynq-zc702.dtb}"
VERSION="${VERSION:-1}"
APPENDED="${APPENDED:-1}"

[ -f "$ZIMAGE" ] || { echo "ERROR: kernel not found at $ZIMAGE" >&2; exit 1; }
[ -f "$DTB" ]    || { echo "ERROR: dtb not found at $DTB"       >&2; exit 1; }
[ -f "$KEY" ]    || { echo "ERROR: signing key $KEY not found"   >&2; exit 1; }

PSIZE=$((${WOLFBOOT_PARTITION_SIZE:-0x600000}))

if [ "$APPENDED" = "1" ]; then
    # Concatenate zImage + DTB, sign as single image.
    KDTB=$(mktemp /tmp/zImage_dtb.XXXXXX)
    # Single-quoted trap so $KDTB is expanded at trap-fire time, with the
    # path quoted internally - safe even if mktemp returns a path with
    # whitespace/metacharacters.
    trap 'rm -f "$KDTB"' EXIT
    cat "$ZIMAGE" "$DTB" > "$KDTB"
    SIZE=$(stat -c %s "$KDTB")

    if [ "$SIZE" -gt "$PSIZE" ]; then
        echo "ERROR: zImage+dtb ($SIZE bytes) exceeds WOLFBOOT_PARTITION_SIZE ($PSIZE)" >&2
        exit 1
    fi

    echo "Mode  : APPENDED (zImage + DTB concatenated, signed as one image)"
    echo "zImage: $ZIMAGE"
    echo "DTB   : $DTB"
    echo "Total : $SIZE bytes"
    echo "Signing as PART_BOOT v$VERSION ..."
    $SIGN_TOOL --ecc256 --sha256 "$KDTB" "$KEY" "$VERSION"

    SIGNED_OUT="${KDTB%.*}_v${VERSION}_signed.bin"
    [ -f "$SIGNED_OUT" ] || SIGNED_OUT="${KDTB}_v${VERSION}_signed.bin"
    mv "$SIGNED_OUT" "image_v${VERSION}_signed.bin"

    echo ""
    echo "Outputs:"
    ls -la "image_v${VERSION}_signed.bin"
    echo ""
    echo "Flash with (replace <FSBL> and <ID>):"
    echo "  program_flash -f image_v${VERSION}_signed.bin -offset ${WOLFBOOT_PARTITION_BOOT_ADDRESS} -flash_type qspi-x4-single -fsbl <FSBL> -target_id <ID>"
    echo ""
    echo "(No separate DTB programming needed - DTB is appended to zImage.)"
else
    # Sign zImage alone, copy DTB raw.
    KSIZE=$(stat -c %s "$ZIMAGE")
    DSIZE=$(stat -c %s "$DTB")

    if [ "$KSIZE" -gt "$PSIZE" ]; then
        echo "ERROR: zImage ($KSIZE bytes) exceeds WOLFBOOT_PARTITION_SIZE ($PSIZE)" >&2
        exit 1
    fi

    echo "Mode  : RAW DTB (zImage signed alone, DTB staged separately at PART_DTS_BOOT)"
    echo "zImage: $ZIMAGE  ($KSIZE bytes)"
    echo "DTB   : $DTB     ($DSIZE bytes)"
    echo "Signing kernel as PART_BOOT v$VERSION ..."
    $SIGN_TOOL --ecc256 --sha256 "$ZIMAGE" "$KEY" "$VERSION"

    SIGNED_OUT="${ZIMAGE%.*}_v${VERSION}_signed.bin"
    [ -f "$SIGNED_OUT" ] || SIGNED_OUT="${ZIMAGE}_v${VERSION}_signed.bin"
    mv "$SIGNED_OUT" "image_v${VERSION}_signed.bin"

    cp "$DTB" dtb.bin

    echo ""
    echo "Outputs:"
    ls -la "image_v${VERSION}_signed.bin" dtb.bin
    echo ""
    echo "Flash with (replace <FSBL> and <ID>):"
    echo "  program_flash -f image_v${VERSION}_signed.bin -offset ${WOLFBOOT_PARTITION_BOOT_ADDRESS} -flash_type qspi-x4-single -fsbl <FSBL> -target_id <ID>"
    echo "  program_flash -f dtb.bin                       -offset ${WOLFBOOT_DTS_BOOT_ADDRESS}       -flash_type qspi-x4-single -fsbl <FSBL> -target_id <ID>"
fi
