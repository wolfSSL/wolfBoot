#!/usr/bin/env bash
#
# imx8qm-flash.sh - write an i.MX 8QuadMax boot container to the MEK.
#
# Two routes:
#
#   sdp   Serial Download Protocol. Set SW2 [D1-D6] to 000100, connect the J17
#         USB port, and load the container straight into RAM over USB with
#         uuu. Nothing is written to the board, so this is the safe way to try
#         a build. Power-cycle to go back to the previous image.
#
#   sd    Write the container to a microSD card in the host's reader. The boot
#         ROM reads it from a 32 KB offset. Set SW2 to 001100 (SD1) to boot it.
#
# eMMC and FlexSPI are written from a running system (or from a U-Boot already
# on the board) rather than from here: overwriting the on-board boot device is
# how a board stops booting, and doing it needs a recovery path already in
# place. See docs/Targets.md.
#
# Usage:
#   tools/scripts/imx8qm/imx8qm-flash.sh sdp [flash.bin]
#   tools/scripts/imx8qm/imx8qm-flash.sh sd /dev/sdX [flash.bin]
set -e

MODE="${1:-}"

case "$MODE" in
sdp)
    IMAGE="${2:-flash.bin}"
    [ -f "$IMAGE" ] || { echo "ERROR: no such image: $IMAGE" >&2; exit 1; }
    command -v uuu >/dev/null || { echo "ERROR: uuu not found in PATH" >&2; exit 1; }
    echo "Set SW2 [D1-D6] to 000100 (SDP) and power-cycle the board, then:"
    uuu -b sdps "$IMAGE"
    ;;

sd)
    DEV="${2:-}"
    IMAGE="${3:-flash.bin}"
    [ -n "$DEV" ] || { echo "ERROR: no device given" >&2; exit 1; }
    [ -b "$DEV" ] || { echo "ERROR: not a block device: $DEV" >&2; exit 1; }
    [ -f "$IMAGE" ] || { echo "ERROR: no such image: $IMAGE" >&2; exit 1; }

    # Refuse anything that is not removable. A typo here overwrites the host's
    # own disk, and the boot container is written to raw sectors with no
    # filesystem in the way to make that fail safely.
    #
    # Resolve the whole-disk name with lsblk rather than stripping trailing
    # digits: a digit-strip turns mmcblk0 into "mmcblk" and nvme0n1 into "nvme",
    # neither of which exists under /sys/block, so a built-in card reader would
    # be refused even though it is removable. PKNAME is empty when $DEV is
    # already a whole disk, so fall back to its own basename.
    base=$(lsblk -ndo PKNAME "$DEV" 2>/dev/null | head -1)
    [ -n "$base" ] || base=$(basename "$DEV")
    if [ "$(cat "/sys/block/$base/removable" 2>/dev/null)" != "1" ]; then
        echo "ERROR: $DEV is not a removable device; refusing to write." >&2
        exit 1
    fi
    echo "About to write $IMAGE to $DEV at a 32 KB offset."
    lsblk -o NAME,SIZE,MODEL,TRAN "$DEV" || true
    printf 'Type YES to continue: '
    read -r reply
    [ "$reply" = "YES" ] || { echo "aborted"; exit 1; }

    # seek=32 with bs=1k puts the container at the 32 KB offset the boot ROM
    # reads from on SD/eMMC. conv=fsync so the write is on the card before
    # this returns.
    sudo dd if="$IMAGE" of="$DEV" bs=1k seek=32 conv=fsync status=progress
    sync
    echo "Done. Set SW2 [D1-D6] to 001100 (SD1) to boot it."
    ;;

*)
    sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
