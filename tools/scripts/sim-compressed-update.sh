#!/bin/bash
# Simulated end-to-end test for a compressed update partition installed into
# a boot partition that keeps executing in place.
#
# Run after:
#   cp config/examples/sim-compressed-update.config .config
#   make keysclean && make && make test-sim-compressed-update-with-update

set -uo pipefail

if [ ! -f ".config" ]; then
    echo "Missing .config. Run make config first." >&2
    exit 1
fi

if ! grep -Eq '^(COMPRESSED_UPDATE(\?|)=1)' .config; then
    echo "COMPRESSED_UPDATE=1 is required for this simulation." >&2
    exit 1
fi

if [ ! -x "./wolfboot.elf" ]; then
    echo "wolfboot.elf not found. Build the simulator first." >&2
    exit 1
fi

cfg() {
    grep -E "^$1\??=" .config | tail -1 | cut -d= -f2
}

# An external update partition is not addressable, so wolfBoot reads the
# container through the external flash interface; the layout also puts it in
# a separate image file.
if grep -Eq '^EXT_FLASH\??=1' .config; then
    EXT=1
    FLASH_IMG=external_flash.dd
    if grep -Eq '^ENCRYPT\??=1' .config; then
        ENC=1
        MAKE_TARGET=test-sim-compressed-update-ext-enc-with-update
    else
        MAKE_TARGET=test-sim-compressed-update-ext-with-update
    fi
else
    EXT=0
    FLASH_IMG=internal_flash.dd
    MAKE_TARGET=test-sim-compressed-update-with-update
fi
ENC=${ENC:-0}

BOOT_SIZE=$(( $(cfg WOLFBOOT_PARTITION_SIZE) ))
UPDATE_SIZE=$(( $(cfg WOLFBOOT_PARTITION_UPDATE_SIZE) ))
UPDATE_OFF=$(( $(cfg WOLFBOOT_PARTITION_UPDATE_ADDRESS) ))
SECTOR=$(( $(cfg WOLFBOOT_SECTOR_SIZE) ))
V2=test-app/image_v2_signed_compressed.bin
# The sign tool cannot auto-detect the algorithm from a raw .der key, so
# pass it explicitly the way tools/test.mk does via SIGN_OPTIONS.
SIGN_FLAGS="--$(echo "$(cfg SIGN)" | tr 'A-Z' 'a-z') --$(echo "$(cfg HASH)" | tr 'A-Z' 'a-z')"

fail() { echo "FAIL: $*" >&2; exit 1; }
ok()   { echo "  ok: $*"; }

reset_flash() {
    make $MAKE_TARGET >/dev/null 2>&1 || \
        fail "could not rebuild the simulated flash"
    if [ "$ENC" = "1" ]; then
        # The application stores the image key and arms the update; the
        # test harness cannot write the key into the trailer itself.
        timeout 60 ./wolfboot.elf update_trigger >/dev/null 2>&1 || \
            fail "could not arm the encrypted update"
    fi
}

run_sim() {
    timeout 120 ./wolfboot.elf "$@" 2>&1
}

# Build the artifacts and the simulated flash up front, so the script works
# straight after a clean build.
reset_flash

if [ "$EXT" = "1" ]; then
    if [ "$ENC" = "1" ]; then
        echo "Update partition is on external flash, encrypted at rest."
    else
        echo "Update partition is on external flash."
    fi
fi

echo "1. the update partition is smaller than the boot partition"
# The point of the feature: the flash a second full copy would have cost is
# given to the application instead.
[ "$UPDATE_SIZE" -lt "$BOOT_SIZE" ] || \
    fail "update partition ($UPDATE_SIZE) is not smaller than boot ($BOOT_SIZE)"
ok "boot $BOOT_SIZE bytes, update $UPDATE_SIZE bytes"

[ -f "$V2" ] || fail "$V2 not found; run make test-sim-compressed-update-with-update"
csz=$(stat -c%s "$V2")
[ "$csz" -le "$((UPDATE_SIZE - SECTOR))" ] || \
    fail "container is $csz bytes, past the update partition's usable space"
ok "container is $csz bytes of $((UPDATE_SIZE - SECTOR)) usable"

echo "2. the update is inflated into the boot partition and booted in place"
reset_flash
out=$(run_sim get_version)
echo "$out" | grep -q "Inflating" || fail "the inflate path did not run"
ver=$(echo "$out" | tail -1)
[ "$ver" = "2" ] || fail "expected version 2 after the install, got '$ver'"
ok "inflated and booted version $ver"

echo "3. the install is consumed, not repeated on every boot"
out=$(run_sim get_version)
echo "$out" | grep -q "Inflating" && \
    fail "the update was reinstalled on the second boot"
ver=$(echo "$out" | tail -1)
[ "$ver" = "2" ] || fail "expected version 2 on the second boot, got '$ver'"
ok "second boot booted version $ver without reinstalling"

echo "4. a corrupt container is rejected before the boot partition is touched"
reset_flash
# Flip a byte in the compressed body. The signature covers the compressed
# bytes, so this must fail verification rather than reach the inflater.
dd if=/dev/zero of=$FLASH_IMG bs=1 \
    seek=$((UPDATE_OFF + 1024)) count=16 conv=notrunc status=none
out=$(run_sim get_version)
echo "$out" | grep -q "Inflating" && \
    fail "a corrupt container reached the inflater"
ver=$(echo "$out" | tail -1)
[ "$ver" = "1" ] || fail "expected version 1 to still be running, got '$ver'"
ok "corrupt container rejected, version $ver still booting"

echo "5. an image that will not fit the update partition is refused at signing"
cp test-app/image.elf test-app/image.toobig.elf
# Incompressible, and far larger than the update partition can hold.
dd if=/dev/urandom of=test-app/image.toobig.elf bs=1k count=$((UPDATE_SIZE / 1024)) \
    oflag=append conv=notrunc status=none
if WOLFBOOT_PARTITION_SIZE=$(cfg WOLFBOOT_PARTITION_SIZE) \
   WOLFBOOT_PARTITION_UPDATE_SIZE=$(cfg WOLFBOOT_PARTITION_UPDATE_SIZE) \
   WOLFBOOT_SECTOR_SIZE=$(cfg WOLFBOOT_SECTOR_SIZE) \
   WOLFBOOT_COMPRESSED_WINDOW=$(cfg WOLFBOOT_COMPRESSED_WINDOW) \
   ./tools/keytools/sign --compress $SIGN_FLAGS test-app/image.toobig.elf \
        wolfboot_signing_private_key.der 3 >/dev/null 2>&1; then
    rm -f test-app/image.toobig.elf test-app/image_v3_signed_compressed.bin
    fail "signing accepted a container larger than the update partition"
fi
rm -f test-app/image.toobig.elf test-app/image_v3_signed_compressed.bin
ok "signing refused a container that would not fit"

reset_flash
echo
# Resume after an interrupted install is covered by unit-compressed-install,
# which can place the already-written bytes and the progress flags together,
# and on hardware by cutting power mid-install. The simulator cannot
# interrupt itself at a deterministic point.
echo "Compressed update simulation successful."
