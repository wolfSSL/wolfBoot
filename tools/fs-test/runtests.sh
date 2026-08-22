#!/bin/bash
# Verify every generated fixture: mount it, read the file through the real
# parsers, and byte-compare against the payload that was written into it.
#
# A pass means the bootloader's own code recovered the exact bytes that mkfs
# put on the volume.

cd "$(dirname "$0")"
F=fixtures
pass=0
fail=0

check() {
    local img="$1" part="$2" path="$3" ref="$4"
    if [ ! -f "$F/$img" ]; then
        printf "  %-26s SKIP (not generated)\n" "$img"
        return
    fi
    printf "  %-26s " "$img"
    if ./fs-test verify "$F/$img" "$part" "$path" "$F/$ref" >/dev/null 2>&1; then
        echo "PASS"
        pass=$((pass + 1))
    else
        echo "FAIL"
        ./fs-test verify "$F/$img" "$part" "$path" "$F/$ref" 2>&1 | sed 's/^/      /'
        fail=$((fail + 1))
    fi
}

echo "=== read-back verification ==="
check fat32-default.img      0 /boot/os.itb   payload/os.itb
check fat32-default.img      0 /boot/small.itb payload/small.itb
check fat32-bigclus-lfn.img  0 "/a/b/a-very-long-directory-name/an-extremely-long-image-name.itb" payload/os.itb
check fat32-fragmented.img   0 /boot/os.itb   payload/os.itb
check ext4-4k.img            0 /boot/os.itb   payload/os.itb
check ext4-4k.img            0 /boot/small.itb payload/small.itb
check ext4-1k.img            0 /boot/os.itb   payload/os.itb
check ext4-2k.img            0 /boot/os.itb   payload/os.itb
check ext4-deep.img          0 "/a/b/c/d/e/f/an-extremely-long-image-name.itb" payload/os.itb
check ext4-multiextent.img   0 /boot/os.itb   payload/big.itb

echo
echo "=== negative cases (each must be refused, not misread) ==="
neg() {
    local img="$1" part="$2" path="$3" what="$4"
    [ -f "$F/$img" ] || return
    printf "  %-26s %-22s " "$img" "$what"
    if ./fs-test cat "$F/$img" "$part" "$path" /dev/null >/dev/null 2>&1; then
        echo "FAIL (accepted)"
        fail=$((fail + 1))
    else
        echo "PASS (refused)"
        pass=$((pass + 1))
    fi
}
neg fat32-default.img 0 /boot/nosuch.itb   "missing file"
neg fat32-default.img 0 /boot              "directory as file"
neg fat32-default.img 0 "/boot/../boot/os.itb" "dotdot traversal"
neg ext4-4k.img       0 /boot/nosuch.itb   "missing file"
neg ext4-4k.img       0 /boot              "directory as file"
neg ext4-4k.img       0 "/a/b/c/d/e/f/g/h/i/j/k/l.itb" "path too deep"

# --- big-endian ------------------------------------------------------------
# Everything on disk is little-endian: the MBR table, the GPT header and
# entries, and every FAT32/ext4 field. All of it is read byte-wise, so the
# whole stack should be endian-neutral. Prove it by running the same
# fixtures as big-endian PowerPC under qemu-user.
if command -v powerpc-linux-gnu-gcc >/dev/null 2>&1 && \
   command -v qemu-ppc-static >/dev/null 2>&1; then
    echo
    echo "=== big-endian (PowerPC), full stack incl. MBR/GPT ==="
    # Do NOT hide this build. A cross-compile failure here (a missing
    # libc6-dev-powerpc-cross, say) would otherwise surface as every
    # big-endian case failing, which points at the parsers instead of at
    # the toolchain.
    if ! powerpc-linux-gnu-gcc -static -o fs-test-be fs-test.c \
            -I. -I../../src -I../../include -Wall -Wextra -O2 \
            -D_FILE_OFFSET_BITS=64 -DWOLFBOOT_DISK_FS -DWOLFBOOT_FAT32 \
            -DWOLFBOOT_EXT4 -DUNIT_TEST -DWOLFSSL_USER_SETTINGS; then
        echo "  ERROR: big-endian cross-build failed (toolchain problem," >&2
        echo "  not a parser problem). Need gcc-powerpc-linux-gnu and" >&2
        echo "  libc6-dev-powerpc-cross for -static." >&2
        fail=$((fail + 1))
    else
    be() {
        local img="$1" path="$2"
        [ -f "$F/$img" ] || return
        printf "  %-26s " "$img"
        if qemu-ppc-static ./fs-test-be verify "$F/$img" 0 "$path" \
                "$F/payload/os.itb" >/dev/null 2>&1; then
            echo "PASS"; pass=$((pass + 1))
        else
            echo "FAIL"; fail=$((fail + 1))
        fi
    }
    be fat32-default.img      /boot/os.itb
    be fat32-bigclus-lfn.img  "/a/b/a-very-long-directory-name/an-extremely-long-image-name.itb"
    be ext4-4k.img            /boot/os.itb
    be ext4-1k.img            /boot/os.itb
    be ext4-2k.img            /boot/os.itb
    be ext4-deep.img          "/a/b/c/d/e/f/an-extremely-long-image-name.itb"
    fi
else
    echo
    echo "  (big-endian check skipped: needs powerpc-linux-gnu-gcc + qemu-ppc-static)"
fi

echo
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
