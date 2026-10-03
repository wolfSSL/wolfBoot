#!/bin/bash
#
# STM32H7S Flash Script for NUCLEO-H7S3L8
# Programs wolfBoot to internal flash and the signed application to the
# external Octo-SPI NOR (MX25UW25645G on XSPI2).
#
# Usage:
#   ./tools/scripts/stm32h7s_flash.sh                  # Build and flash all
#   ./tools/scripts/stm32h7s_flash.sh --skip-build     # Flash only
#   ./tools/scripts/stm32h7s_flash.sh --app-only       # Flash app to NOR only
#   ./tools/scripts/stm32h7s_flash.sh --test-update    # Flash v1 + a v2 update
#   ./tools/scripts/stm32h7s_flash.sh --probe          # Identify target only
#
# Environment:
#   STLINK_SERIAL   ST-LINK serial number. Pass this whenever more than one
#                   probe is attached, otherwise the tool picks the first it
#                   finds and programs the wrong board.
#   EXT_LOADER      Override the external memory loader .stldr
#   PROGRAMMER_CLI  Override the STM32_Programmer_CLI to use

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SKIP_BUILD=0
APP_ONLY=0
TEST_UPDATE=0
PROBE_ONLY=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)   SKIP_BUILD=1; shift ;;
        --app-only)     APP_ONLY=1; shift ;;
        --test-update)  TEST_UPDATE=1; shift ;;
        --probe)        PROBE_ONLY=1; shift ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo -e "${RED}Unknown option: $1${NC}"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${WOLFBOOT_ROOT}"

# Partition layout comes from .config so the script tracks the build.
# The boot partition is an absolute address in the XSPI2 window; update and
# swap are device-relative offsets, so they need the window base added to
# become programmer addresses.
XSPI_BASE=0x70000000
cfg_val() {
    grep -E "^$1\??=" "${WOLFBOOT_ROOT}/.config" 2>/dev/null \
        | head -1 | sed 's/.*=//;s/[[:space:]]//g'
}
BOOT_ADDR=$(cfg_val WOLFBOOT_PARTITION_BOOT_ADDRESS)
UPDATE_REL=$(cfg_val WOLFBOOT_PARTITION_UPDATE_ADDRESS)
HDR_SIZE=$(cfg_val IMAGE_HEADER_SIZE)
SIGN_ALG=$(cfg_val SIGN)
HASH_ALG=$(cfg_val HASH)
[ -z "$BOOT_ADDR" ]  && BOOT_ADDR=0x70020000
[ -z "$UPDATE_REL" ] && UPDATE_REL=0x00420000
[ -z "$HDR_SIZE" ]   && HDR_SIZE=1024
[ -z "$SIGN_ALG" ]   && SIGN_ALG=ECC256
[ -z "$HASH_ALG" ]   && HASH_ALG=SHA256
# The v1 image comes from make, which signs with the .config algorithm, so
# the update has to use the same one or wolfBoot rejects it and the test
# silently exercises the failure path.
SIGN_FLAG="--$(echo "$SIGN_ALG" | tr 'A-Z' 'a-z')"
HASH_FLAG="--$(echo "$HASH_ALG" | tr 'A-Z' 'a-z')"
UPDATE_ADDR=$(printf "0x%08x" $(( ${XSPI_BASE} + ${UPDATE_REL} )))

# wolfBoot itself lives in internal flash
INTERNAL_ADDR=0x08000000

# Pick the newest STM32_Programmer_CLI available. Version matters: releases
# older than 2.23 have been seen either not to enumerate this board's ST-LINK
# at all, or to push it into firmware-upgrade mode, which needs a USB replug
# to recover.
if [ -z "$PROGRAMMER_CLI" ]; then
    # Version-sorted, newest first. A plain lexical sort would rank
    # stm32cubeide_1.9 above stm32cubeide_1.20.
    mapfile -t CLI_DIRS < <(printf '%s\n' \
        /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*/tools/bin \
        2>/dev/null | sort -V -r)
    for d in "${CLI_DIRS[@]}"; do
        if [ -x "$d/STM32_Programmer_CLI" ]; then
            PROGRAMMER_CLI="$d/STM32_Programmer_CLI"
            break
        fi
    done
