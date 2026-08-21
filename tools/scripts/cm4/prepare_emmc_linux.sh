#!/bin/bash
# prepare_emmc_linux.sh - build the CM4 eMMC layout to boot GCX Yocto Linux under
# wolfBoot (as opposed to prepare_emmc.sh, which boots the disk_app prove-out stub).
#
# Simple first-boot GPT (see docs/Targets.md):
#   p1 = boot   (FAT32) : RPi firmware (start4.elf, fixup4.dat), bcm2711-rpi-cm4.dtb,
#                         config.txt, kernel8.img (= wolfBoot, cm4_emmc_linux.config)
#   p2 = image_a (raw)  : wolfBoot-signed kernel FIT (version 1)
#   p3 = rootfs  (ext4) : the Yocto core-image-ironbutterfly rootfs
#
# Boot chain: RPi firmware loads + patches bcm2711-rpi-cm4.dtb (RAM size, UART
# clock) and hands it to kernel8.img (= wolfBoot) in x0; wolfBoot captures that DTB
# (hal_get_boot_dts), reads/verifies the signed FIT from p2, gzip-loads the kernel to
# 0x10000000, injects "earlycon=pl011,mmio32,0xfe201000 console=ttyAMA0,115200
# root=/dev/mmcblk0p3 rootwait" into the firmware DTB, and hands off EL2->EL1 to
# Linux. Console = PL011 (ttyAMA0).
#
# Usage:
#   # build wolfBoot for Linux first:
#   #   cp config/examples/cm4_emmc_linux.config .config
#   #   make wolfboot.bin CROSS_COMPILE=aarch64-none-elf- DEBUG=1
#   # stage artifacts only (no root):
#   #   bash tools/scripts/cm4/prepare_emmc_linux.sh
#   # with the eMMC presented via rpiboot as /dev/sdN, write it (root):
#   #   sudo bash tools/scripts/cm4/prepare_emmc_linux.sh /dev/sdN
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
OUT="$HERE/linux"        # staging dir for the assembled boot files

# Deploy artifact basenames (machine-suffixed; the plain names are dangling
# symlinks). Override per-name for a different image recipe / machine.
KERNEL_NAME="${KERNEL_NAME:-fitImage-linux.bin-raspberrypi4-64}"
ROOTFS_NAME="${ROOTFS_NAME:-core-image-ironbutterfly-raspberrypi4-64.rootfs.ext4}"
DTB_NAME="${DTB_NAME:-bcm2711-rpi-cm4.dtb}"
KERNEL_GZ="$GCX_DEPLOY/$KERNEL_NAME"
ROOTFS="$GCX_DEPLOY/$ROOTFS_NAME"
DTB="$GCX_DEPLOY/$DTB_NAME"
FW_START="$GCX_DEPLOY/bootfiles/start4.elf"
FW_FIXUP="$GCX_DEPLOY/bootfiles/fixup4.dat"

# ---- artifact prep (no root) ----------------------------------------------
prep_artifacts() {
    local f
    # Idempotent: if everything is already staged (e.g. prepped without sudo,
    # then re-run under sudo where the cross-toolchain/mkimage/sign are not in
    # root's PATH), skip the rebuild. Set FORCE_PREP=1 to force.
    if [ "${FORCE_PREP:-0}" != "1" ] && \
       [ -f "$OUT/cm4-fitImage_v1_signed.bin" ] && [ -f "$OUT/kernel8.img" ] && \
       [ -f "$OUT/config.txt" ] && [ -f "$OUT/start4.elf" ] && \
       [ -f "$OUT/fixup4.dat" ] && [ -f "$OUT/bcm2711-rpi-cm4.dtb" ]; then
        echo "== artifacts already staged in $OUT (skip prep; FORCE_PREP=1 to rebuild) =="
        return 0
    fi

    [ -f "$ROOT/wolfboot.bin" ] || { echo "!! build wolfboot.bin first (cm4_emmc_linux.config)"; exit 1; }
    [ -f "$KEY" ] || { echo "!! signing key not found: $KEY"; exit 1; }
    [ -n "$GCX_DEPLOY" ] || { echo "!! set GCX_DEPLOY=<yocto deploy/images/MACHINE dir> (no default)"; exit 1; }
    for f in "$KERNEL_GZ" "$ROOTFS" "$DTB" "$FW_START" "$FW_FIXUP"; do
        [ -f "$f" ] || { echo "!! missing deploy artifact: $f (check GCX_DEPLOY / *_NAME overrides)"; exit 1; }
    done
    command -v mkimage >/dev/null || { echo "!! mkimage not found (u-boot-tools)"; exit 1; }

    mkdir -p "$OUT"
    echo "== building single-config kernel FIT (kernel-only; DTB from firmware) =="
    cp -L "$KERNEL_GZ" "$OUT/linux.bin.gz"
    ( cd "$OUT" && sed 's#\.\./Image\.gz#linux.bin.gz#' "$ROOT/hal/cm4.its" > cm4.its \
      && mkimage -f cm4.its cm4-fitImage >/dev/null )
    echo "== wolfBoot-signing the FIT (v1, ECC384/SHA384) =="
    ( cd "$ROOT" && IMAGE_HEADER_SIZE="$IMG_HDR" ./tools/keytools/sign --ecc384 --sha384 \
        "$OUT/cm4-fitImage" "$KEY" 1 >/dev/null )
    [ -f "$OUT/cm4-fitImage_v1_signed.bin" ] || { echo "!! signing failed"; exit 1; }

    echo "== assembling boot partition files =="
    cp "$FW_START" "$OUT/start4.elf"
    cp "$FW_FIXUP" "$OUT/fixup4.dat"
    cp "$DTB"      "$OUT/bcm2711-rpi-cm4.dtb"
    cp "$ROOT/wolfboot.bin" "$OUT/kernel8.img"
    cat > "$OUT/config.txt" <<'EOF'
arm_64bit=1
enable_uart=1
# Route the PL011 (uart0) onto GPIO14/15 in place of the mini-UART: the PL011
# registers cleanly in Linux (ttyAMA0) whereas the bcm2835-aux mini-UART does not
# on this DTB. wolfBoot is built with CM4_UART_PL011 to match (cm4_emmc_linux.config).
dtoverlay=disable-bt
# Pin the PL011 UARTCLK to 48MHz - wolfBoot's PL011 init assumes 48MHz for its
# 115200 divisor (hal/cm4.c), so this keeps the baud consistent.
init_uart_clock=48000000
# wolfBoot is the "kernel" the firmware loads, linked absolutely at 0x200000.
kernel=kernel8.img
kernel_address=0x200000
# Force the CM4 device tree so the firmware patches it (RAM size, UART clock)
# and hands it to wolfBoot in x0 (hal_get_boot_dts captures it).
device_tree=bcm2711-rpi-cm4.dtb
EOF
    echo "== staged in $OUT: $(ls "$OUT" | tr '\n' ' ')"
    echo "   FIT $(stat -c%s "$OUT/cm4-fitImage_v1_signed.bin") B, rootfs $(stat -Lc%s "$ROOTFS") B"
}

