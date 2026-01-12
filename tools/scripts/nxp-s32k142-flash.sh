#!/bin/bash
#
# NXP S32K142 Flash and UART Monitor Script
#
# This script automates:
# 1. Configure for NXP S32K142
# 2. Build factory.srec (or update.srec for testing updates)
# 3. Start UART capture (before flash to catch boot messages)
# 4. Flash via USB mass storage
# 5. Capture UART output for specified duration
#

set -e

# Configuration
CONFIG_FILE="./config/examples/nxp-s32k142.config"
MOUNT_PATH="/media/davidgarske/S32K142EVB"
UART_DEV="/dev/ttyACM1"
UART_BAUD=115200
SREC_FILE="factory.srec"
UART_TIMEOUT=5  # Default 5 seconds capture

# Memory layout (from nxp-s32k142.config)
WOLFBOOT_PARTITION_BOOT_ADDRESS=0xC000
WOLFBOOT_PARTITION_UPDATE_ADDRESS=0x25000
WOLFBOOT_PARTITION_SIZE=0x19000

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Parse command line arguments
SKIP_BUILD=0
SKIP_FLASH=0
SKIP_UART=0
UART_ONLY=0
INTERACTIVE=0
BUILD_UPDATE=0
TEST_UPDATE=0

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --test-update   Build test-update.srec with v2 image in update partition"
    echo "                  (no trigger - use test-app 'trigger' command to start update)"
    echo "  --update        Build update.srec with v2 image + trigger magic"
    echo "                  (auto-starts update on boot - may cause issues)"
    echo "  --skip-build    Skip the build step (use existing .srec)"
    echo "  --skip-flash    Skip flashing (just monitor UART)"
    echo "  --skip-uart     Skip UART monitoring (just build and flash)"
    echo "  --uart-only     Only monitor UART (same as --skip-build --skip-flash)"
    echo "  --interactive   Keep UART open until Ctrl+C (default: auto-exit after timeout)"
    echo "  --timeout SECS  UART capture duration in seconds (default: $UART_TIMEOUT)"
    echo "  --mount PATH    Override mount path (default: $MOUNT_PATH)"
    echo "  --uart DEV      Override UART device (default: $UART_DEV)"
    echo "  --baud RATE     Override baud rate (default: $UART_BAUD)"
    echo "  -h, --help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                      # Build and flash factory.srec (v1 only)"
    echo "  $0 --test-update        # Build with v2 in update partition, use 'trigger' cmd"
    echo "  $0 --skip-uart          # Flash without UART monitoring"
    echo "  $0 --uart-only          # Just monitor UART"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --test-update)
            TEST_UPDATE=1
            SREC_FILE="test-update.srec"
            shift
            ;;
        --update)
            BUILD_UPDATE=1
            SREC_FILE="update.srec"
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
        --skip-uart)
            SKIP_UART=1
            shift
            ;;
        --uart-only)
            SKIP_BUILD=1
            SKIP_FLASH=1
            shift
            ;;
        --interactive)
            INTERACTIVE=1
            shift
            ;;
        --timeout)
            UART_TIMEOUT="$2"
            shift 2
            ;;
        --mount)
            MOUNT_PATH="$2"
            shift 2
            ;;
        --uart)
            UART_DEV="$2"
            shift 2
            ;;
        --baud)
            UART_BAUD="$2"
            shift 2
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

