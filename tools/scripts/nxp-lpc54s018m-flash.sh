#!/bin/bash
#
# NXP LPC54S018M-EVK Flash Script
#
# End-to-end helper to build wolfBoot, sign the test application, and
# program the LPC54S018M-EVK SPIFI flash via pyocd. Also optionally
# exercises the A/B update flow by signing a v2 image and loading it
# into the update partition so wolfBoot will swap on the next boot.
#
# Flow:
#   1. Copy config/examples/nxp_lpc54s018m.config to .config
#   2. make -> factory.bin (wolfBoot + signed v1 test-app)
#   3. Parse .config to derive partition/trailer addresses
#   4. Erase BOOT and UPDATE partition trailer sectors (clean boot state)
#   5. pyocd flash factory.bin @ 0x10000000 (SPIFI base)
#   6. With --test-update: sign v2, flash at WOLFBOOT_PARTITION_UPDATE_ADDRESS
#
# Requirements:
#   - pyocd + LPC54S018J4MET180 target pack
#   - arm-none-eabi-gcc toolchain
#   - LPC-Link2 probe running CMSIS-DAP firmware (see docs/Targets.md:
#     "LPC54S018M: Link2 debug probe setup")
#
# Customization (LPC540xx / LPC54S0xx family):
#   Override CONFIG_FILE, PYOCD_TARGET, or CROSS_COMPILE via environment to
#   reuse this script for other LPC540xx / LPC54S0xx boards.
#
# See also: docs/Targets.md section
#   "NXP LPC540xx / LPC54S0xx (SPIFI boot) -> LPC54S018M: Testing firmware update"
#

set -e

# Configuration (can be overridden via environment variables)
CONFIG_FILE="${CONFIG_FILE:-config/examples/nxp_lpc54s018m.config}"
PYOCD_TARGET="${PYOCD_TARGET:-lpc54s018j4met180}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parse command line arguments
SKIP_BUILD=0
SKIP_FLASH=0
TEST_UPDATE=0

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --test-update    Build v2 update image and flash to update partition"
    echo "  --skip-build     Skip the build step (use existing binaries)"
    echo "  --skip-flash     Skip flashing (just build)"
    echo "  -h, --help       Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  CONFIG_FILE      Override config file (default: config/examples/nxp_lpc54s018m.config)"
    echo "  PYOCD_TARGET     Override pyocd target (default: lpc54s018j4met180)"
    echo "  CROSS_COMPILE    Override toolchain prefix (default: arm-none-eabi-)"
    echo ""
    echo "Examples:"
    echo "  $0                          # Build and flash factory.bin (v1 only)"
    echo "  $0 --test-update            # Build v1 + v2, flash both partitions"
    echo "  $0 --skip-flash             # Build without flashing"
    echo "  $0 --test-update --skip-build  # Flash existing v1 + v2 images"
    echo ""
    echo "Requirements:"
    echo "  - pyocd: pip install pyocd"
    echo "  - Target pack: pyocd pack install ${PYOCD_TARGET}"
    echo "  - ARM GCC: arm-none-eabi-gcc"
    echo ""
    echo "Note: The firmware swap takes ~60 seconds after the update is triggered."
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --test-update)
            TEST_UPDATE=1
            shift
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --skip-flash)
            SKIP_FLASH=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            ;;
    esac
done

echo -e "${GREEN}=== NXP LPC54S018M-EVK Flash Script ===${NC}"

