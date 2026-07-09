#!/bin/bash
# Build gnu-efi for AArch64 (for the wolfBoot aarch64_efi UEFI target).
# Installs crt0-efi-aarch64.o, elf_aarch64_efi.lds, libgnuefi.a, libefi.a
# and the EFI headers into tools/gnu-efi-aarch64/ inside the wolfBoot tree.
#
# GNU_EFI_REF is pinned to a released tag so the runtime CRT0/linker-script/libs
# are reproducible and auditable (the aarch64_efi link depends on the exact
# revision -- see the --allow-multiple-definition note in arch.mk). Override:
#   CROSS_COMPILE=... GNU_EFI_REF=... ./build-gnu-efi-aarch64.sh
set -e

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
GNU_EFI_REF="${GNU_EFI_REF:-4.0.4}"

# Resolve wolfBoot root (this script lives in tools/scripts/)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PREFIX="$ROOT/tools/gnu-efi-aarch64"
SRC="$ROOT/tools/gnu-efi-src"

echo "== gnu-efi aarch64 build =="
echo "   CROSS_COMPILE=$CROSS_COMPILE"
echo "   PREFIX=$PREFIX"

# gnu-efi upstream (Nigel Croxon, the gnu-efi maintainer). Pin to $GNU_EFI_REF;
# if the tree already exists, force it onto that ref (reproducible re-runs).
if [ ! -d "$SRC/.git" ]; then
    git clone --depth 1 -b "$GNU_EFI_REF" https://github.com/ncroxon/gnu-efi "$SRC"
else
    git -C "$SRC" fetch --depth 1 origin tag "$GNU_EFI_REF" 2>/dev/null \
        || git -C "$SRC" fetch --depth 1 origin "$GNU_EFI_REF"
    git -C "$SRC" checkout -q "$GNU_EFI_REF" 2>/dev/null \
        || git -C "$SRC" checkout -q FETCH_HEAD
    make -C "$SRC" ARCH=aarch64 clean >/dev/null 2>&1 || true
fi

make -C "$SRC" ARCH=aarch64 CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"
make -C "$SRC" ARCH=aarch64 CROSS_COMPILE="$CROSS_COMPILE" \
    PREFIX=/ INSTALLROOT="$PREFIX" install

echo "== installed files =="
find "$PREFIX" -name 'crt0-efi-aarch64.o' -o -name 'elf_aarch64_efi.lds' \
    -o -name 'libgnuefi.a' -o -name 'libefi.a' | sort
echo "== done =="
