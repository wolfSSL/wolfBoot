#!/bin/bash
# Benchmark the read-only FAT32 / ext4 layer across the generated fixtures.
#
# The number that matters for boot time is not host throughput -- the host's
# page cache makes that meaningless -- but the COUNT of media reads needed to
# deliver the payload. On an SD target each read costs a command round trip,
# so read count is what dominates. A raw partition needs one read for the
# whole payload; anything above that is the filesystem's overhead.

cd "$(dirname "$0")"
F=fixtures
ITERS=${ITERS:-25}

row() {
    local img="$1" part="$2" path="$3" label="$4"
    local out
    [ -f "$F/$img" ] || return
    out=$(./fs-test bench "$F/$img" "$part" "$path" "$ITERS" 2>/dev/null)
    [ -n "$out" ] || { printf "  %-24s (failed)\n" "$label"; return; }
    local size opent readt openr readr thr
    size=$(echo   "$out" | awk '/^size /{print $3}')
    opent=$(echo  "$out" | awk '/^open  time/{print $4}')
    readt=$(echo  "$out" | awk '/^read  time/{print $4}')
    openr=$(echo  "$out" | awk '/^open  disk reads/{print $4}')
    readr=$(echo  "$out" | awk '/^read  disk reads/{print $4}')
    thr=$(echo    "$out" | awk '/^throughput/{print $3}')
    printf "  %-24s %10s %8s %8s %9s %9s %9s\n" \
        "$label" "$size" "$opent" "$readt" "$openr" "$readr" "$thr"
}

echo "iterations: $ITERS   (cache = ${CACHE:-512} bytes)"
echo
printf "  %-24s %10s %8s %8s %9s %9s %9s\n" \
    "fixture" "bytes" "open/ms" "read/ms" "open rds" "read rds" "MiB/s"
printf "  %-24s %10s %8s %8s %9s %9s %9s\n" \
    "------------------------" "----------" "--------" "--------" \
    "---------" "---------" "---------"

row fat32-default.img     0 /boot/small.itb "fat32 4 KiB file"
row fat32-default.img     0 /boot/os.itb    "fat32 1 MiB"
row fat32-bigclus-lfn.img 0 "/a/b/a-very-long-directory-name/an-extremely-long-image-name.itb" "fat32 1 MiB 32K clus"
row fat32-fragmented.img  0 /boot/os.itb    "fat32 1 MiB fragmented"
row ext4-4k.img           0 /boot/small.itb "ext4 4 KiB file"
row ext4-4k.img           0 /boot/os.itb    "ext4 1 MiB (4K blk)"
row ext4-1k.img           0 /boot/os.itb    "ext4 1 MiB (1K blk)"
row ext4-2k.img           0 /boot/os.itb    "ext4 1 MiB (2K blk)"
row ext4-deep.img         0 "/a/b/c/d/e/f/an-extremely-long-image-name.itb" "ext4 1 MiB deep path"
row ext4-multiextent.img  0 /boot/os.itb    "ext4 160 MiB multi-ext"