# ---- device write (root) --------------------------------------------------
write_device() {
    # MP is intentionally NOT local: the global EXIT trap references it, and a
    # function-local would be out of scope (set -u -> "unbound") when the trap
    # fires on a set -e abort.
    local t root_src root_pk P1 P2 P3 ok p
    [ "$(id -u)" -eq 0 ] || { echo "!! writing $DEV requires root"; exit 1; }
    for t in sgdisk partprobe mkfs.vfat lsblk udevadm dd findmnt mountpoint; do
        command -v "$t" >/dev/null || { echo "!! missing required tool: $t"; exit 1; }
    done
    [ -b "$DEV" ] || { echo "!! not a block device: $DEV"; exit 1; }
    [ -f "$OUT/cm4-fitImage_v1_signed.bin" ] || { echo "!! FIT not staged; run prep first"; exit 1; }
    [ -f "$OUT/kernel8.img" ] || { echo "!! kernel8.img not staged; run prep first"; exit 1; }
    P1="${DEV}1"; P2="${DEV}2"; P3="${DEV}3"
    [ -b "${DEV}1" ] || { P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3"; }

    # Refuse the disk currently backing '/'. Hoisted ABOVE the KERNEL_ONLY branch
    # so BOTH the fast path and the full repartition path are guarded - the
    # KERNEL_ONLY path also writes $DEV (P1 files + P2 FIT) and must not be able
    # to clobber a live system disk unguarded. Override with ALLOW_ANY_DISK=1.
    root_src="$(findmnt -no SOURCE / 2>/dev/null || true)"
    root_pk="$(lsblk -no PKNAME "$root_src" 2>/dev/null | head -1)"
    if [ "${ALLOW_ANY_DISK:-0}" != "1" ]; then
        # Fail closed: an empty PKNAME (LVM/btrfs/overlay root, empty root_src)
        # would otherwise leave the guard comparing against a bare "/dev/".
        [ -n "$root_pk" ] || { echo "!! cannot determine root disk (lsblk PKNAME empty); set ALLOW_ANY_DISK=1 to override"; exit 1; }
        if [ "$DEV" = "/dev/$root_pk" ]; then
            echo "!! refusing to write the root disk $DEV (set ALLOW_ANY_DISK=1 to override)"; exit 1
        fi
    fi

    # Fast iteration path: only wolfBoot (kernel8.img) changed - refresh the boot
    # partition + FIT without repartitioning or re-writing the 600MB rootfs.
    if [ "${KERNEL_ONLY:-0}" = "1" ]; then
        [ -b "$P1" ] || { echo "!! KERNEL_ONLY: $P1 not found (device not laid out yet)"; exit 1; }
        # Assert P2 too before the dd below: a missing node makes dd silently
        # create a regular file instead of writing the partition.
        [ -b "$P2" ] || { echo "!! partition node $P2 missing (partprobe/udev race?)"; exit 1; }
        # Confirm before overwriting a live partition (no repartition, so a
        # lighter prompt than the full-erase path, but still fail-safe). The
        # root-disk guard above always runs; ASSUME_YES=1 skips only this
        # interactive prompt (for scripted/known-good flashing).
        echo "== KERNEL_ONLY target: $DEV =="; lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
        if [ "${ASSUME_YES:-0}" = "1" ]; then
            echo "ASSUME_YES=1: proceeding without prompt"
        else
            read -rp "Overwrite kernel8.img/config/dtb on $P1 and the signed FIT on $P2? [type YES] " ok
            [ "$ok" = "YES" ] || { echo "aborted"; exit 1; }
        fi
        MP=$(mktemp -d)
        # Clean up on exit even if mount/cp fails under set -e. A RETURN trap does
        # NOT fire when set -e aborts inside a function; EXIT does. The mountpoint
        # -q guard makes it idempotent with the explicit umount below.
        trap 'if [ -n "${MP:-}" ]; then mountpoint -q "$MP" && umount "$MP"; rmdir "$MP" 2>/dev/null || true; fi' EXIT
        echo "== KERNEL_ONLY: refresh kernel8.img/config/dtb on $P1 + FIT on $P2 =="
        mount "$P1" "$MP"
        cp "$OUT"/kernel8.img "$OUT"/config.txt "$OUT"/bcm2711-rpi-cm4.dtb "$MP"/
        sync; umount "$MP"
        dd if="$OUT/cm4-fitImage_v1_signed.bin" of="$P2" bs=4k conv=fsync status=none
        sync
        echo "== KERNEL_ONLY done. BOOT OFF, power-cycle, watch the PL011 console (ttyAMA0). =="
        return 0
    fi

    [ -f "$ROOTFS" ] || { echo "!! rootfs not found: $ROOTFS (set GCX_DEPLOY)"; exit 1; }
    echo "== target: $DEV =="; lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
    read -rp "Repartition and ERASE $DEV? [type YES] " ok
    [ "$ok" = "YES" ] || { echo "aborted"; exit 1; }

    echo "== GPT: p1 boot 128M / p2 image_a 64M / p3 rootfs (rest) =="
    sgdisk --zap-all "$DEV" >/dev/null
    sgdisk -n 1:2048:+128M -t 1:0700 -c 1:boot    "$DEV" >/dev/null
    sgdisk -n 2:0:+64M     -t 2:8300 -c 2:image_a "$DEV" >/dev/null
    sgdisk -n 3:0:0        -t 3:8300 -c 3:rootfs  "$DEV" >/dev/null
    partprobe "$DEV"; udevadm settle
    P1="${DEV}1"; P2="${DEV}2"; P3="${DEV}3"
    [ -b "$P1" ] || { P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3"; }
    # Assert every node exists before any dd: a missing node makes dd silently
    # create a regular file instead of writing the partition.
    for p in "$P1" "$P2" "$P3"; do
        [ -b "$p" ] || { echo "!! partition node $p missing (partprobe/udev race?)"; exit 1; }
    done

    echo "== FAT boot partition ($P1) =="
    mkfs.vfat -n BOOT "$P1" >/dev/null
    MP=$(mktemp -d)
    # Clean up on exit even if mount/cp fails under set -e. A RETURN trap does NOT
    # fire when set -e aborts inside a function; EXIT does. The mountpoint -q guard
    # makes it idempotent with the explicit umount below.
    trap 'if [ -n "${MP:-}" ]; then mountpoint -q "$MP" && umount "$MP"; rmdir "$MP" 2>/dev/null || true; fi' EXIT
    mount "$P1" "$MP"
    cp "$OUT"/start4.elf "$OUT"/fixup4.dat "$OUT"/bcm2711-rpi-cm4.dtb \
       "$OUT"/config.txt "$OUT"/kernel8.img "$MP"/
    sync; umount "$MP"

    echo "== signed kernel FIT -> image_a ($P2) =="
    dd if="$OUT/cm4-fitImage_v1_signed.bin" of="$P2" bs=4k conv=fsync status=none

    echo "== rootfs -> $P3 (ext4), then grow to fill =="
    dd if="$ROOTFS" of="$P3" bs=4M conv=fsync status=none
    # e2fsck returns 1/2 when it fixed errors (not a failure), so || true is fine.
    command -v e2fsck >/dev/null && e2fsck -pf "$P3" >/dev/null 2>&1 || true
    # Distinguish "tool missing" from "tool ran but failed" - a swallowed
    # resize2fs failure hides a real error behind a "missing" message.
    if command -v resize2fs >/dev/null; then
        resize2fs "$P3" >/dev/null 2>&1 || echo "   !! resize2fs failed on $P3 (rootfs left at image size)"
    else
        echo "   (resize2fs missing; rootfs stays at image size)"
    fi
    sync
    echo "== done. Set BOOT switch OFF, power-cycle, watch the CM4 PL011 console (ttyAMA0). =="
    echo "   cmdline: earlycon=pl011,mmio32,0xfe201000 console=ttyAMA0,115200 root=/dev/mmcblk0p3 rootfstype=ext4 rootwait"
}

prep_artifacts
if [ -n "$DEV" ]; then
    write_device
else
    echo "(no device given; artifacts only. Re-run with: sudo bash $0 /dev/sdN)"
fi
