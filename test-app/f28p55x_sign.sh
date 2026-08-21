#!/bin/bash
# f28p55x_sign.sh
#
# Build, sign, and package the wolfBoot C28x test application for the
# TI LAUNCHXL-F28P55X (TMS320F28P550SJ).  This documents the full MVP flow:
# compile the XIP app, extract its firmware words as the host octet stream,
# sign it, and synthesize the octet-per-cell header blob.
#
# Prereqs (override via env):
#   CGT_ROOT   TI C2000 codegen install (dir containing bin/cl2000)
#   C2000WARE  C2000Ware install (default ~/ti/C2000Ware_26_01_00_00)
#   WOLFBOOT   wolfBoot root (default: parent of this script's dir)
#
# Copyright (C) 2026 wolfSSL Inc.  GPLv3 - see project headers.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
WOLFBOOT="${WOLFBOOT:-$(cd "$HERE/.." && pwd)}"
C2000WARE="${C2000WARE:-$HOME/ti/C2000Ware_26_01_00_00}"
: "${CGT_ROOT:?Set CGT_ROOT to a TI C2000 codegen install (dir with bin/cl2000)}"

CL="$CGT_ROOT/bin/cl2000"
HEX="$CGT_ROOT/bin/hex2000"
DEV="$C2000WARE/device_support/f28p55x"
DRV="$C2000WARE/driverlib/f28p55x/driverlib"
OUT="$HERE/out_f28p55x"
KEY="$WOLFBOOT/wolfboot_signing_private_key.der"
SIGN="$WOLFBOOT/tools/keytools/sign"
CONV="$WOLFBOOT/tools/scripts/c2000_flashimg.py"

BOOT_ADDR=0xA0000       # WOLFBOOT_PARTITION_BOOT_ADDRESS
FW_ADDR=0xA0100         # BOOT_ADDR + IMAGE_HEADER_SIZE(256 words)
HDR_SIZE=256

mkdir -p "$OUT"

echo "[1/5] Compile + link the XIP app (codestart at $FW_ADDR)"
"$CL" -v28 --float_support=fpu32 --tmu_support=tmu1 --abi=eabi -O2 \
    --gen_func_subsections=on \
    -D_LAUNCHXL_F28P55X -D_FLASH \
    -I"$CGT_ROOT/include" -I"$DRV" \
    -I"$DEV/common/include" -I"$DEV/headers/include" \
    "$HERE/app_f28p55x.c" \
    "$DEV/common/source/device.c" \
    "$DEV/common/source/f28p55x_codestartbranch.asm" \
    -z --reread_libs --warn_sections \
    -i"$CGT_ROOT/lib" -i"$DRV/ccs/Release" \
    -m "$OUT/app.map" \
    "$HERE/f28p55x_app.cmd" \
    --output_file="$OUT/app.out" \
    -l driverlib.lib -l libc.a

echo "[2/5] Extract the firmware region as a flat little-endian word image"
# hex2000 -> flat binary of the firmware address range.  Each C28x 16-bit word
# is emitted as 2 little-endian host bytes (2 bytes/word), which is exactly the
# octet stream `sign` must hash.  VERIFY these hex2000 options against your
# installed TI utility version; the goal is a raw binary of [FW_ADDR..end).
"$HEX" "$OUT/app.out" -o "$OUT/app_fw_words.bin" \
    --memwidth=16 --romwidth=16 --binary \
    --fill=0xFFFF || {
    echo "hex2000 flat-binary extraction failed - adjust options for your"
    echo "toolchain version (or dump the firmware region another way) so that"
    echo "$OUT/app_fw_words.bin is a raw LE 16-bit-word image of [$FW_ADDR..end)."
    exit 1
}

echo "[3/5] Firmware word image -> host octet stream for signing"
python3 "$CONV" fw2oct "$OUT/app_fw_words.bin" "$OUT/app_fw.oct"

echo "[4/5] Sign the firmware octet stream (ECC P-256 + SHA-256)"
"$SIGN" --ecc256 --sha256 "$OUT/app_fw.oct" "$KEY" 1
# sign writes app_fw_v1_signed.bin next to the input
SIGNED="$OUT/app_fw_v1_signed.bin"

echo "[5/5] Header blob (octet-per-cell) for load address $BOOT_ADDR"
python3 "$CONV" hdr2cells "$SIGNED" "$OUT/header_cells.bin" \
    --header-size "$HDR_SIZE" --addr "$BOOT_ADDR"

cat <<EOF

Done.  Artifacts in $OUT:
  app.out          - native XIP application (flash directly with DSLite)
  header_cells.bin - 256-word signed header blob to program at $BOOT_ADDR

Flash (JTAG detached afterwards; see the ti-c2000-c28x skill):
  DSLite flash -c ccxml/F28P550SJ.ccxml -e -f -v wolfboot.elf
  DSLite flash -c ccxml/F28P550SJ.ccxml    -f -v $OUT/app.out
  # program header_cells.bin at $BOOT_ADDR (UniFlash 'load raw' or a DSS script)

Expected SCIA output (115200 8N1): wolfBoot progress, a verify-OK line, then
"hello from the wolfBoot app on F28P55x".
EOF
