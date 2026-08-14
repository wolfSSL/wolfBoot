#!/bin/bash
# prepare_emmc.sh - build the eMMC/SD layout for the CM4 wolfBoot disk-boot test.
#
# Creates a GPT with:
#   p1 = boot   (FAT32) : RPi firmware (start4.elf, fixup4.dat), config.txt,
#                         kernel8.img (= wolfBoot, built from cm4_emmc.config)
#   p2 = image_a (raw)  : signed disk_app ELF, version 1  (BOOT_PART_A=1)
#   p3 = image_b (raw)  : signed disk_app ELF, version 2  (BOOT_PART_B=2)
#
# wolfBoot (loaded to RAM by the RPi firmware) then drives the BCM2711 EMMC2
# controller to read p2/p3, verifies ECC384/SHA384, and boots the higher
# version. Prove-out prints ">>> CM4 DISK APP OK <<<" over the mini-UART (AUX).
#
# Usage:
#   # 1) build wolfBoot first:
#   #    cp config/examples/cm4_emmc.config .config
#   #    make wolfboot.bin CROSS_COMPILE=aarch64-none-elf-   (add DEBUG_SDHCI for bring-up)
#   # 2) prep artifacts only (no root):
#   #    bash tools/scripts/cm4/prepare_emmc.sh
#   # 3) with the eMMC presented via rpiboot as /dev/sdN, write it (root):
#   #    sudo bash tools/scripts/cm4/prepare_emmc.sh /dev/sdN
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3 (see wolfBoot COPYING).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"   # wolfboot repo root
HERE="$ROOT/tools/scripts/cm4"
CROSS="${CROSS_COMPILE:-aarch64-none-elf-}"
KEY="${PRIVATE_KEY:-$ROOT/wolfboot_signing_private_key.der}"
IMG_HDR="${IMAGE_HEADER_SIZE:-1024}"
# Pin the RPi firmware to a specific ref for reproducibility - these blobs are
# the first link of the boot chain. Override with FWREF=<tag|sha> (a tag like
# 1.20240529, or a full commit sha).
FWREF="${FWREF:-1.20240529}"
FWBASE="https://raw.githubusercontent.com/raspberrypi/firmware/${FWREF}/boot"
DEV="${1:-}"

# ---- artifact prep (no root) ----------------------------------------------
prep_artifacts() {
    # Idempotent: if everything is already staged (e.g. prepped without sudo,
    # then re-run under sudo to write the device where the cross-toolchain is
    # not in PATH), skip the rebuild. Set FORCE_PREP=1 to force.
    if [ "${FORCE_PREP:-0}" != "1" ] && \
       [ -f "$HERE/disk_app_v1_signed.bin" ] && \
       [ -f "$HERE/disk_app_v2_signed.bin" ] && \
       [ -f "$HERE/fw/kernel8.img" ] && [ -f "$HERE/fw/start4.elf" ] && \
       [ -f "$HERE/fw/config.txt" ]; then
        echo "== artifacts already staged in $HERE (skip prep; FORCE_PREP=1 to rebuild) =="
        return 0
    fi

    [ -f "$ROOT/wolfboot.bin" ] || { echo "!! build wolfboot.bin first (cm4_emmc.config)"; exit 1; }
    [ -f "$KEY" ] || { echo "!! signing key not found: $KEY"; exit 1; }

    echo "== building + signing disk_app (v1, v2) =="
    "${CROSS}gcc" -nostdlib -nostartfiles -Wl,-Ttext=0x10000000 \
        -o "$HERE/disk_app.elf" "$HERE/disk_app.S"
    ( cd "$ROOT" && \
      IMAGE_HEADER_SIZE="$IMG_HDR" ./tools/keytools/sign --ecc384 --sha384 \
        "$HERE/disk_app.elf" "$KEY" 1 >/dev/null && \
      IMAGE_HEADER_SIZE="$IMG_HDR" ./tools/keytools/sign --ecc384 --sha384 \
        "$HERE/disk_app.elf" "$KEY" 2 >/dev/null )
    ls -la "$HERE"/disk_app_v1_signed.bin "$HERE"/disk_app_v2_signed.bin

    echo "== fetching RPi BCM2711 firmware (ref $FWREF) =="
    mkdir -p "$HERE/fw"
    # These blobs are the FIRST link of the boot chain (start4.elf runs on the
    # VideoCore before the ARM cores load wolfBoot), i.e. below wolfBoot's root
    # of trust. Pinning a full commit SHA in FWREF and committing an integrity
    # manifest (tools/scripts/cm4/fw.sha256, lines "<sha256>  <filename>") is
    # strongly recommended; when the manifest is present each blob is verified.
    for f in start4.elf fixup4.dat bcm2711-rpi-cm4.dtb; do
        dst="$HERE/fw/$f"
        # Re-fetch when forced or when the cached file is missing/empty. A
        # truncated download leaves a short file that a plain [ -f ] would treat
        # as present (poisoning the cache), so test [ -s ] and download to a temp
        # file, renaming only on success so an interrupted transfer never lands.
        if [ "${FORCE_PREP:-0}" = "1" ] || [ ! -s "$dst" ]; then
            tmp="$(mktemp "$dst.XXXXXX")"
            if ! curl -fsSL -o "$tmp" "$FWBASE/$f"; then
                rm -f "$tmp"; echo "!! failed to download $f from $FWBASE"; exit 1
            fi
            [ -s "$tmp" ] || { rm -f "$tmp"; echo "!! downloaded $f is empty"; exit 1; }
            mv -f "$tmp" "$dst"
        fi
    done
    if [ -f "$HERE/fw.sha256" ]; then
        echo "== verifying firmware against fw.sha256 =="
        ( cd "$HERE/fw" && sha256sum -c "$HERE/fw.sha256" ) || {
            echo "!! firmware checksum verification FAILED"; exit 1; }
    else
        echo "   (no fw.sha256 manifest; firmware not integrity-checked - see comment above)"
    fi

    cat > "$HERE/fw/config.txt" <<'EOF'
arm_64bit=1
kernel=kernel8.img
# wolfBoot is linked absolutely at 0x200000; pin the load address to match.
kernel_address=0x200000
enable_uart=1
uart_2ndstage=1
# NOTE: this stub (cm4.config) uses the BCM2711 mini-UART (AUX, ttyS0) on
# GPIO14/15 - do NOT add dtoverlay=disable-bt here (that routes the PL011 onto
# GPIO14/15 instead and kills the mini-UART console). disable-bt belongs only to
# the CM4_UART_PL011 Linux path (see prepare_emmc_linux.sh).
init_uart_clock=48000000
init_uart_baud=115200
EOF
    cp "$ROOT/wolfboot.bin" "$HERE/fw/kernel8.img"
    echo "== artifacts ready in $HERE (fw/ + disk_app_v{1,2}_signed.bin) =="
}

