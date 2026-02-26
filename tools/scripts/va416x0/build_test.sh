#!/bin/bash
set -e

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <clean|update> [version_number]"
    echo "  clean:  Build and flash a clean factory image (version defaults to 1)"
    echo "  update: Build and flash an update image (version defaults to 2)"
    echo "  version_number: Version number for the signed image (optional, defaults to 1 for clean, 2 for update)"
    exit 1
fi

MODE=$1
[ "$MODE" != "clean" ] && [ "$MODE" != "update" ] && { echo "Error: Mode must be 'clean' or 'update'"; exit 1; }

# Set version: default to 1 for clean, default to 2 for update
VERSION=$([ "$MODE" = "clean" ] && echo "${2:-1}" || echo "${2:-2}")

# Find JLinkExe (in PATH on Linux, /Applications/SEGGER on macOS)
if command -v JLinkExe >/dev/null 2>&1; then
    JLINK="JLinkExe"
else
    # Check for versioned JLink directory on macOS (e.g., JLink_V812g)
    JLINK_PATH=$(find /Applications/SEGGER -name "JLinkExe" 2>/dev/null | head -n1)
    if [ -n "$JLINK_PATH" ] && [ -x "$JLINK_PATH" ]; then
        JLINK="$JLINK_PATH"
    else
        echo "Error: JLinkExe not found. Please install SEGGER J-Link software."
        exit 1
    fi
fi

# Function to get value from .config file
get_config_value() {
    grep "^${1}" .config | sed -E "s/^${1}[?]?=//" | head -n1
}

# Extract values from .config
BOOT_ADDRESS=$(get_config_value "WOLFBOOT_PARTITION_BOOT_ADDRESS")
UPDATE_ADDRESS=$(get_config_value "WOLFBOOT_PARTITION_UPDATE_ADDRESS")
PARTITION_SIZE=$(get_config_value "WOLFBOOT_PARTITION_SIZE")
SECTOR_SIZE=$(get_config_value "WOLFBOOT_SECTOR_SIZE")
IMAGE_HEADER_SIZE=$(get_config_value "IMAGE_HEADER_SIZE")
SIGN=$(get_config_value "SIGN")
HASH=$(get_config_value "HASH")
SIGN_ARG="--$(echo "${SIGN}" | tr '[:upper:]' '[:lower:]')"
HASH_ARG="--$(echo "${HASH}" | tr '[:upper:]' '[:lower:]')"

# Common build steps
make clean && make wolfboot.bin && make test-app/image.bin

# Function to sign image
sign_image() {
    IMAGE_HEADER_SIZE=${IMAGE_HEADER_SIZE} \
    WOLFBOOT_PARTITION_SIZE=${PARTITION_SIZE} \
    WOLFBOOT_SECTOR_SIZE=${SECTOR_SIZE} \
    ./tools/keytools/sign ${SIGN_ARG} ${HASH_ARG} test-app/image.bin wolfboot_signing_private_key.der "$1"
}

# Function to print summary
print_summary() {
    echo "Computed addresses:"
    echo "  BOOT_ADDRESS:   ${BOOT_ADDRESS}"
    echo "  UPDATE_ADDRESS: ${UPDATE_ADDRESS}"
    [ -n "$1" ] && echo "  TRIGGER_ADDRESS: $1"
    echo "Sign/Hash configuration:"
    echo "  SIGN:           ${SIGN} (${SIGN_ARG})"
    echo "  HASH:           ${HASH} (${HASH_ARG})"
    [ -n "$2" ] && echo "  PREV_VERSION:    $2"
    echo "  $([ -n "$2" ] && echo "NEW_VERSION" || echo "VERSION"):        ${VERSION}"
}

if [ "$MODE" = "clean" ]; then
    sign_image ${VERSION}
    dd if=/dev/zero of=blank_update.bin bs=1K count=108
    ./tools/bin-assemble/bin-assemble factory.bin 0x0 wolfboot.bin \
        ${BOOT_ADDRESS} test-app/image_v${VERSION}_signed.bin \
        ${UPDATE_ADDRESS} blank_update.bin
    ${JLINK} -CommanderScript tools/scripts/va416x0/flash_va416xx.jlink
    print_summary
else
    TRIGGER_ADDRESS=$(printf "0x%X" $((${UPDATE_ADDRESS} + ${PARTITION_SIZE} - 5)))
    PREV_VERSION=$((${VERSION} - 1))
    sign_image ${PREV_VERSION} && sign_image ${VERSION}
    echo -n "pBOOT" > trigger_magic.bin
    ./tools/bin-assemble/bin-assemble update.bin 0x0 wolfboot.bin \
        ${BOOT_ADDRESS} test-app/image_v${PREV_VERSION}_signed.bin \
        ${UPDATE_ADDRESS} test-app/image_v${VERSION}_signed.bin \
        ${TRIGGER_ADDRESS} trigger_magic.bin
    ${JLINK} -CommanderScript tools/scripts/va416x0/flash_va416xx_update.jlink
    print_summary "${TRIGGER_ADDRESS}" "${PREV_VERSION}"
fi