fi
if [ -z "$PROGRAMMER_CLI" ] || [ ! -x "$PROGRAMMER_CLI" ]; then
    echo -e "${RED}Error: STM32_Programmer_CLI not found${NC}"
    echo "Set PROGRAMMER_CLI to a CubeProgrammer 2.23 or newer binary."
    exit 1
fi
CLI_BIN_DIR="$(dirname "${PROGRAMMER_CLI}")"

# The external loader that drives the on-board NOR
if [ -z "$EXT_LOADER" ]; then
    EXT_LOADER="${CLI_BIN_DIR}/ExternalLoader/MX25UW25645G_NUCLEO-H7S3L8.stldr"
fi
if [ ! -f "$EXT_LOADER" ]; then
    echo -e "${RED}Error: external loader not found:${NC} $EXT_LOADER"
    echo "Set EXT_LOADER to the .stldr matching the NOR on your board."
    exit 1
fi

CONNECT="port=swd mode=UR"
if [ -n "$STLINK_SERIAL" ]; then
    CONNECT="${CONNECT} sn=${STLINK_SERIAL}"
else
    echo -e "${YELLOW}Warning: STLINK_SERIAL not set.${NC} If more than one"
    echo "ST-LINK is attached the first one found will be programmed."
fi

prog() { "$PROGRAMMER_CLI" -c ${CONNECT} "$@"; }

echo -e "${CYAN}CubeProgrammer :${NC} ${PROGRAMMER_CLI}"
echo -e "${CYAN}External loader:${NC} $(basename "${EXT_LOADER}")"
echo -e "${CYAN}wolfBoot       :${NC} ${INTERNAL_ADDR} (internal flash)"
echo -e "${CYAN}Boot partition :${NC} ${BOOT_ADDR}"
echo -e "${CYAN}Update part.   :${NC} ${UPDATE_ADDR} (device ${UPDATE_REL})"

if [ $PROBE_ONLY -eq 1 ]; then
    prog | grep -E "Board|Device ID|Device name|Revision ID" || true
    exit 0
fi

if [ $SKIP_BUILD -eq 0 ]; then
    command -v arm-none-eabi-gcc >/dev/null || {
        echo -e "${RED}Error: arm-none-eabi-gcc not found${NC}"; exit 1; }
    echo -e "${CYAN}Building...${NC}"
    make
fi

if [ $APP_ONLY -eq 0 ]; then
    echo -e "${CYAN}Programming wolfBoot to internal flash...${NC}"
    prog -d wolfboot.bin "${INTERNAL_ADDR}" -v
fi

echo -e "${CYAN}Programming application to the NOR...${NC}"
prog -el "${EXT_LOADER}" -d test-app/image_v1_signed.bin "${BOOT_ADDR}" -v

if [ $TEST_UPDATE -eq 1 ]; then
    # IMAGE_HEADER_SIZE is not optional here. The sign tool defaults to a
    # 256-byte manifest; this target uses 1024, and a mismatch produces an
    # image whose firmware starts at the wrong offset. wolfBoot then parses
    # the header but hashes the wrong bytes, reporting
    # "Update verify failed: Hdr 1, Hash 0, Sig 0".
    echo -e "${CYAN}Signing v2 ${SIGN_FLAG} ${HASH_FLAG}${NC}"
    IMAGE_HEADER_SIZE="${HDR_SIZE}" tools/keytools/sign \
        "${SIGN_FLAG}" "${HASH_FLAG}" \
        test-app/image.bin wolfboot_signing_private_key.der 2
    echo -e "${CYAN}Programming the update to ${UPDATE_ADDR}...${NC}"
    prog -el "${EXT_LOADER}" -d test-app/image_v2_signed.bin \
        "${UPDATE_ADDR}" -v
fi

echo -e "${CYAN}Resetting...${NC}"
prog -rst >/dev/null

if [ $TEST_UPDATE -eq 1 ]; then
    # The application asks for the update and then stays running rather
    # than resetting itself, so a second reset is what actually applies it.
    echo -e "${CYAN}Letting the application request the update...${NC}"
    sleep 4
    echo -e "${CYAN}Resetting again to apply it...${NC}"
    prog -rst >/dev/null
fi

echo -e "${GREEN}Done.${NC} Console is USART3 on the ST-LINK VCP at 115200."