# ---- device write (root) --------------------------------------------------
write_device() {
    local t root_src root_pk P1 P2 P3 MP ok p
    [ "$(id -u)" -eq 0 ] || { echo "!! writing $DEV requires root"; exit 1; }
    for t in sgdisk partprobe mkfs.vfat lsblk udevadm; do
        command -v "$t" >/dev/null || { echo "!! missing required tool: $t"; exit 1; }
    done
    [ -b "$DEV" ] || { echo "!! not a block device: $DEV"; exit 1; }
    # Refuse the disk currently backing '/' (a real safety check, not a
    # bench-specific node name). Override with ALLOW_ANY_DISK=1.
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
    echo "== target: $DEV =="; lsblk -o NAME,SIZE,FSTYPE,LABEL "$DEV"
    read -rp "Repartition and ERASE $DEV? [type YES] " ok
    [ "$ok" = "YES" ] || { echo "aborted"; exit 1; }

    # A/B partitions are 32M - far larger than the signed disk_app test payload
    # (~64KB). They intentionally need not match WOLFBOOT_PARTITION_SIZE (0x4000000
    # = 64M) in cm4_emmc.config: the disk path reads only the image at the
    # partition start, so the partition just has to be big enough to hold it.
    echo "== GPT: p1 boot 128M / p2 image_a 32M / p3 image_b 32M =="
    sgdisk --zap-all "$DEV" >/dev/null
    sgdisk -n 1:2048:+128M -t 1:0700 -c 1:boot "$DEV" >/dev/null
    sgdisk -n 2:0:+32M     -t 2:8300 -c 2:image_a "$DEV" >/dev/null
    sgdisk -n 3:0:+32M     -t 3:8300 -c 3:image_b "$DEV" >/dev/null
    partprobe "$DEV"; udevadm settle
    P1="${DEV}1"; P2="${DEV}2"; P3="${DEV}3"
    # some kernels use pN naming for nvme/mmc; handle 'p' suffix
    [ -b "$P1" ] || { P1="${DEV}p1"; P2="${DEV}p2"; P3="${DEV}p3"; }
    # Assert every node exists before any dd: a missing node makes dd silently
    # create a regular file instead of writing the partition.
    for p in "$P1" "$P2" "$P3"; do
        [ -b "$p" ] || { echo "!! partition node $p missing (partprobe/udev race?)"; exit 1; }
    done

    echo "== FAT boot partition ($P1) =="
    mkfs.vfat -n BOOT "$P1" >/dev/null
    MP=$(mktemp -d)
    # Clean up the mount point on exit even if mount/cp fails under set -e. A
    # RETURN trap does NOT fire when set -e aborts inside a function; EXIT does.
    # The mountpoint -q guard makes it idempotent with the explicit umount below.
    trap 'mountpoint -q "$MP" && umount "$MP"; rmdir "$MP" 2>/dev/null || true' EXIT
    mount "$P1" "$MP"
    cp "$HERE"/fw/start4.elf "$HERE"/fw/fixup4.dat "$HERE"/fw/bcm2711-rpi-cm4.dtb \
       "$HERE"/fw/config.txt "$HERE"/fw/kernel8.img "$MP"/
    sync; umount "$MP"

    echo "== raw signed images -> A ($P2, v1), B ($P3, v2) =="
    dd if="$HERE/disk_app_v1_signed.bin" of="$P2" bs=4k conv=fsync status=none
    dd if="$HERE/disk_app_v2_signed.bin" of="$P3" bs=4k conv=fsync status=none
    sync
    echo "== done. Set BOOT switch OFF, power-cycle, and watch the CM4 mini-UART console. =="
}

prep_artifacts
# if/else (not an AND-OR list): calling write_device as the left operand of
# '&&' would disable set -e for its whole body, so a failed sgdisk/dd would be
# silently ignored and a half-written eMMC reported as success.
if [ -n "$DEV" ]; then
    write_device
else
    echo "(no device given; artifacts only. Re-run with: sudo bash $0 /dev/sdN)"
fi
