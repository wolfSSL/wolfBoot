#!/bin/bash
#
# NXP S32K142 Flash Script
#
# This script automates:
# 1. Configure for NXP S32K142
# 2. Build factory.srec (or test-update.srec / test-selfupdate.srec)
# 3. Flash via USB mass storage
#

set -e

# Configuration (can be overridden via environment variables or command line)
CONFIG_FILE="${CONFIG_FILE:-.config}"
SREC_FILE="${SREC_FILE:-factory.srec}"

# Function to detect USB mass storage mount point portably
detect_mount_path() {
    # Common mount locations for USB mass storage devices
    local mount_dirs=(
        "/media"/*/S32K142EVB
        "/media"/*/S32K142*
        "/mnt"/*/S32K142EVB
        "/mnt"/*/S32K142*
        "/run/media"/*/S32K142EVB
        "/run/media"/*/S32K142*
    )

    # Try to find existing mount
    for path in "${mount_dirs[@]}"; do
        if [ -d "$path" ] && [ -w "$path" ]; then
            echo "$path"
            return 0
        fi
    done

    # If not found, return empty (will be set via --mount or wait for mount)
    return 1
}

# Set default mount path (can be overridden via MOUNT_PATH env var or --mount flag)
if [ -z "${MOUNT_PATH}" ]; then
    MOUNT_PATH=$(detect_mount_path 2>/dev/null || echo "")
fi

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
TEST_SELFUPDATE=0
TRIGGER_MAGIC=0

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --test-update      Build test-update.srec with v2 image in update partition (use 'trigger' command to start update)"
    echo "  --test-selfupdate  Build test-selfupdate.srec with bootloader v1 + v2 bootloader update (use 'trigger' command to start update)"
    echo "  --trigger-magic    Include trigger magic bytes to auto-start update on boot (use with --test-update)"
    echo "  --skip-build       Skip the build step (use existing .srec)"
    echo "  --skip-flash       Skip flashing (just build)"
    echo "  -h, --help         Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  MOUNT_PATH         Override mount path (default: auto-detect)"
    echo ""
    echo "Examples:"
    echo "  $0                              # Build and flash factory.srec (v1 only)"
    echo "  $0 --test-update                # Build with v2 in update partition, use 'trigger' cmd"
    echo "  $0 --test-update --trigger-magic  # Build with v2 and auto-trigger update on boot"
    echo "  $0 --test-selfupdate            # Build with bootloader v2 update, tests self-update"
    echo "  $0 --skip-flash                 # Build without flashing"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --test-update)
            TEST_UPDATE=1
            SREC_FILE="test-update.srec"
            shift
            ;;
        --test-selfupdate)
            TEST_SELFUPDATE=1
            SREC_FILE="test-selfupdate.srec"
            shift
            ;;
        --trigger-magic)
            TRIGGER_MAGIC=1
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
        --mount)
            MOUNT_PATH="$2"
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

echo -e "${GREEN}=== NXP S32K142 Flash Script ===${NC}"

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

    # Validate required fields
    local missing=""
    [ -z "$SIGN_VALUE" ] && missing="${missing}SIGN "
    [ -z "$HASH_VALUE" ] && missing="${missing}HASH "
    [ -z "$WOLFBOOT_PARTITION_BOOT_ADDRESS" ] && missing="${missing}WOLFBOOT_PARTITION_BOOT_ADDRESS "
    [ -z "$WOLFBOOT_PARTITION_UPDATE_ADDRESS" ] && missing="${missing}WOLFBOOT_PARTITION_UPDATE_ADDRESS "
    [ -z "$WOLFBOOT_PARTITION_SIZE" ] && missing="${missing}WOLFBOOT_PARTITION_SIZE "

    if [ -n "$missing" ]; then
        echo -e "${RED}Error: Missing required config values: ${missing}${NC}"
        exit 1
    fi

    # Convert SIGN/HASH to lowercase flag format
    SIGN_FLAG="--$(echo "$SIGN_VALUE" | tr '[:upper:]' '[:lower:]')"
    HASH_FLAG="--$(echo "$HASH_VALUE" | tr '[:upper:]' '[:lower:]')"

    # Ensure partition addresses have 0x prefix for bash arithmetic
    for var in WOLFBOOT_PARTITION_BOOT_ADDRESS WOLFBOOT_PARTITION_UPDATE_ADDRESS WOLFBOOT_PARTITION_SIZE; do
        eval "val=\$$var"
        if [[ ! "$val" =~ ^0x ]]; then
            eval "$var=\"0x\${val}\""
        fi
    done

    echo -e "${CYAN}Config: SIGN=${SIGN_VALUE} HASH=${HASH_VALUE} BOOT=${WOLFBOOT_PARTITION_BOOT_ADDRESS} UPDATE=${WOLFBOOT_PARTITION_UPDATE_ADDRESS} SIZE=${WOLFBOOT_PARTITION_SIZE}${NC}"
}

