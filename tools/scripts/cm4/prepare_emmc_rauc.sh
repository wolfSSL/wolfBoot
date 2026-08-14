#!/bin/bash
# prepare_emmc_rauc.sh - lay out the CM4 eMMC for production RAUC A/B boot under
# wolfBoot (cm4_emmc_rauc.config). wolfBoot replaces GCX's U-Boot as the RAUC
# slot arbiter: it reads a raw U-Boot-env partition, runs the BOOT_ORDER state
# machine, and boots the shared kernel FIT with root= pointing at the active slot.
#
# GPT (0-based indices in [brackets]):
#   p1 boot     FAT  [0] : RPi fw + wolfBoot kernel8.img + config.txt (disable-bt)
#   p2 uboot-env raw [1] : U-Boot env (mkenvimage; RAUC fw_env.config -> here)
#   p3 fitImage raw  [2] : wolfBoot-signed kernel FIT (shared)
#   p4 rootfs_A ext4 [3] : RAUC slot A  (initial rootfs)
#   p5 rootfs_B ext4 [4] : RAUC slot B  (initial rootfs copy)
#   p6 data     ext4 [5] : persistent /data
#
# Usage:
#   cp config/examples/cm4_emmc_rauc.config .config
#   make wolfboot.bin CROSS_COMPILE=aarch64-none-elf- DEBUG=1
#   bash tools/scripts/cm4/prepare_emmc_rauc.sh            (stage only, no root)
#   sudo bash tools/scripts/cm4/prepare_emmc_rauc.sh /dev/sdN   (write, root)
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3 (see wolfBoot COPYING).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/tools/scripts/cm4"
CROSS="${CROSS_COMPILE:-aarch64-none-elf-}"
KEY="${PRIVATE_KEY:-$ROOT/wolfboot_signing_private_key.der}"
IMG_HDR="${IMAGE_HEADER_SIZE:-1024}"
# Yocto deploy dir - REQUIRED (no default; set GCX_DEPLOY to the
# deploy/images/<MACHINE> directory of your build).
GCX_DEPLOY="${GCX_DEPLOY:-}"
DEV="${1:-}"
OUT="$HERE/rauc"

# Deploy artifact basenames; override per-name for a different recipe / machine.
KERNEL_NAME="${KERNEL_NAME:-fitImage-linux.bin-raspberrypi4-64}"
ROOTFS_NAME="${ROOTFS_NAME:-core-image-ironbutterfly-raspberrypi4-64.rootfs.ext4}"
DTB_NAME="${DTB_NAME:-bcm2711-rpi-cm4.dtb}"
KERNEL_GZ="$GCX_DEPLOY/$KERNEL_NAME"
ROOTFS="$GCX_DEPLOY/$ROOTFS_NAME"
DTB="$GCX_DEPLOY/$DTB_NAME"
FW_START="$GCX_DEPLOY/bootfiles/start4.elf"
FW_FIXUP="$GCX_DEPLOY/bootfiles/fixup4.dat"

