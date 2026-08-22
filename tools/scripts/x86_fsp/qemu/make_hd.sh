#!/bin/bash
SIGN=${SIGN:-"--ecc256"}
HASH=${HASH:-"--sha256"}

IMAGE=${IMAGE:-"bzImage"}

# FS=1 adds two extra partitions holding read-only filesystems, for testing
# the DISK_FS boot path. The raw OFP_A / OFP_B partitions are laid out
# identically either way, so the default image is unchanged.
FS=${FS:-0}
BOOT_FILE=${BOOT_FILE:-"os.itb"}

set -e

if [ "$FS" = "1" ]; then
    # A conformant FAT32 volume needs at least 65525 clusters, so the FAT
    # partition cannot be smaller than about 33MB whatever the image holds.
    dd if=/dev/zero of=app.bin bs=1M count=128
    /sbin/fdisk app.bin <<EOF
g
n
1

+16M
n
2

+16M
n
3

+40M
n
4

+16M
x
n
1
OFP_A
n
2
OFP_B
n
3
FS_A
n
4
FS_B
r
w
EOF
else
    dd if=/dev/zero of=app.bin bs=1M count=64
    /sbin/fdisk app.bin <<EOF
g
n
1

+16M
n


+16M
x
n
1
OFP_A
n
2
OFP_B
r
w
EOF
fi

cp ${IMAGE} "image.bin"
tools/keytools/sign $SIGN $HASH image.bin wolfboot_signing_private_key.der 1
tools/keytools/sign $SIGN $HASH image.bin wolfboot_signing_private_key.der 2
dd if=image_v1_signed.bin of=app.bin bs=512 seek=2048 conv=notrunc
dd if=image_v2_signed.bin of=app.bin bs=512 seek=34816 conv=notrunc

if [ "$FS" = "1" ]; then
    # Read the start sectors back from the table rather than assuming
    # fdisk's alignment.
    P3_LBA=$(/sbin/fdisk -l app.bin | awk '/^app.bin3/ {print $2}')
    P4_LBA=$(/sbin/fdisk -l app.bin | awk '/^app.bin4/ {print $2}')
    if [ -z "$P3_LBA" ] || [ -z "$P4_LBA" ]; then
        echo "could not determine filesystem partition offsets" >&2
        exit 1
    fi

    # Slot A: FAT32, version 1.
    rm -f fs_a.img
    dd if=/dev/zero of=fs_a.img bs=1M count=40
    mkfs.vfat -F 32 -s 1 -S 512 -n WBFSA fs_a.img >/dev/null
    MTOOLS_SKIP_CHECK=1 mmd -i fs_a.img ::/boot
    MTOOLS_SKIP_CHECK=1 mcopy -i fs_a.img image_v1_signed.bin \
        ::/boot/${BOOT_FILE}
    dd if=fs_a.img of=app.bin bs=512 seek=${P3_LBA} conv=notrunc

    # Slot B: ext4, version 2, built with the stock mkfs feature set.
    rm -rf .fs_b_root fs_b.img
    mkdir -p .fs_b_root/boot
    cp image_v2_signed.bin .fs_b_root/boot/${BOOT_FILE}
    mke2fs -q -F -t ext4 -b 1024 -L WBFSB -d .fs_b_root fs_b.img 16384
    dd if=fs_b.img of=app.bin bs=512 seek=${P4_LBA} conv=notrunc
    rm -rf .fs_b_root

    echo "filesystem partitions: FAT32 at LBA ${P3_LBA}, ext4 at LBA ${P4_LBA}"
fi
