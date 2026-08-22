#!/bin/bash
# Generate assorted REAL filesystem images for tools/fs-test.
#
# Everything here uses mkfs.vfat + mtools and mke2fs -d, so no root, no
# loopback mounts and no sudo. Images are sparse on the host, so the
# directory costs far less than its apparent size.
#
# Each fixture is a whole-disk image with a real partition table, so
# src/disk.c parses an actual MBR or GPT rather than a synthetic one.

set -e
cd "$(dirname "$0")"
OUT=fixtures
mkdir -p "$OUT"

need() {
    command -v "$1" >/dev/null 2>&1 || { echo "SKIP: $1 not installed"; return 1; }
}

# stamp_mbr <disk.img> <part_start_bytes> <part_size_bytes> <ptype>
stamp_mbr() {
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import sys, struct
img, off, size, ptype = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4], 0)
sec = 512
with open(img, 'r+b') as f:
    f.seek(0)
    mbr = bytearray(512)
    e = 0x1BE
    mbr[e + 4] = ptype
    struct.pack_into('<I', mbr, e + 8, off // sec)
    struct.pack_into('<I', mbr, e + 12, size // sec)
    mbr[0x1FE] = 0x55
    mbr[0x1FF] = 0xAA
    f.write(mbr)
PY
}

# stamp_gpt <disk.img> <part_start_bytes> <part_size_bytes> <name>
stamp_gpt() {
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import sys, struct, binascii
img, off, size, name = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
sec = 512
first, last = off // sec, (off + size) // sec - 1
with open(img, 'r+b') as f:
    # protective MBR
    mbr = bytearray(512)
    e = 0x1BE
    mbr[e + 4] = 0xEE
    struct.pack_into('<I', mbr, e + 8, 1)
    struct.pack_into('<I', mbr, e + 12, 0xFFFFFFFF)
    mbr[0x1FE] = 0x55; mbr[0x1FF] = 0xAA
    f.seek(0); f.write(mbr)

    # one partition entry at LBA 2
    ent = bytearray(128)
    ent[0:16]  = bytes(range(1, 17))          # type GUID (non-zero)
    ent[16:32] = bytes(range(17, 33))         # unique GUID
    struct.pack_into('<Q', ent, 32, first)
    struct.pack_into('<Q', ent, 40, last)
    nm = name.encode('utf-16-le')[:72]
    ent[56:56+len(nm)] = nm
    arr = bytes(ent) + bytes(128 * 3)         # 4 entries, 3 empty
    f.seek(2 * sec); f.write(arr)

    hdr = bytearray(92)
    hdr[0:8] = b'EFI PART'
    struct.pack_into('<I', hdr, 8, 0x00010000)
    struct.pack_into('<I', hdr, 12, 92)
    struct.pack_into('<Q', hdr, 24, 1)        # current LBA
    struct.pack_into('<Q', hdr, 72, 2)        # start of array
    struct.pack_into('<I', hdr, 80, 4)        # n_part
    struct.pack_into('<I', hdr, 84, 128)      # array_sz
    struct.pack_into('<I', hdr, 88, binascii.crc32(arr) & 0xFFFFFFFF)
    struct.pack_into('<I', hdr, 16, 0)
    struct.pack_into('<I', hdr, 16, binascii.crc32(bytes(hdr)) & 0xFFFFFFFF)
    f.seek(sec); f.write(bytes(hdr) + bytes(sec - 92))
PY
}

# wrap <fs.img> <disk.img> <mbr|gpt> <ptype-or-name>
wrap() {
    local fs="$1" disk="$2" mode="$3" tag="$4"
    local off=$((1024 * 1024))
    local size
    size=$(stat -c %s "$fs")
    rm -f "$disk"
    truncate -s $((off + size)) "$disk"
    dd if="$fs" of="$disk" bs=1M seek=1 conv=notrunc status=none
    if [ "$mode" = "gpt" ]; then
        stamp_gpt "$disk" "$off" "$size" "$tag"
    else
        stamp_mbr "$disk" "$off" "$size" "$tag"
    fi
    rm -f "$fs"
}

# QUICK=1 skips the two expensive fixtures (a 2.5 GiB volume and a 160 MiB
# payload). Everything else is small, which makes the set usable in CI.
QUICK=${QUICK:-0}

echo "=== payloads ==="
mkdir -p "$OUT/payload"
# A signed-image-shaped payload: a header-sized run of zeros then real data,
# which is exactly the shape that makes mke2fs punch a hole.
{ head -c 1024 /dev/zero; head -c $((1024 * 1024 - 1024)) /dev/urandom; } \
    > "$OUT/payload/os.itb"
if [ "$QUICK" != 1 ]; then
    head -c $((160 * 1024 * 1024)) /dev/urandom > "$OUT/payload/big.itb"
fi
head -c 4096 /dev/urandom > "$OUT/payload/small.itb"

# ----------------------------------------------------------------- FAT32
if need mkfs.vfat && need mcopy; then
    echo "=== fat32: default cluster size, MBR ==="
    truncate -s 96M "$OUT/f1.fs"
    mkfs.vfat -F 32 -n SLOTA "$OUT/f1.fs" >/dev/null
    mmd   -i "$OUT/f1.fs" ::/boot
    mcopy -i "$OUT/f1.fs" "$OUT/payload/os.itb" ::/boot/os.itb
    mcopy -i "$OUT/f1.fs" "$OUT/payload/small.itb" ::/boot/small.itb
    wrap "$OUT/f1.fs" "$OUT/fat32-default.img" mbr 0x0C

    # 32 KiB clusters need >= 65525 of them to be FAT32 at all, so the
    # volume has to be over 2 GiB. The image is sparse, so this costs
    # almost nothing on disk.
    if [ "$QUICK" != 1 ]; then
    echo "=== fat32: 32 KiB clusters (2.5 GiB), long filenames, deep path ==="
    truncate -s 2600M "$OUT/f2.fs"
    mkfs.vfat -F 32 -s 64 -n BIGCLUS "$OUT/f2.fs" >/dev/null
    mmd -i "$OUT/f2.fs" ::/a
    mmd -i "$OUT/f2.fs" ::/a/b
    mmd -i "$OUT/f2.fs" "::/a/b/a-very-long-directory-name"
    mcopy -i "$OUT/f2.fs" "$OUT/payload/os.itb" \
        "::/a/b/a-very-long-directory-name/an-extremely-long-image-name.itb"
    wrap "$OUT/f2.fs" "$OUT/fat32-bigclus-lfn.img" mbr 0x0C
    fi

    echo "=== fat32: fragmented file ==="
    truncate -s 96M "$OUT/f3.fs"
    mkfs.vfat -F 32 -n FRAG "$OUT/f3.fs" >/dev/null
    mmd -i "$OUT/f3.fs" ::/boot
    # Interleave filler with the target, then delete the filler, so the
    # target's clusters are left scattered across the volume.
    for i in 0 1 2 3 4 5 6 7; do
        head -c $((512 * 1024)) /dev/urandom > "$OUT/pad$i"
        mcopy -i "$OUT/f3.fs" "$OUT/pad$i" "::/boot/pad$i.bin"
        head -c $((512 * 1024)) /dev/urandom > "$OUT/frag$i"
        mcopy -i "$OUT/f3.fs" "$OUT/frag$i" "::/boot/frag$i.bin"
    done
    for i in 0 1 2 3 4 5 6 7; do
        mdel -i "$OUT/f3.fs" "::/boot/pad$i.bin"
    done
    mcopy -i "$OUT/f3.fs" "$OUT/payload/os.itb" ::/boot/os.itb
    rm -f "$OUT"/pad? "$OUT"/frag?
    wrap "$OUT/f3.fs" "$OUT/fat32-fragmented.img" mbr 0x0C
fi

# ------------------------------------------------------------------ ext4
if need mke2fs; then
    echo "=== ext4: 4 KiB blocks, stock features, GPT ==="
    mkdir -p "$OUT/root4k/boot"
    cp "$OUT/payload/os.itb" "$OUT/root4k/boot/os.itb"
    cp "$OUT/payload/small.itb" "$OUT/root4k/boot/small.itb"
    truncate -s 128M "$OUT/e1.fs"
    mke2fs -q -t ext4 -b 4096 -L SLOTB -d "$OUT/root4k" -F "$OUT/e1.fs"
    wrap "$OUT/e1.fs" "$OUT/ext4-4k.img" gpt "bootpart"

    echo "=== ext4: 1 KiB blocks (first_data_block == 1) ==="
    mkdir -p "$OUT/root1k/boot"
    cp "$OUT/payload/os.itb" "$OUT/root1k/boot/os.itb"
    truncate -s 64M "$OUT/e2.fs"
    mke2fs -q -t ext4 -b 1024 -L ONEK -d "$OUT/root1k" -F "$OUT/e2.fs"
    wrap "$OUT/e2.fs" "$OUT/ext4-1k.img" mbr 0x83

    echo "=== ext4: deep path + long name ==="
    mkdir -p "$OUT/rootdeep/a/b/c/d/e/f"
    cp "$OUT/payload/os.itb" \
       "$OUT/rootdeep/a/b/c/d/e/f/an-extremely-long-image-name.itb"
    truncate -s 64M "$OUT/e3.fs"
    mke2fs -q -t ext4 -b 4096 -L DEEP -d "$OUT/rootdeep" -F "$OUT/e3.fs"
    wrap "$OUT/e3.fs" "$OUT/ext4-deep.img" mbr 0x83

    if [ "$QUICK" != 1 ]; then
    echo "=== ext4: multi-extent file (>128 MiB forces extent split) ==="
    mkdir -p "$OUT/rootbig/boot"
    cp "$OUT/payload/big.itb" "$OUT/rootbig/boot/os.itb"
    truncate -s 400M "$OUT/e4.fs"
    mke2fs -q -t ext4 -b 4096 -L BIG -d "$OUT/rootbig" -F "$OUT/e4.fs"
    wrap "$OUT/e4.fs" "$OUT/ext4-multiextent.img" mbr 0x83
    fi

    echo "=== ext4: 2 KiB blocks ==="
    mkdir -p "$OUT/root2k/boot"
    cp "$OUT/payload/os.itb" "$OUT/root2k/boot/os.itb"
    truncate -s 64M "$OUT/e5.fs"
    mke2fs -q -t ext4 -b 2048 -L TWOK -d "$OUT/root2k" -F "$OUT/e5.fs"
    wrap "$OUT/e5.fs" "$OUT/ext4-2k.img" mbr 0x83

    rm -rf "$OUT/root4k" "$OUT/root1k" "$OUT/rootdeep" "$OUT/rootbig" \
           "$OUT/root2k"
fi

echo
echo "=== fixtures ==="
ls -lh "$OUT"/*.img 2>/dev/null | awk '{print "  "$9"  "$5}'
echo "(apparent size; images are sparse)"