# Change to wolfboot root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${WOLFBOOT_ROOT}"
echo -e "${YELLOW}Working directory: ${WOLFBOOT_ROOT}${NC}"

# Step 1: Configure
if [ $SKIP_BUILD -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[1/3] Configuring for NXP S32K142...${NC}"
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
    if [ $TEST_SELFUPDATE -eq 1 ]; then
        echo -e "${GREEN}[2/3] Building test-selfupdate.srec (bootloader v1 + v2 bootloader update)...${NC}"
        echo -e "${CYAN}NOTE: This tests bootloader self-update functionality${NC}"

        # Step 2a: Build bootloader v1
        echo ""
        echo -e "${CYAN}Building bootloader v1...${NC}"
        make clean
        make wolfboot.bin RAM_CODE=1 WOLFBOOT_VERSION=1
        if [ ! -f "wolfboot.bin" ]; then
            echo -e "${RED}Error: Failed to build wolfboot.bin v1${NC}"
            exit 1
        fi
        cp wolfboot.bin wolfboot_v1.bin
        echo -e "${GREEN}Bootloader v1 built successfully${NC}"

        # Build factory.srec to get the v1 application image
        echo ""
        echo -e "${CYAN}Building factory image with v1 application...${NC}"
        make factory.srec
        if [ ! -f "factory.srec" ]; then
            echo -e "${RED}Error: Failed to build factory.srec${NC}"
            exit 1
        fi
        if [ ! -f "test-app/image_v1_signed.bin" ]; then
            echo -e "${RED}Error: test-app/image_v1_signed.bin not found${NC}"
            exit 1
        fi
        # Preserve v1 application image before clean (copy outside test-app to survive clean)
        cp test-app/image_v1_signed.bin image_v1_signed_backup.bin
        echo -e "${GREEN}Preserved v1 application image${NC}"

        # Step 2b: Build bootloader v2
        echo ""
        echo -e "${CYAN}Building bootloader v2...${NC}"
        make clean
        make wolfboot.bin RAM_CODE=1 WOLFBOOT_VERSION=2
        if [ ! -f "wolfboot.bin" ]; then
            echo -e "${RED}Error: Failed to build wolfboot.bin v2${NC}"
            exit 1
        fi

        # Step 2c: Sign bootloader v2 with --wolfboot-update flag
        echo ""
        echo -e "${CYAN}Signing bootloader v2 with --wolfboot-update flag...${NC}"
        # Ensure sign tool is built
        if [ ! -f "tools/keytools/sign" ]; then
            echo -e "${YELLOW}Building sign tool...${NC}"
            make -C tools/keytools
        fi
        # Check if key exists
        if [ ! -f "wolfboot_signing_private_key.der" ]; then
            echo -e "${RED}Error: wolfboot_signing_private_key.der not found${NC}"
            echo "Please generate keys first with: make keys"
            exit 1
        fi

        # Sign with --wolfboot-update flag (version 2) using config values
        ./tools/keytools/sign ${SIGN_FLAG} ${HASH_FLAG} --wolfboot-update wolfboot.bin wolfboot_signing_private_key.der 2
        if [ ! -f "wolfboot_v2_signed.bin" ]; then
            echo -e "${RED}Error: Failed to sign bootloader v2${NC}"
            exit 1
        fi
        echo -e "${GREEN}Bootloader v2 signed successfully${NC}"

        # Step 2e: Assemble test-selfupdate.bin
        echo ""
        echo -e "${CYAN}Assembling test-selfupdate.bin...${NC}"
        echo "  wolfboot_v1.bin        @ 0x0 (bootloader v1)"
        echo "  image_v1_signed.bin   @ ${WOLFBOOT_PARTITION_BOOT_ADDRESS} (boot partition)"
        echo "  wolfboot_v2_signed.bin @ ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} (update partition)"

        # Ensure bin-assemble tool is built
        if [ ! -f "tools/bin-assemble/bin-assemble" ]; then
            echo -e "${YELLOW}Building bin-assemble tool...${NC}"
            make -C tools/bin-assemble
        fi

        ./tools/bin-assemble/bin-assemble \
            test-selfupdate.bin \
            0x0                             wolfboot_v1.bin \
            ${WOLFBOOT_PARTITION_BOOT_ADDRESS}   image_v1_signed_backup.bin \
            ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} wolfboot_v2_signed.bin

        if [ ! -f "test-selfupdate.bin" ]; then
            echo -e "${RED}Error: Failed to assemble test-selfupdate.bin${NC}"
            exit 1
        fi

        # Step 2f: Convert to SREC
        echo ""
        echo -e "${CYAN}Converting to test-selfupdate.srec...${NC}"
        arm-none-eabi-objcopy -I binary -O srec --srec-forceS3 test-selfupdate.bin test-selfupdate.srec

        # Cleanup temp files
        rm -f trigger_magic.bin
        rm -f wolfboot_v1.bin
        rm -f image_v1_signed_backup.bin

        echo -e "${GREEN}Build successful: test-selfupdate.srec${NC}"

    elif [ $TEST_UPDATE -eq 1 ]; then
        if [ $TRIGGER_MAGIC -eq 1 ]; then
            echo -e "${GREEN}[2/3] Building test-update.srec (v1 boot + v2 update + trigger magic)...${NC}"
        else
            echo -e "${GREEN}[2/3] Building test-update.srec (v1 boot + v2 update, no trigger)...${NC}"
        fi
        make clean
        make factory.srec

        # Build v2 signed image
        echo ""
        echo -e "${CYAN}Signing test-app with version 2...${NC}"
        cp test-app/image_v1_signed.bin test-app/image_v1_signed_backup.bin
        ./tools/keytools/sign ${SIGN_FLAG} ${HASH_FLAG} test-app/image.bin wolfboot_signing_private_key.der 2

        echo -e "${CYAN}Assembling test-update.bin...${NC}"
        echo "  wolfboot.bin          @ 0x0"
        echo "  image_v1_signed.bin   @ ${WOLFBOOT_PARTITION_BOOT_ADDRESS} (boot partition)"
        echo "  image_v2_signed.bin   @ ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} (update partition)"

        if [ $TRIGGER_MAGIC -eq 1 ]; then
            # Calculate trigger magic address: end of update partition - 5 bytes
            # Update partition end = UPDATE_ADDRESS + PARTITION_SIZE
            # Trigger magic "pBOOT" (5 bytes) goes at end - 5
            TRIGGER_ADDRESS=$(printf "0x%X" $(( ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} + ${WOLFBOOT_PARTITION_SIZE} - 5 )))
            echo "  trigger_magic.bin     @ ${TRIGGER_ADDRESS} (auto-trigger update)"
            echo ""
            echo -e "${CYAN}NOTE: Update will start automatically on first boot${NC}"

            # Create trigger magic file
            echo -n "pBOOT" > trigger_magic.bin

            ./tools/bin-assemble/bin-assemble \
                test-update.bin \
                0x0                                  wolfboot.bin \
                ${WOLFBOOT_PARTITION_BOOT_ADDRESS}   test-app/image_v1_signed_backup.bin \
                ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} test-app/image_v2_signed.bin \
                ${TRIGGER_ADDRESS}                   trigger_magic.bin

            # Cleanup trigger magic file
            rm -f trigger_magic.bin
        else
            echo ""
            echo -e "${YELLOW}NOTE: No trigger magic - use test-app 'trigger' command to start update${NC}"

            ./tools/bin-assemble/bin-assemble \
                test-update.bin \
                0x0                             wolfboot.bin \
                ${WOLFBOOT_PARTITION_BOOT_ADDRESS}   test-app/image_v1_signed_backup.bin \
                ${WOLFBOOT_PARTITION_UPDATE_ADDRESS} test-app/image_v2_signed.bin
        fi

        # Convert to srec
        echo -e "${CYAN}Converting to test-update.srec...${NC}"
        arm-none-eabi-objcopy -I binary -O srec --srec-forceS3 test-update.bin test-update.srec

        # Cleanup temp files
        mv test-app/image_v1_signed_backup.bin test-app/image_v1_signed.bin 2>/dev/null || true

        echo -e "${GREEN}Build successful: test-update.srec${NC}"
        echo ""
        if [ $TRIGGER_MAGIC -eq 1 ]; then
            echo -e "${CYAN}Update will start automatically on boot.${NC}"
        else
            echo -e "${CYAN}After boot, use these test-app commands:${NC}"
            echo "  status   - Show partition info (should show v1 boot, v2 update)"
            echo "  trigger  - Set update flag and reboot"
            echo "  reboot   - Reboot to start update"
        fi
    else
        echo -e "${GREEN}[2/3] Building factory.srec...${NC}"
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
    echo -e "${YELLOW}[1/3] Skipping configure (--skip-build)${NC}"
    echo -e "${YELLOW}[2/3] Skipping build (--skip-build)${NC}"
