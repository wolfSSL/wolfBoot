#!/bin/bash
# s32g2-mkfit.sh - build and sign the FIT (gzip kernel plus device tree,
# hal/s32g2.its) wolfBoot boots on the S32G2 GoldBox.
#
#   tools/scripts/s32g2/s32g2-mkfit.sh <Image or Image.gz> <version> <key.der> <board.dtb>
#
# Output: s32g2-fitImage_v<version>_signed.bin in the wolfBoot root. Copy it
# to the FAT32 boot partition as /fitImage_a.bin or /fitImage_b.bin
# (config/examples/s32g2-goldbox.config), or program it into a NOR partition.
# The file ends with 4 KB of padding: on a file slot wolfBoot keeps the boot
# confirmation state in its last bytes (DISK_BOOT_CONFIRM).
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
KERNEL="${1:?usage: $0 <Image|Image.gz> [version] [key.der] <board.dtb>}"
VERSION="${2:-1}"
KEY="${3:-$ROOT/wolfboot_signing_private_key.der}"
DTB="${4:?the device tree is required: $0 <Image> [version] [key.der] <board.dtb>}"
ITS="$ROOT/hal/s32g2.its"
SIGN_OPTS="${SIGN_OPTS:---ecc384 --sha384}"

[ -f "$KERNEL" ] || { echo "!! kernel not found: $KERNEL"; exit 1; }
[ -f "$KEY" ] || { echo "!! signing key not found: $KEY"; exit 1; }
# sign runs from the wolfBoot root: resolve the key against the caller's
# directory first.
KEY="$(cd "$(dirname "$KEY")" && pwd)/$(basename "$KEY")"
command -v mkimage >/dev/null || { echo "!! mkimage not found (u-boot-tools)"; exit 1; }

case "$KERNEL" in
    *.gz) [ "$KERNEL" -ef "$ROOT/Image.gz" ] || cp -L "$KERNEL" "$ROOT/Image.gz" ;;
    *)    gzip -9 -c "$KERNEL" > "$ROOT/Image.gz" ;;
esac
[ -f "$DTB" ] || { echo "!! dtb not found: $DTB"; exit 1; }
[ "$DTB" -ef "$ROOT/s32g2.dtb" ] || cp -L "$DTB" "$ROOT/s32g2.dtb"
echo "== building FIT ($ITS) =="
mkimage -f "$ITS" "$ROOT/s32g2-fitImage" >/dev/null
echo "== signing the FIT (v$VERSION, $SIGN_OPTS) =="
( cd "$ROOT" && ./tools/keytools/sign $SIGN_OPTS s32g2-fitImage "$KEY" "$VERSION" >/dev/null )
OUT="$ROOT/s32g2-fitImage_v${VERSION}_signed.bin"
[ -f "$OUT" ] || { echo "!! signing failed"; exit 1; }
head -c 4096 /dev/zero >> "$OUT"
ls -l "$OUT"