# Cleanup function to kill background UART capture
UART_PID=""
cleanup() {
    if [ -n "$UART_PID" ] && kill -0 "$UART_PID" 2>/dev/null; then
        kill "$UART_PID" 2>/dev/null || true
        wait "$UART_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo -e "${GREEN}=== NXP S32K142 Flash and Monitor Script ===${NC}"

# Change to wolfboot root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${WOLFBOOT_ROOT}"
echo -e "${YELLOW}Working directory: ${WOLFBOOT_ROOT}${NC}"

# Step 1: Configure
if [ $SKIP_BUILD -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[1/4] Configuring for NXP S32K142...${NC}"
    if [ ! -f "${CONFIG_FILE}" ]; then
        echo -e "${RED}Error: Config file not found: ${CONFIG_FILE}${NC}"
        exit 1
    fi
    cp "${CONFIG_FILE}" .config
    echo "Copied ${CONFIG_FILE} to .config"

    # Step 2: Build
    echo ""
    if [ $TEST_UPDATE -eq 1 ]; then
        echo -e "${GREEN}[2/4] Building test-update.srec (v1 boot + v2 update, no trigger)...${NC}"
        make clean
        make factory.srec

        # Build v2 signed image
        echo ""
        echo -e "${CYAN}Signing test-app with version 2...${NC}"
        cp test-app/image_v1_signed.bin test-app/image_v1_signed_backup.bin
        ./tools/keytools/sign --ecc256 --sha256 test-app/image.bin wolfboot_signing_private_key.der 2

        echo -e "${CYAN}Assembling test-update.bin...${NC}"
        echo "  wolfboot.bin          @ 0x0"
        echo "  image_v1_signed.bin   @ ${WOLFBOOT_PARTITION_BOOT_ADDRESS} (boot partition)"
        echo "  image_v2_signed.bin   @ ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} (update partition)"
        echo ""
        echo -e "${YELLOW}NOTE: No trigger magic - use test-app 'trigger' command to start update${NC}"

        ./tools/bin-assemble/bin-assemble \
            test-update.bin \
            0x0                             wolfboot.bin \
            ${WOLFBOOT_PARTITION_BOOT_ADDRESS}   test-app/image_v1_signed_backup.bin \
            ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} test-app/image_v2_signed.bin

        # Convert to srec
        echo -e "${CYAN}Converting to test-update.srec...${NC}"
        arm-none-eabi-objcopy -I binary -O srec --srec-forceS3 test-update.bin test-update.srec

        # Cleanup temp files
        mv test-app/image_v1_signed_backup.bin test-app/image_v1_signed.bin 2>/dev/null || true

        echo -e "${GREEN}Build successful: test-update.srec${NC}"
        echo ""
        echo -e "${CYAN}After boot, use these test-app commands:${NC}"
        echo "  status   - Show partition info (should show v1 boot, v2 update)"
        echo "  trigger  - Set update flag and reboot"
        echo "  reboot   - Reboot to start update"
    elif [ $BUILD_UPDATE -eq 1 ]; then
        echo -e "${GREEN}[2/4] Building update.srec (with v2 image + trigger)...${NC}"
        echo -e "${CYAN}NOTE: Auto-trigger starts update on first boot${NC}"
        make clean
        make factory.srec

        # Build v2 signed image
        # Save v1 since sign tool overwrites with default name pattern
        echo ""
        echo -e "${CYAN}Signing test-app with version 2...${NC}"
        cp test-app/image_v1_signed.bin test-app/image_v1_signed_backup.bin
        ./tools/keytools/sign --ecc256 --sha256 test-app/image.bin wolfboot_signing_private_key.der 2
        # Sign tool outputs to image_v2_signed.bin directly

        # Create trigger magic: 'p' (IMG_STATE_UPDATING = 0x70) + "BOOT"
        echo -e "${CYAN}Creating update trigger magic...${NC}"
        printf 'pBOOT' > trigger_magic.bin

        # Calculate trigger offset: update partition end - 5
        # Update end = WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE
        # 0x25000 + 0x19000 = 0x3E000, minus 5 = 0x3DFFB
        TRIGGER_OFFSET=$((WOLFBOOT_PARTITION_UPDATE_ADDRESS + WOLFBOOT_PARTITION_SIZE - 5))
        TRIGGER_OFFSET_HEX=$(printf "0x%X" $TRIGGER_OFFSET)

        echo -e "${CYAN}Assembling update.bin...${NC}"
        echo "  wolfboot.bin          @ 0x0"
        echo "  image_v1_signed.bin   @ ${WOLFBOOT_PARTITION_BOOT_ADDRESS} (boot partition)"
        echo "  image_v2_signed.bin   @ ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} (update partition)"
        echo "  trigger_magic.bin     @ ${TRIGGER_OFFSET_HEX} (update trailer)"

        ./tools/bin-assemble/bin-assemble \
            update.bin \
            0x0                             wolfboot.bin \
            ${WOLFBOOT_PARTITION_BOOT_ADDRESS}   test-app/image_v1_signed_backup.bin \
            ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} test-app/image_v2_signed.bin \
            ${TRIGGER_OFFSET_HEX}           trigger_magic.bin

        # Convert to srec
        echo -e "${CYAN}Converting to update.srec...${NC}"
        arm-none-eabi-objcopy -I binary -O srec --srec-forceS3 update.bin update.srec

        # Cleanup temp files
        rm -f trigger_magic.bin
        mv test-app/image_v1_signed_backup.bin test-app/image_v1_signed.bin 2>/dev/null || true

        echo -e "${GREEN}Build successful: update.srec${NC}"
    else
        echo -e "${GREEN}[2/4] Building factory.srec...${NC}"
        make clean
        make factory.srec
    fi

    if [ ! -f "${SREC_FILE}" ]; then
        echo -e "${RED}Error: Build failed - ${SREC_FILE} not found${NC}"
        exit 1
    fi
    echo -e "${GREEN}Build successful: ${SREC_FILE}${NC}"