fi

# Step 3: Flash
if [ $SKIP_FLASH -eq 0 ]; then
    echo ""
    echo -e "${GREEN}[3/3] Flashing to S32K142EVB...${NC}"

    if [ ! -f "${SREC_FILE}" ]; then
        echo -e "${RED}Error: ${SREC_FILE} not found. Run without --skip-build first.${NC}"
        exit 1
    fi

    # Auto-detect mount path if not set
    if [ -z "${MOUNT_PATH}" ]; then
        echo -e "${CYAN}Auto-detecting mount path...${NC}"
        MOUNT_PATH=$(detect_mount_path 2>/dev/null || echo "")
    fi

    # Wait for mount point if not available
    WAIT_COUNT=0
    while [ -z "${MOUNT_PATH}" ] || [ ! -d "${MOUNT_PATH}" ]; do
        if [ $WAIT_COUNT -eq 0 ]; then
            if [ -z "${MOUNT_PATH}" ]; then
                echo -e "${YELLOW}Waiting for S32K142EVB to be mounted...${NC}"
            else
                echo -e "${YELLOW}Waiting for ${MOUNT_PATH} to be mounted...${NC}"
            fi
            echo "(Connect the S32K142EVB board via USB)"
            echo "  Or specify mount path with: --mount PATH"
            echo "  Or set environment variable: MOUNT_PATH=/path/to/mount"
        fi
        sleep 1
        WAIT_COUNT=$((WAIT_COUNT + 1))

        # Try to detect mount path again
        if [ -z "${MOUNT_PATH}" ]; then
            MOUNT_PATH=$(detect_mount_path 2>/dev/null || echo "")
        fi

        if [ $WAIT_COUNT -gt 30 ]; then
            echo -e "${RED}Error: Timeout waiting for mount point${NC}"
            if [ -z "${MOUNT_PATH}" ]; then
                echo "  Please specify mount path with: --mount PATH"
                echo "  Or set environment variable: MOUNT_PATH=/path/to/mount"
            else
                echo "  Expected mount at: ${MOUNT_PATH}"
            fi
            exit 1
        fi
    done

    echo -e "${CYAN}Using mount path: ${MOUNT_PATH}${NC}"

    echo "Copying ${SREC_FILE} to ${MOUNT_PATH}..."
    cp "${SREC_FILE}" "${MOUNT_PATH}/"
    sync
    echo -e "${GREEN}Flash complete!${NC}"
else
    echo ""
    echo -e "${YELLOW}[3/3] Skipping flash (--skip-flash)${NC}"
fi

echo ""
echo -e "${GREEN}=== Complete ===${NC}"