prep_artifacts() {
    local f
    if [ "${FORCE_PREP:-0}" != "1" ] && \
       [ -f "$OUT/cm4-fitImage_v1_signed.bin" ] && [ -f "$OUT/kernel8.img" ] && \
       [ -f "$OUT/config.txt" ] && [ -f "$OUT/uboot.env" ] && \
       [ -f "$OUT/start4.elf" ] && [ -f "$OUT/fixup4.dat" ] && \
       [ -f "$OUT/bcm2711-rpi-cm4.dtb" ]; then
        echo "== artifacts already staged in $OUT (skip prep; FORCE_PREP=1 to rebuild) =="
        return 0
    fi
    [ -f "$ROOT/wolfboot.bin" ] || { echo "!! build wolfboot.bin first (cm4_emmc_rauc.config)"; exit 1; }
    [ -f "$KEY" ] || { echo "!! signing key not found: $KEY"; exit 1; }
    [ -n "$GCX_DEPLOY" ] || { echo "!! set GCX_DEPLOY=<yocto deploy/images/MACHINE dir> (no default)"; exit 1; }
    for f in "$KERNEL_GZ" "$DTB" "$FW_START" "$FW_FIXUP"; do
        [ -f "$f" ] || { echo "!! missing deploy artifact: $f (check GCX_DEPLOY / *_NAME overrides)"; exit 1; }
    done
    command -v mkimage >/dev/null || { echo "!! mkimage not found (u-boot-tools)"; exit 1; }
    command -v mkenvimage >/dev/null || { echo "!! mkenvimage not found (u-boot-tools)"; exit 1; }

    mkdir -p "$OUT"
    echo "== building + signing the shared kernel FIT =="
    cp -L "$KERNEL_GZ" "$OUT/linux.bin.gz"
    ( cd "$OUT" && sed 's#\.\./Image\.gz#linux.bin.gz#' "$ROOT/hal/cm4.its" > cm4.its \
      && mkimage -f cm4.its cm4-fitImage >/dev/null )
    ( cd "$ROOT" && IMAGE_HEADER_SIZE="$IMG_HDR" ./tools/keytools/sign --ecc384 --sha384 \
        "$OUT/cm4-fitImage" "$KEY" 1 >/dev/null )
    [ -f "$OUT/cm4-fitImage_v1_signed.bin" ] || { echo "!! signing failed"; exit 1; }

    echo "== generating initial U-Boot env (BOOT_ORDER 'A B', tries=3) =="
    printf 'BOOT_ORDER=A B\nBOOT_A_LEFT=3\nBOOT_B_LEFT=3\n' > "$OUT/env.txt"
    # 0x4000 must match wolfBoot UBOOT_ENV_SIZE and RAUC fw_env.config size.
    mkenvimage -s 0x4000 -o "$OUT/uboot.env" "$OUT/env.txt"

    echo "== assembling boot partition files =="
    cp "$FW_START" "$OUT/start4.elf"
    cp "$FW_FIXUP" "$OUT/fixup4.dat"
    cp "$DTB"      "$OUT/bcm2711-rpi-cm4.dtb"
    cp "$ROOT/wolfboot.bin" "$OUT/kernel8.img"
    cat > "$OUT/config.txt" <<'EOF'
arm_64bit=1
enable_uart=1
dtoverlay=disable-bt
init_uart_clock=48000000
kernel=kernel8.img
kernel_address=0x200000
device_tree=bcm2711-rpi-cm4.dtb
EOF
    echo "== staged in $OUT: $(ls "$OUT" | tr '\n' ' ')"
}

