#!/usr/bin/env bash
# Build wolfBoot for U-Boot "go" (IMX8MM_BL33=0) and append the signed kernel.
# See "NXP i.MX 8M Mini" in docs/Targets.md.
#
# Usage: imx8mm-mkgoimage.sh Image DTB   -> wolfboot_linux.bin
# Env: CROSS_COMPILE, CONFIG, MAKE_ARGS, VERSION
# Overwrites .config and wolfboot.bin; uses the existing signing key.
set -e
# Paths from the caller are relative to the current directory
abspath() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
KERNEL="${1:+$(abspath "$1")}"
DTB="${2:+$(abspath "$2")}"
[ -z "${CONFIG:-}" ] || CONFIG="$(abspath "$CONFIG")"
cd "$(dirname "$0")/../../.."

KERNEL_OFFSET=$((0xC0000))    # kernel_addr (0x40540000) - 0x40480000

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
CONFIG="${CONFIG:-config/examples/imx8mm-pico.config}"
MAKE_ARGS="${MAKE_ARGS:-}"
VERSION="${VERSION:-1}"
OUT=wolfboot_linux.bin

[ -f "$KERNEL" ] && [ -f "$DTB" ] || {
    echo "Usage: $0 <kernel Image> <DTB>" >&2; exit 1; }

cp "$CONFIG" .config
make clean >/dev/null
# shellcheck disable=SC2086  # MAKE_ARGS must word-split into separate options
make CROSS_COMPILE="$CROSS_COMPILE" IMX8MM_BL33=0 $MAKE_ARGS wolfboot.bin
make keytools >/dev/null

core_sz=$(stat -c%s wolfboot.bin)
core_end_hex=$("${CROSS_COMPILE}"nm -n wolfboot.elf | \
    awk '$3 == "_end" { print $1; exit }')
[ -n "$core_end_hex" ] || {
    echo "ERROR: cannot find _end in wolfboot.elf" >&2; exit 1; }
core_end=$((16#$core_end_hex))
[ "$core_end" -le $((0x40480000 + KERNEL_OFFSET)) ] || {
    echo "ERROR: wolfBoot BSS overlaps the kernel offset" >&2; exit 1; }

tools/keytools/sign --rsa4096 --sha3 --dts "$DTB" "$KERNEL" \
    wolfboot_signing_private_key.der "$VERSION"
SIGNED="${KERNEL%.*}_v${VERSION}_signed.bin"

cp wolfboot.bin "$OUT"
dd if=/dev/zero bs=1 count=$((KERNEL_OFFSET - core_sz)) >> "$OUT" 2>/dev/null
cat "$SIGNED" >> "$OUT"
[ "$(stat -c%s "$OUT")" -le $((0x43000000 - 0x40480000)) ] || {
    echo "ERROR: $OUT overlaps the DTB at 0x43000000" >&2; exit 1; }

echo "$OUT: $(stat -c%s "$OUT") bytes (signed kernel at +0x$(printf %x $KERNEL_OFFSET))"
