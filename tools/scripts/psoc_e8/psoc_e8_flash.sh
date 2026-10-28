#!/bin/bash
# Program wolfBoot and signed images into the KIT_PSE84_AI QSPI over KitProg3,
# using the ModusToolbox Programming Tools OpenOCD. Run from the wolfBoot root
# after `make`; partition addresses are taken from .config.
#
# Usage: psoc_e8_flash.sh [--boot image_v1_signed.bin] [--update image_v2_signed.bin]
#                         [--erase-keyvault]
#
# --erase-keyvault fills the wolfHSM NVM (WOLFBOOT_KEYVAULT_*) with 0xFF. Fresh
# RRAM reads as zero, which the NVM does not accept as empty, so a board needs
# this once before the first wolfHSM boot. It destroys any stored keys.
#
# Environment: MTB_PROGTOOLS (default /opt/Tools/ModusToolboxProgtools-1.6),
# KITPROG_SERIAL to pick the probe when several are attached.

set -e

MTB_PROGTOOLS=${MTB_PROGTOOLS:-/opt/Tools/ModusToolboxProgtools-1.6}
OCD=$MTB_PROGTOOLS/openocd
SCRIPTS=$MTB_PROGTOOLS/mtb-programmer/scripts
HERE=$(cd "$(dirname "$0")" && pwd)

config_addr() {
    local v
    v=$(sed -n "s/^$1?*=[[:space:]]*\(0x[0-9a-fA-F]*\).*/\1/p" .config | tail -1)
    [ -n "$v" ] || { echo "$1 not found in .config" >&2; exit 1; }
    printf "0x%08x" $(( (v & 0x03FFFFFF) | 0x60000000 ))
}
BOOT_ADDR=$(config_addr WOLFBOOT_PARTITION_BOOT_ADDRESS)
UPDATE_ADDR=$(config_addr WOLFBOOT_PARTITION_UPDATE_ADDRESS)

BOOT_IMG=test-app/image_v1_signed.bin
UPDATE_IMG=
ERASE_KEYVAULT=0
while [ $# -gt 0 ]; do
    case "$1" in
        --boot) BOOT_IMG=$2; shift 2 ;;
        --update) UPDATE_IMG=$2; shift 2 ;;
        --erase-keyvault) ERASE_KEYVAULT=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

BOOTIMG=$(mktemp --suffix=.bin)
BLANK=$(mktemp --suffix=.bin)
trap 'rm -f "$BOOTIMG" "$BLANK"' EXIT
python3 "$HERE/psoc_e8_boot_header.py" wolfboot.bin "$BOOTIMG" \
    $(( BOOT_ADDR - 0x60100000 ))

CMDS="init; reset init"
CMDS="$CMDS; flash write_image erase $BOOTIMG 0x60100000 bin"
CMDS="$CMDS; verify_image $BOOTIMG 0x60100000 bin"
CMDS="$CMDS; flash write_image erase $BOOT_IMG $BOOT_ADDR bin"
CMDS="$CMDS; verify_image $BOOT_IMG $BOOT_ADDR bin"
if [ -n "$UPDATE_IMG" ]; then
    CMDS="$CMDS; flash write_image erase $UPDATE_IMG $UPDATE_ADDR bin"
    CMDS="$CMDS; verify_image $UPDATE_IMG $UPDATE_ADDR bin"
fi
if [ "$ERASE_KEYVAULT" = 1 ]; then
    # The RRAM loader programs through the Non-secure system-bus alias.
    KV=$(sed -n 's/^WOLFBOOT_KEYVAULT_ADDRESS?*=[[:space:]]*\(0x[0-9a-fA-F]*\).*/\1/p' .config | tail -1)
    KV_SIZE=$(sed -n 's/^WOLFBOOT_KEYVAULT_SIZE?*=[[:space:]]*\(0x[0-9a-fA-F]*\).*/\1/p' .config | tail -1)
    [ -n "$KV" ] && [ -n "$KV_SIZE" ] || { echo "no WOLFBOOT_KEYVAULT_* in .config" >&2; exit 1; }
    head -c $((KV_SIZE)) /dev/zero | tr '\0' '\377' > "$BLANK"
    KV_NS=$(printf "0x%08x" $(( (KV & 0x0FFFFFFF) | 0x20000000 )))
    CMDS="$CMDS; flash write_image erase $BLANK $KV_NS bin"
fi
# "reset run" would leave the part in KitProg3 acquire mode.
CMDS="$CMDS; reset halt; resume; shutdown"

SERIAL_ARGS=()
if [ -n "$KITPROG_SERIAL" ]; then
    SERIAL_ARGS=(-c "adapter serial $KITPROG_SERIAL")
fi

"$OCD/bin/openocd" -s "$OCD/scripts" -s "$SCRIPTS" \
    -c "set SCRIPTS_DIR $SCRIPTS" \
    -f interface/kitprog3.cfg "${SERIAL_ARGS[@]}" -c "transport select swd" \
    -f CAT1D/kit_PSE84_AI_external.cfg \
    -c "$CMDS"