# Function to parse all needed values from .config file
parse_config() {
    local config_file="$1"
    if [ ! -f "$config_file" ]; then
        echo -e "${RED}Error: Config file not found: ${config_file}${NC}"
        exit 1
    fi

    # Helper function to extract config value
    get_config_value() {
        local key="$1"
        grep -E "^${key}" "$config_file" | head -1 | sed -E "s/^${key}\??=//" | tr -d '[:space:]'
    }

    # Extract SIGN and HASH
    SIGN_VALUE=$(get_config_value "SIGN")
    HASH_VALUE=$(get_config_value "HASH")

    # Extract partition layout
    WOLFBOOT_PARTITION_BOOT_ADDRESS=$(get_config_value "WOLFBOOT_PARTITION_BOOT_ADDRESS")
    WOLFBOOT_PARTITION_UPDATE_ADDRESS=$(get_config_value "WOLFBOOT_PARTITION_UPDATE_ADDRESS")
    WOLFBOOT_PARTITION_SIZE=$(get_config_value "WOLFBOOT_PARTITION_SIZE")
    WOLFBOOT_SECTOR_SIZE=$(get_config_value "WOLFBOOT_SECTOR_SIZE")

    # Validate required fields
    local missing=""
    [ -z "$SIGN_VALUE" ] && missing="${missing}SIGN "
    [ -z "$HASH_VALUE" ] && missing="${missing}HASH "
    [ -z "$WOLFBOOT_PARTITION_BOOT_ADDRESS" ] && missing="${missing}WOLFBOOT_PARTITION_BOOT_ADDRESS "
    [ -z "$WOLFBOOT_PARTITION_UPDATE_ADDRESS" ] && missing="${missing}WOLFBOOT_PARTITION_UPDATE_ADDRESS "
    [ -z "$WOLFBOOT_PARTITION_SIZE" ] && missing="${missing}WOLFBOOT_PARTITION_SIZE "
    [ -z "$WOLFBOOT_SECTOR_SIZE" ] && missing="${missing}WOLFBOOT_SECTOR_SIZE "

    if [ -n "$missing" ]; then
        echo -e "${RED}Error: Missing required config values: ${missing}${NC}"
        exit 1
    fi

    # Convert SIGN/HASH to lowercase flag format for sign tool
    SIGN_FLAG="--$(echo "$SIGN_VALUE" | tr '[:upper:]' '[:lower:]')"
    HASH_FLAG="--$(echo "$HASH_VALUE" | tr '[:upper:]' '[:lower:]')"

    # Ensure partition addresses have 0x prefix for bash arithmetic
    for var in WOLFBOOT_PARTITION_BOOT_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS WOLFBOOT_PARTITION_SIZE WOLFBOOT_SECTOR_SIZE; do
        eval "val=\$$var"
        if [[ ! "$val" =~ ^0x ]]; then
            eval "$var=\"0x\${val}\""
        fi
    done

    # Compute trailer sector addresses (last sector of each partition)
    BOOT_TRAILER_SECTOR=$(printf "0x%X" $(( ${WOLFBOOT_PARTITION_BOOT_ADDRESS} + ${WOLFBOOT_PARTITION_SIZE} - ${WOLFBOOT_SECTOR_SIZE} )))
    UPDATE_TRAILER_SECTOR=$(printf "0x%X" $(( ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} + ${WOLFBOOT_PARTITION_SIZE} - ${WOLFBOOT_SECTOR_SIZE} )))

    # SPIFI flash base for this target
    SPIFI_BASE="0x10000000"

    echo -e "${CYAN}Config: SIGN=${SIGN_VALUE} HASH=${HASH_VALUE}${NC}"
    echo -e "${CYAN}  BOOT=${WOLFBOOT_PARTITION_BOOT_ADDRESS} UPDATE=${WOLFBOOT_PARTITION_UPDATE_ADDRESS} SIZE=${WOLFBOOT_PARTITION_SIZE}${NC}"
}

# Change to wolfboot root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${WOLFBOOT_ROOT}"
echo -e "${YELLOW}Working directory: ${WOLFBOOT_ROOT}${NC}"

