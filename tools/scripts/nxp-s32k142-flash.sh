#!/bin/bash
#
# NXP S32K142 Flash and UART Monitor Script
#
# This script automates:
# 1. Configure for NXP S32K142
# 2. Build factory.srec
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

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --skip-build    Skip the build step (use existing factory.srec)"
    echo "  --skip-flash    Skip flashing (just monitor UART)"
    echo "  --skip-uart     Skip UART monitoring (just build and flash)"
    echo "  --uart-only     Only monitor UART (same as --skip-build --skip-flash)"
    echo "  --interactive   Keep UART open until Ctrl+C (default: auto-exit after timeout)"
    echo "  --timeout SECS  UART capture duration in seconds (default: $UART_TIMEOUT)"
    echo "  --mount PATH    Override mount path (default: $MOUNT_PATH)"
    echo "  --uart DEV      Override UART device (default: $UART_DEV)"
    echo "  --baud RATE     Override baud rate (default: $UART_BAUD)"
    echo "  -h, --help      Show this help message"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
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
    echo -e "${GREEN}[2/4] Building factory.srec...${NC}"
    make clean
    make factory.srec

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
