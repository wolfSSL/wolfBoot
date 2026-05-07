#!/bin/bash
# Lay out a ZC702 wolfBoot SD card.
#
# Pure MBR layout (no GPT). The Zynq-7000 BootROM (UG821 ch.6.3) requires
# an MBR with the first partition as FAT32, type 0x0C (FAT32-LBA), with the
# Active flag (0x80) set, and BOOT.BIN as a regular file in that FAT32
# root. wolfBoot's disk.c reads MBR partitions when no protective GPT entry
# is found (src/disk.c:disk_open_mbr).
#
# Layout:
#   MBR p1   64 MB  FAT32-LBA (0x0C) Active   - holds BOOT.BIN for BootROM
#                                               (>= 33 MB so mkfs.vfat creates a
#                                               standard-cluster FAT32 the
#                                               BootROM accepts)
#   MBR p2   16 MB  Linux raw (0x83)          - signed boot image (BOOT_PART_A=1)
#   MBR p3   16 MB  Linux raw (0x83)          - signed update image (BOOT_PART_B=2)
#
# wolfBoot indexes MBR partitions starting at 0, so partition p1=idx0,
# p2=idx1, p3=idx2 - matching BOOT_PART_A=1 and BOOT_PART_B=2 in the config.
#
# Usage:
#   sudo ./tools/scripts/zc702/prepare_sdcard.sh /dev/sdX [signed_image]

set -e
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

DEV="$1"
SIGNED="${2:-test-app/image_v1_signed.bin}"
BOOTBIN="${BOOT_BIN:-./BOOT.BIN}"

[ -n "$DEV" ] || { echo -e "${RED}usage:${NC} sudo $0 <device> [signed_image]" >&2; exit 1; }
[ "$EUID" = 0 ] || { echo -e "${RED}must run as root${NC}" >&2; exit 1; }

case "$DEV" in
    /dev/sda|/dev/nvme*)
        echo -e "${RED}refusing $DEV (looks like a system disk)${NC}" >&2
        exit 1 ;;
    /dev/mmcblk0|/dev/mmcblk0*)
        # On many embedded Linux hosts mmcblk0 IS the OS disk. Refuse by
        # default; allow override only if the user explicitly opts in.
        if [ "${ALLOW_MMCBLK0:-0}" != "1" ]; then
            echo -e "${RED}refusing $DEV (mmcblk0 is the primary OS disk on" >&2
            echo -e "many systems). Re-run with ALLOW_MMCBLK0=1 if this is" >&2
            echo -e "really the SD card you want to wipe.${NC}" >&2
            exit 1
        fi ;;
    /dev/sd[b-z]|/dev/mmcblk[1-9]) ;;
    *) echo -e "${RED}unsupported device: $DEV${NC}" >&2; exit 1 ;;
esac

[ -b "$DEV" ] || { echo -e "${RED}$DEV not a block device${NC}" >&2; exit 1; }

# Belt-and-suspenders: refuse non-removable devices unless caller opts in.
# Some USB SD readers expose RM=0; in that case the user can set
# ALLOW_NON_REMOVABLE=1 after eyeballing lsblk.
if [ "${ALLOW_NON_REMOVABLE:-0}" != "1" ]; then
    rm_flag=$(lsblk -ndo RM "$DEV" 2>/dev/null || echo "")
    if [ -n "$rm_flag" ] && [ "$rm_flag" != "1" ]; then
        echo -e "${RED}refusing $DEV (RM=$rm_flag - not flagged as removable)." >&2
        echo -e "Re-run with ALLOW_NON_REMOVABLE=1 if this really is the" >&2
        echo -e "SD card you want to wipe.${NC}" >&2
        exit 1
    fi
fi
mount | grep -q "^${DEV}" && { echo -e "${RED}unmount $DEV partitions first${NC}" >&2; mount | grep "^${DEV}" >&2; exit 1; }
[ -f "$BOOTBIN" ] || { echo -e "${RED}BOOT.BIN not found at $BOOTBIN${NC}" >&2; exit 1; }
[ -f "$SIGNED" ]  || { echo -e "${RED}signed image not found at $SIGNED${NC}" >&2; exit 1; }

case "$DEV" in
    /dev/mmcblk*) P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3" ;;
    *)            P1="${DEV}1";  P2="${DEV}2";  P3="${DEV}3" ;;
