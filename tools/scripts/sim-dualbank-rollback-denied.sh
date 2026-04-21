#!/bin/bash
set -euo pipefail

if [ ! -f ".config" ]; then
    echo "Missing .config. Run make config first." >&2
    exit 1
fi

if ! grep -Eq '^(DUALBANK_SWAP(\?|)=1)' .config; then
    echo "DUALBANK_SWAP=1 is required for this simulation." >&2
    exit 1
fi

if [ ! -x "./wolfboot.elf" ]; then
    echo "wolfboot.elf not found. Build the simulator first." >&2
    exit 1
fi

if [ ! -f "./internal_flash.dd" ]; then
    echo "internal_flash.dd not found. Build test-sim-internal-flash-with-update first." >&2
    exit 1
fi

backup_image="$(mktemp ./internal_flash.rollback.XXXXXX)"
cp ./internal_flash.dd "$backup_image"
trap 'cp "$backup_image" ./internal_flash.dd; rm -f "$backup_image" sim_registers.dd' EXIT

rm -f sim_registers.dd

update_addr_hex="$(grep '^WOLFBOOT_PARTITION_UPDATE_ADDRESS=' .config | cut -d= -f2)"
if [ -z "${update_addr_hex}" ]; then
    echo "WOLFBOOT_PARTITION_UPDATE_ADDRESS is not set in .config." >&2
    exit 1
fi

update_addr=$((update_addr_hex))

# Corrupt UPDATE payload bytes so version metadata remains intact but
# image verification fails and boot logic attempts fallback.
printf '\x00\x00\x00\x00\x00\x00\x00\x00' | \
    dd of=./internal_flash.dd bs=1 seek="$((update_addr + 0x120))" conv=notrunc status=none

set +e
rollback_output="$(timeout 3s ./wolfboot.elf get_version 2>&1)"
rollback_rc=$?
set -e

if [ "$rollback_rc" -eq 0 ]; then
    echo "Expected rollback denial, but boot continued normally." >&2
    exit 1
fi

if [ "$rollback_rc" -ne 124 ] && [ "$rollback_rc" -ne 80 ]; then
    echo "Unexpected exit code while checking rollback denial: $rollback_rc" >&2
    echo "$rollback_output" >&2
    exit 1
fi

if ! printf '%s\n' "$rollback_output" | grep -q "Rollback to lower version not allowed"; then
    echo "Rollback denial message not found in output." >&2
    echo "$rollback_output" >&2
    exit 1
fi

echo "Dualbank rollback-to-older-version denial verified."