else
    echo ""
    echo -e "${YELLOW}[1/4] Skipping configure (--skip-build)${NC}"
    echo -e "${YELLOW}[2/4] Skipping build (--skip-build)${NC}"
fi

# Step 3: Start UART capture BEFORE flashing
if [ $SKIP_UART -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[3/4] Starting UART capture on ${UART_DEV} at ${UART_BAUD} baud...${NC}"

    # Check if UART device exists
    if [ ! -c "${UART_DEV}" ]; then
        echo -e "${RED}Error: UART device ${UART_DEV} not found${NC}"
        echo "Available ttyACM devices:"
        ls -la /dev/ttyACM* 2>/dev/null || echo "  (none found)"
        exit 1
    fi

    # Configure UART
    stty -F "${UART_DEV}" "${UART_BAUD}" raw -echo -hupcl

    # Start background UART capture that outputs to terminal
    cat "${UART_DEV}" &
    UART_PID=$!
    echo -e "${CYAN}UART capture started (PID: ${UART_PID})${NC}"
else
    echo ""
    echo -e "${YELLOW}[3/4] Skipping UART capture (--skip-uart)${NC}"
fi

# Step 4: Flash
if [ $SKIP_FLASH -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[4/4] Flashing to S32K142EVB...${NC}"

    if [ ! -f "${SREC_FILE}" ]; then
        echo -e "${RED}Error: ${SREC_FILE} not found. Run without --skip-build first.${NC}"
        exit 1
    fi

    # Wait for mount point if not available
    WAIT_COUNT=0
    while [ ! -d "${MOUNT_PATH}" ]; do
        if [ $WAIT_COUNT -eq 0 ]; then
            echo -e "${YELLOW}Waiting for ${MOUNT_PATH} to be mounted...${NC}"
            echo "(Connect the S32K142EVB board via USB)"
        fi
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))
        if [ $WAIT_COUNT -gt 30 ]; then
            echo -e "${RED}Error: Timeout waiting for ${MOUNT_PATH}${NC}"
            exit 1
        fi
    done

    echo "Copying ${SREC_FILE} to ${MOUNT_PATH}..."
    cp "${SREC_FILE}" "${MOUNT_PATH}/"
    sync
    echo -e "${GREEN}Flash complete! Waiting for boot output...${NC}"
else
    echo ""
    echo -e "${YELLOW}[4/4] Skipping flash (--skip-flash)${NC}"
fi

# UART monitoring
if [ $SKIP_UART -eq 0 ]; then
    echo ""
    if [ $INTERACTIVE -eq 1 ]; then
        echo -e "${YELLOW}=== UART Output (Press Ctrl+C to exit) ===${NC}"
        wait "$UART_PID" 2>/dev/null || true
    else
        echo -e "${YELLOW}=== UART Output (capturing for ${UART_TIMEOUT} seconds) ===${NC}"
        sleep "$UART_TIMEOUT"

        # Kill the background cat process
        cleanup
        UART_PID=""
    fi
fi

echo ""
echo -e "${GREEN}=== Complete ===${NC}"