esac

echo -e "${YELLOW}Target:${NC}"
lsblk -o NAME,SIZE,MODEL,VENDOR,TRAN "$DEV" 2>/dev/null | head -5
echo -e "${YELLOW}BOOT.BIN:${NC} $BOOTBIN ($(stat -c %s "$BOOTBIN") bytes)"
echo -e "${YELLOW}signed:${NC}   $SIGNED ($(stat -c %s "$SIGNED") bytes)"
echo
read -p "Type 'yes' to wipe $DEV: " CONFIRM
[ "$CONFIRM" = yes ] || { echo "Aborted."; exit 1; }

echo -e "${GREEN}1.${NC} Wiping head + tail to remove any existing GPT/MBR..."
wipefs --all --force "$DEV" >/dev/null 2>&1 || true
dd if=/dev/zero of="$DEV" bs=1M count=8 conv=fsync status=none
# Tail wipe (kills stale backup-GPT) - only if the device is large enough
# that seek = (size - 2048) is positive. Refuse to seek into a tiny or
# unreadable device.
DEV_SECTORS=$(blockdev --getsz "$DEV" 2>/dev/null || echo 0)
if [ "$DEV_SECTORS" -gt 4096 ]; then
    dd if=/dev/zero of="$DEV" bs=512 \
       seek=$((DEV_SECTORS - 2048)) count=2048 \
       conv=fsync status=none 2>/dev/null || true
else
    echo -e "${YELLOW}  (skipping tail wipe: device only $DEV_SECTORS sectors)${NC}"
fi
sync

echo -e "${GREEN}2.${NC} Writing pure MBR (parted msdos label) with 3 primary partitions..."
parted "$DEV" --script -- \
    mklabel msdos \
    mkpart primary fat32 1MiB    65MiB  \
    mkpart primary       65MiB   81MiB  \
    mkpart primary       81MiB   97MiB  \
    set 1 boot on
sync; partprobe "$DEV"; sleep 1

echo -e "${GREEN}3.${NC} Patching MBR types: p1=0x0C (FAT32-LBA), p2/p3=0x83 (Linux)..."
# parted leaves p1 as 0x0C already when fat32 is requested; force in case.
# Each MBR partition entry is 16 bytes starting at 0x1BE.
#   p1 entry: offset 0x1BE, type byte at 0x1BE+4 = 0x1C2
#   p2 entry: offset 0x1CE, type byte at 0x1CE+4 = 0x1D2
#   p3 entry: offset 0x1DE, type byte at 0x1DE+4 = 0x1E2
printf '\x0C' | dd of="$DEV" bs=1 seek=$((0x1C2)) count=1 conv=notrunc status=none
printf '\x83' | dd of="$DEV" bs=1 seek=$((0x1D2)) count=1 conv=notrunc status=none
printf '\x83' | dd of="$DEV" bs=1 seek=$((0x1E2)) count=1 conv=notrunc status=none
# Active flag on p1 (parted's `set 1 boot on` should have done this)
printf '\x80' | dd of="$DEV" bs=1 seek=$((0x1BE)) count=1 conv=notrunc status=none
sync; partprobe "$DEV"; sleep 1

echo -e "${GREEN}4.${NC} Formatting $P1 as FAT32 (label BOOT)..."
mkfs.vfat -F 32 -n BOOT "$P1" >/dev/null

echo -e "${GREEN}5.${NC} Copying BOOT.BIN to $P1..."
MNT=$(mktemp -d)
mount "$P1" "$MNT"
cp "$BOOTBIN" "$MNT/BOOT.BIN"
sync
umount "$MNT"
rmdir "$MNT"

echo -e "${GREEN}6.${NC} Writing signed image to $P2 (BOOT_A) and $P3 (BOOT_B)..."
dd if="$SIGNED" of="$P2" bs=512 conv=fsync status=none
dd if="$SIGNED" of="$P3" bs=512 conv=fsync status=none
sync

echo
echo -e "${GREEN}MBR partition entries (offset 0x1BE):${NC}"
dd if="$DEV" bs=1 skip=$((0x1BE)) count=64 status=none 2>/dev/null | xxd | head -4
echo
echo -e "${GREEN}Done.${NC} Insert into J64, set SW16-3 + SW16-4 ON for SD boot,"
echo -e "and power-cycle. Console on UART1 @ 115200."
