#!/bin/bash
# Provision SD card for Versal VMK180 wolfBoot
#
# Usage: sudo ./versal_sdcard_provision.sh /dev/sdX
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WOLFBOOT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC} $*"; }
log_ok() { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# Check arguments
if [ -z "$1" ]; then
    echo "Usage: sudo $0 /dev/sdX"
    echo ""
    echo "Provisions an SD card for Versal VMK180 wolfBoot testing."
    echo ""
    echo "Prerequisites: Run './tools/scripts/versal_test.sh --sdcard' first"
    echo "               to generate sdcard.img and BOOT.BIN"
    exit 1
fi

DEVICE="$1"

# Safety checks
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root (sudo)"
    exit 1
fi

if [ ! -b "$DEVICE" ]; then
    log_error "Device not found: $DEVICE"
    exit 1
fi

if [[ "$DEVICE" == *"sda"* ]] || [[ "$DEVICE" == *"nvme0n1"* ]]; then
    log_error "Refusing to write to system drive: $DEVICE"
    exit 1
fi

# Check required files exist
SDCARD_IMG="${WOLFBOOT_ROOT}/sdcard.img"
BOOT_BIN="${WOLFBOOT_ROOT}/BOOT.BIN"

if [ ! -f "$SDCARD_IMG" ]; then
    log_error "SD card image not found: $SDCARD_IMG"
    log_info "Run './tools/scripts/versal_test.sh --sdcard' first"
    exit 1
fi

if [ ! -f "$BOOT_BIN" ]; then
    log_error "BOOT.BIN not found: $BOOT_BIN"
    log_info "Run './tools/scripts/versal_test.sh --sdcard' first"
    exit 1
fi

# Show what we're about to do
echo ""
log_warn "This will ERASE ALL DATA on $DEVICE"
echo ""
log_info "Source files:"
log_info "  SD card image: $SDCARD_IMG ($(stat -c%s "$SDCARD_IMG") bytes)"
log_info "  BOOT.BIN:      $BOOT_BIN ($(stat -c%s "$BOOT_BIN") bytes)"
echo ""
log_info "Target device: $DEVICE"
lsblk "$DEVICE" 2>/dev/null || true
echo ""

read -p "Type 'YES' to proceed: " confirm
if [ "$confirm" != "YES" ]; then
    log_info "Aborted."
    exit 0
fi

# Unmount any mounted partitions
log_info "Unmounting any mounted partitions..."
umount ${DEVICE}* 2>/dev/null || true
sleep 1

# Write the SD card image
log_info "Writing SD card image to $DEVICE..."
dd if="$SDCARD_IMG" of="$DEVICE" bs=4M status=progress conv=fsync
sync
log_ok "SD card image written"

# Re-read partition table
log_info "Re-reading partition table..."
partprobe "$DEVICE" 2>/dev/null || true
sleep 2

# Show partition layout
echo ""
log_info "=== Partition Layout ==="
fdisk -l "$DEVICE"
echo ""

# Show partition details
log_info "=== Partition Details ==="
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE"
echo ""

# Format partition 1 as FAT32
PART1="${DEVICE}1"
if [ ! -b "$PART1" ]; then
    # Handle nvme style naming (p1 instead of 1)
    PART1="${DEVICE}p1"
fi

if [ -b "$PART1" ]; then
    log_info "Formatting $PART1 as FAT32..."
    mkfs.vfat -F 32 -n BOOT "$PART1"
    log_ok "Partition 1 formatted as FAT32"

    # Mount and copy BOOT.BIN
    MOUNT_POINT=$(mktemp -d)
    log_info "Mounting $PART1 to $MOUNT_POINT..."
    mount "$PART1" "$MOUNT_POINT"

    log_info "Copying BOOT.BIN..."
    cp "$BOOT_BIN" "$MOUNT_POINT/"
    sync

    # Show boot partition contents
    log_info "Boot partition contents:"
    ls -la "$MOUNT_POINT/"
    echo ""

    umount "$MOUNT_POINT"
    rmdir "$MOUNT_POINT"
    log_ok "BOOT.BIN copied to boot partition"
else
    log_error "Could not find partition 1: tried $PART1"
fi

# Final summary
echo ""
log_info "=== Final Partition Layout ==="
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL "$DEVICE"
echo ""

log_info "=== Partition Contents ==="
log_info "  Partition 1 (boot):   BOOT.BIN (PLM + PSM + BL31 + wolfBoot)"
log_info "  Partition 2 (OFP_A):  test-app/image_v1_signed.bin (primary)"
log_info "  Partition 3 (OFP_B):  test-app/image_v2_signed.bin (update)"
log_info "  Partition 4 (rootfs): Empty (for Linux rootfs)"
echo ""

log_ok "SD card provisioning complete!"
log_info "Insert SD card into VMK180 and set boot mode to SD"