write_device() {
    # MP is intentionally NOT local: the global EXIT trap below references it, and
    # a function-local would be out of scope (set -u -> "unbound") when the trap
    # fires on a set -e abort.
    local t root_src root_pk P1 P2 P3 P4 P5 P6 p ok
    [ "$(id -u)" -eq 0 ] || { echo "!! writing $DEV requires root"; exit 1; }
    for t in sgdisk partprobe mkfs.vfat mkfs.ext4 lsblk udevadm dd tune2fs; do
        command -v "$t" >/dev/null || { echo "!! missing required tool: $t"; exit 1; }
    done
    [ -b "$DEV" ] || { echo "!! not a block device: $DEV"; exit 1; }
    [ -f "$ROOTFS" ] || { echo "!! rootfs not found: $ROOTFS (set GCX_DEPLOY)"; exit 1; }
    [ -f "$OUT/uboot.env" ] || { echo "!! env not staged; run prep first"; exit 1; }
    root_src="$(findmnt -no SOURCE / 2>/dev/null || true)"
    root_pk="$(lsblk -no PKNAME "$root_src" 2>/dev/null | head -1)"
    if [ "${ALLOW_ANY_DISK:-0}" != "1" ]; then
        # Fail closed: an empty PKNAME (LVM/btrfs/overlay root, empty root_src)
        # would otherwise leave the guard comparing against a bare "/dev/".
        [ -n "$root_pk" ] || { echo "!! cannot determine root disk (lsblk PKNAME empty); set ALLOW_ANY_DISK=1 to override"; exit 1; }
        if [ "$DEV" = "/dev/$root_pk" ]; then
            echo "!! refusing to erase the root disk $DEV (set ALLOW_ANY_DISK=1 to override)"; exit 1
        fi
    fi

    # Fast iteration path: refresh only wolfBoot (p1 boot) + U-Boot env (p2) +
    # signed FIT (p3) on an already-laid-out RAUC eMMC, WITHOUT repartitioning or
    # re-writing the two 4GB rootfs slots. The root-disk guard above still runs.
    P1="${DEV}1"; P2="${DEV}2"; P3="${DEV}3"
    [ -b "$P1" ] || { P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3"; }
    if [ "${KERNEL_ONLY:-0}" = "1" ]; then
        for p in "$P1" "$P2" "$P3"; do
            [ -b "$p" ] || { echo "!! KERNEL_ONLY: $p not found (device not laid out yet)"; exit 1; }
        done
        echo "== KERNEL_ONLY target: $DEV =="; lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
        if [ "${ASSUME_YES:-0}" = "1" ]; then
            echo "ASSUME_YES=1: proceeding without prompt"
        else
            read -rp "Overwrite boot ($P1) + uboot-env ($P2) + FIT ($P3)? [type YES] " ok
            [ "$ok" = "YES" ] || { echo "aborted"; exit 1; }
        fi
        MP=$(mktemp -d)
        trap 'if [ -n "${MP:-}" ]; then mountpoint -q "$MP" && umount "$MP"; rmdir "$MP" 2>/dev/null || true; fi' EXIT
        echo "== KERNEL_ONLY: refresh boot files on $P1 =="
        mount "$P1" "$MP"
        cp "$OUT"/start4.elf "$OUT"/fixup4.dat "$OUT"/bcm2711-rpi-cm4.dtb \
           "$OUT"/config.txt "$OUT"/kernel8.img "$MP"/
        sync; umount "$MP"
        echo "== U-Boot env -> uboot-env ($P2) =="
        dd if="$OUT/uboot.env" of="$P2" bs=4k conv=fsync status=none
        echo "== signed kernel FIT -> fitImage ($P3) =="
        dd if="$OUT/cm4-fitImage_v1_signed.bin" of="$P3" bs=4k conv=fsync status=none
        sync
        echo "== KERNEL_ONLY done. Set BOOT OFF, power-cycle, watch the console. =="
        return 0
    fi

    echo "== target: $DEV =="; lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
    read -rp "Repartition and ERASE $DEV? [type YES] " ok
    [ "$ok" = "YES" ] || { echo "aborted"; exit 1; }

    echo "== GPT: boot 128M / uboot-env 8M / fitImage 64M / rootfs_A 4G / rootfs_B 4G / data (rest) =="
    sgdisk --zap-all "$DEV" >/dev/null
    sgdisk -n 1:2048:+128M -t 1:0700 -c 1:boot      "$DEV" >/dev/null
    sgdisk -n 2:0:+8M      -t 2:8300 -c 2:uboot-env "$DEV" >/dev/null
    sgdisk -n 3:0:+64M     -t 3:8300 -c 3:fitImage  "$DEV" >/dev/null
    sgdisk -n 4:0:+4G      -t 4:8300 -c 4:rootfs_A  "$DEV" >/dev/null
    sgdisk -n 5:0:+4G      -t 5:8300 -c 5:rootfs_B  "$DEV" >/dev/null
    sgdisk -n 6:0:0        -t 6:8300 -c 6:data      "$DEV" >/dev/null
    partprobe "$DEV"; udevadm settle
    P1="${DEV}1"; P2="${DEV}2"; P3="${DEV}3"; P4="${DEV}4"; P5="${DEV}5"; P6="${DEV}6"
    [ -b "$P1" ] || { P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3"; P4="${DEV}p4"; P5="${DEV}p5"; P6="${DEV}p6"; }
    # Assert every node exists before any dd/mkfs: a missing node makes dd/mkfs
    # silently create a regular file instead of writing the partition.
    for p in "$P1" "$P2" "$P3" "$P4" "$P5" "$P6"; do
        [ -b "$p" ] || { echo "!! partition node $p missing (partprobe/udev race?)"; exit 1; }
    done

    echo "== FAT boot partition ($P1) =="
    mkfs.vfat -n BOOT "$P1" >/dev/null
    MP=$(mktemp -d)
    # Clean up on exit even if mount/cp fails under set -e. A RETURN trap does NOT
    # fire when set -e aborts inside a function; EXIT does. The mountpoint -q guard
    # makes it idempotent with the explicit umount below. The ${MP:-} guard keeps
    # the trap safe under set -u if it fires before MP is assigned.
    trap 'if [ -n "${MP:-}" ]; then mountpoint -q "$MP" && umount "$MP"; rmdir "$MP" 2>/dev/null || true; fi' EXIT
    mount "$P1" "$MP"
    cp "$OUT"/start4.elf "$OUT"/fixup4.dat "$OUT"/bcm2711-rpi-cm4.dtb \
       "$OUT"/config.txt "$OUT"/kernel8.img "$MP"/
    sync; umount "$MP"

    echo "== U-Boot env -> uboot-env ($P2) =="
    dd if="$OUT/uboot.env" of="$P2" bs=4k conv=fsync status=none
    echo "== signed kernel FIT -> fitImage ($P3) =="
    dd if="$OUT/cm4-fitImage_v1_signed.bin" of="$P3" bs=4k conv=fsync status=none

    echo "== rootfs -> slot A ($P4) and slot B ($P5), then grow both =="
    dd if="$ROOTFS" of="$P4" bs=4M conv=fsync status=none
    dd if="$ROOTFS" of="$P5" bs=4M conv=fsync status=none
    for p in "$P4" "$P5"; do
        # e2fsck returns 1/2 when it fixed errors (not a failure), so || true is fine.
        command -v e2fsck >/dev/null && e2fsck -pf "$p" >/dev/null 2>&1 || true
        # Distinguish "tool missing" from "tool ran but failed" - a swallowed
        # resize2fs failure hides a real error behind a silent no-op.
        if command -v resize2fs >/dev/null; then
            resize2fs "$p" >/dev/null 2>&1 || echo "   !! resize2fs failed on $p (rootfs left at image size)"
        else
            echo "   (resize2fs missing; rootfs stays at image size)"
        fi
    done
    # Slots A and B were dd'd from the same image, so they share ext4 UUID+label.
    # Re-stamp a distinct random UUID + label on each so nothing resolves root by
    # a duplicate UUID/LABEL once RAUC updates a slot.
    if command -v tune2fs >/dev/null; then
        tune2fs -U random -L rootfs_A "$P4" >/dev/null 2>&1 || echo "   !! tune2fs restamp failed on $P4 (slot A)"
        tune2fs -U random -L rootfs_B "$P5" >/dev/null 2>&1 || echo "   !! tune2fs restamp failed on $P5 (slot B)"
    else
        echo "   (tune2fs missing; A/B slots keep duplicate ext4 UUID/label)"
    fi
    echo "== data partition ($P6) =="
    mkfs.ext4 -q -L data "$P6" >/dev/null 2>&1 || echo "   !! mkfs.ext4 failed on $P6 (no /data filesystem)"
    sync
    echo "== done. BOOT switch OFF, power-cycle, watch the CM4 PL011 console (ttyAMA0). =="
    echo "   wolfBoot picks the RAUC slot from p2 and boots root=/dev/mmcblk0p4|p5."
}

prep_artifacts
if [ -n "$DEV" ]; then
    write_device
else
    echo "(no device given; artifacts only. Re-run with: sudo bash $0 /dev/sdN)"
fi
