#!/bin/bash
#
# STM32N6 Flash Script for NUCLEO-N657X0-Q
# Programs NOR flash (MX25UM51245G on XSPI2) and loads wolfBoot to SRAM.
#
# Usage:
#   ./tools/scripts/stm32n6_flash.sh                  # Build and flash all
#   ./tools/scripts/stm32n6_flash.sh --skip-build     # Flash only
#   ./tools/scripts/stm32n6_flash.sh --app-only       # Flash app to NOR only
#   ./tools/scripts/stm32n6_flash.sh --test-update    # Flash v1 + v2 update
#   ./tools/scripts/stm32n6_flash.sh --halt           # Leave OpenOCD running

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SKIP_BUILD=0
APP_ONLY=0
TEST_UPDATE=0
LEAVE_RUNNING=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-build)   SKIP_BUILD=1; shift ;;
        --app-only)     APP_ONLY=1; shift ;;
        --test-update)  TEST_UPDATE=1; shift ;;
        --halt)         LEAVE_RUNNING=1; shift ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo -e "${RED}Unknown option: $1${NC}"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${WOLFBOOT_ROOT}"

OPENOCD_CFG="${WOLFBOOT_ROOT}/config/openocd/openocd_stm32n6.cfg"
BOOT_ADDR=0x70020000
UPDATE_ADDR=0x70120000

check_tool() {
    command -v "$1" &>/dev/null || { echo -e "${RED}Error: $1 not found${NC}"; exit 1; }
}

check_tool openocd
[ $SKIP_BUILD -eq 0 ] && check_tool arm-none-eabi-gcc
[ -f "${OPENOCD_CFG}" ] || { echo -e "${RED}Error: ${OPENOCD_CFG} not found${NC}"; exit 1; }

echo -e "${GREEN}=== STM32N6 Flash Script ===${NC}"

# Build
if [ $SKIP_BUILD -eq 0 ]; then
    echo -e "${GREEN}[1/2] Building...${NC}"

    if [ ! -f .config ]; then
        [ -f config/examples/stm32n6.config ] || { echo -e "${RED}No .config found${NC}"; exit 1; }
        cp config/examples/stm32n6.config .config
    fi

    TARGET_CHECK=$(grep -E '^TARGET\?*=' .config | head -1 | sed 's/.*=//;s/[[:space:]]//g')
    [ "$TARGET_CHECK" = "stm32n6" ] || { echo -e "${RED}TARGET is '${TARGET_CHECK}', expected 'stm32n6'${NC}"; exit 1; }

    make clean && make wolfboot.bin
    echo -e "${GREEN}wolfboot.bin: $(stat -c%s wolfboot.bin 2>/dev/null || stat -f%z wolfboot.bin) bytes${NC}"

    make test-app/image_v1_signed.bin
    echo -e "${GREEN}image_v1_signed.bin: $(stat -c%s test-app/image_v1_signed.bin 2>/dev/null || stat -f%z test-app/image_v1_signed.bin) bytes${NC}"

    if [ $TEST_UPDATE -eq 1 ]; then
        SIGN_VALUE=$(grep -E '^SIGN\?*=' .config | head -1 | sed 's/.*=//;s/[[:space:]]//g')
        HASH_VALUE=$(grep -E '^HASH\?*=' .config | head -1 | sed 's/.*=//;s/[[:space:]]//g')
        [ -f tools/keytools/sign ] || make -C tools/keytools
        ./tools/keytools/sign \
            --$(echo "$SIGN_VALUE" | tr '[:upper:]' '[:lower:]') \
            --$(echo "$HASH_VALUE" | tr '[:upper:]' '[:lower:]') \
            test-app/image.bin wolfboot_signing_private_key.der 2
        echo -e "${GREEN}image_v2_signed.bin built${NC}"
    fi
else
    echo -e "${YELLOW}[1/2] Skipping build${NC}"
fi

# Verify binaries
[ $APP_ONLY -eq 0 ] && [ ! -f wolfboot.bin ] && { echo -e "${RED}wolfboot.bin not found${NC}"; exit 1; }
[ -f test-app/image_v1_signed.bin ] || { echo -e "${RED}image_v1_signed.bin not found${NC}"; exit 1; }
[ $TEST_UPDATE -eq 1 ] && [ ! -f test-app/image_v2_signed.bin ] && { echo -e "${RED}image_v2_signed.bin not found${NC}"; exit 1; }

# Flash via OpenOCD
echo -e "${GREEN}[2/2] Programming via OpenOCD...${NC}"
pkill -x openocd 2>/dev/null || true
sleep 1

OPENOCD_CMDS="reset init; "

if [ $APP_ONLY -eq 0 ]; then
    echo -e "${CYAN}  wolfboot.bin -> SRAM 0x34000000${NC}"
    OPENOCD_CMDS+="load_image ${WOLFBOOT_ROOT}/wolfboot.bin 0x34000000 bin; "
fi

echo -e "${CYAN}  image_v1_signed.bin -> NOR ${BOOT_ADDR}${NC}"
OPENOCD_CMDS+="flash write_image erase ${WOLFBOOT_ROOT}/test-app/image_v1_signed.bin ${BOOT_ADDR}; "

if [ $TEST_UPDATE -eq 1 ]; then
    echo -e "${CYAN}  image_v2_signed.bin -> NOR ${UPDATE_ADDR}${NC}"
    OPENOCD_CMDS+="flash write_image erase ${WOLFBOOT_ROOT}/test-app/image_v2_signed.bin ${UPDATE_ADDR}; "
fi

# Boot wolfBoot from SRAM (reset would clear SRAM, so we jump directly)
if [ $APP_ONLY -eq 0 ]; then
    # Extract initial SP (word 0) and entry point (word 1) from vector table
    INIT_SP=$(od -A n -t x4 -N 4 "${WOLFBOOT_ROOT}/wolfboot.bin" | awk '{print "0x"$1}')
    ENTRY_ADDR=$(od -A n -j 4 -t x4 -N 4 "${WOLFBOOT_ROOT}/wolfboot.bin" | awk '{print "0x"$1}')
    ENTRY_THUMB=$(printf "0x%08x" $(( ${ENTRY_ADDR} | 1 )))
    echo -e "${CYAN}  Booting wolfBoot (SP: ${INIT_SP}, entry: ${ENTRY_THUMB})...${NC}"
    OPENOCD_CMDS+="reg msplim_s 0x00000000; "
    OPENOCD_CMDS+="reg psplim_s 0x00000000; "
    OPENOCD_CMDS+="reg msp ${INIT_SP}; "
    OPENOCD_CMDS+="mww 0xE000ED08 0x34000000; "  # VTOR
    OPENOCD_CMDS+="mww 0xE000ED28 0xFFFFFFFF; "  # Clear CFSR
    OPENOCD_CMDS+="resume ${ENTRY_THUMB}; "
fi

if [ $LEAVE_RUNNING -eq 0 ]; then
    OPENOCD_CMDS+="shutdown"
else
    OPENOCD_CMDS+="echo {OpenOCD running. Connect via: telnet localhost 4444}"
fi

openocd -f "${OPENOCD_CFG}" -c "${OPENOCD_CMDS}"

echo -e "${GREEN}=== Done ===${NC}"