# Step 1: Configure and Build
if [ $SKIP_BUILD -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[1/3] Configuring for NXP LPC54S018M-EVK...${NC}"
    if [ ! -f "${CONFIG_FILE}" ]; then
        echo -e "${RED}Error: Config file not found: ${CONFIG_FILE}${NC}"
        exit 1
    fi
    # Only copy if source and destination are different
    if [ "${CONFIG_FILE}" != ".config" ]; then
        cp "${CONFIG_FILE}" .config
        echo "Copied ${CONFIG_FILE} to .config"
    else
        echo "Using existing .config"
    fi

    # Parse all configuration values from .config
    parse_config .config

    # Step 2: Build
    echo ""
    if [ $TEST_UPDATE -eq 1 ]; then
        echo -e "${GREEN}[2/3] Building factory.bin + v2 update image...${NC}"
    else
        echo -e "${GREEN}[2/3] Building factory.bin...${NC}"
    fi

    make clean
    make CROSS_COMPILE=${CROSS_COMPILE:-arm-none-eabi-}

    if [ ! -f factory.bin ]; then
        echo -e "${RED}Error: Build failed - factory.bin not found${NC}"
        exit 1
    fi
    echo -e "${GREEN}factory.bin built successfully${NC}"

    if [ $TEST_UPDATE -eq 1 ]; then
        echo ""
        echo -e "${CYAN}Signing test-app with version 2...${NC}"
        ./tools/keytools/sign ${SIGN_FLAG} ${HASH_FLAG} \
            test-app/image.bin wolfboot_signing_private_key.der 2
        if [ ! -f test-app/image_v2_signed.bin ]; then
            echo -e "${RED}Error: Failed to sign v2 image${NC}"
            exit 1
        fi
        echo -e "${GREEN}image_v2_signed.bin created${NC}"
    fi
else
    echo ""
    echo -e "${YELLOW}[1/3] Skipping configure (--skip-build)${NC}"
    echo -e "${YELLOW}[2/3] Skipping build (--skip-build)${NC}"

    # Still need to parse config for flash addresses
    if [ -f .config ]; then
        parse_config .config
    else
        parse_config "${CONFIG_FILE}"
    fi
fi

# Step 3: Flash
if [ $SKIP_FLASH -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[3/3] Flashing to LPC54S018M-EVK...${NC}"

    # Check pyocd is available
    if ! command -v pyocd &> /dev/null; then
        echo -e "${RED}Error: pyocd not found. Install with: pip install pyocd${NC}"
        echo -e "${YELLOW}Then install target pack: pyocd pack install ${PYOCD_TARGET}${NC}"
        exit 1
    fi

    # Verify images exist
    if [ ! -f factory.bin ]; then
        echo -e "${RED}Error: factory.bin not found. Run without --skip-build first.${NC}"
        exit 1
    fi
    if [ $TEST_UPDATE -eq 1 ] && [ ! -f test-app/image_v2_signed.bin ]; then
        echo -e "${RED}Error: test-app/image_v2_signed.bin not found.${NC}"
        exit 1
    fi

    # Erase partition trailer sectors for clean boot state
    echo -e "${CYAN}Erasing partition trailers for clean state...${NC}"
    pyocd erase -t ${PYOCD_TARGET} -s ${BOOT_TRAILER_SECTOR}+${WOLFBOOT_SECTOR_SIZE}
    pyocd erase -t ${PYOCD_TARGET} -s ${UPDATE_TRAILER_SECTOR}+${WOLFBOOT_SECTOR_SIZE}

    # Flash factory image (wolfBoot + v1 test app)
    echo -e "${CYAN}Flashing factory.bin -> ${SPIFI_BASE}${NC}"
    pyocd flash -t ${PYOCD_TARGET} factory.bin --base-address ${SPIFI_BASE}

    if [ $TEST_UPDATE -eq 1 ]; then
        echo -e "${CYAN}Flashing image_v2_signed.bin -> ${WOLFBOOT_PARTITION_UPDATE_ADDRESS}${NC}"
        pyocd flash -t ${PYOCD_TARGET} test-app/image_v2_signed.bin \
            --base-address ${WOLFBOOT_PARTITION_UPDATE_ADDRESS}
    fi

    echo -e "${GREEN}Flash complete!${NC}"
else
    echo ""
    echo -e "${YELLOW}[3/3] Skipping flash (--skip-flash)${NC}"
fi

echo ""
echo -e "${GREEN}=== Complete ===${NC}"
echo ""
if [ $TEST_UPDATE -eq 1 ]; then
    echo -e "${CYAN}Power cycle the board. Expected sequence:${NC}"
    echo "  1st boot: USR_LED1 (v1 running) + USR_LED3 (update triggered)"
    echo "  2nd boot: Wait ~60s for swap, then USR_LED2 (v2 running)"
else
    echo -e "${CYAN}Power cycle the board. Expected: USR_LED1 (v1 running)${NC}"
fi
