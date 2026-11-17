#!/bin/bash
#
# mk_board_dtb.sh - Make the Linux DTB for the VEK385 board from AMD's
# VEK385 Rev B Linux DTB (cortexa78-linux.dtb in AMD's
# soc-prebuilt-firmware, the same file as the system.dtb in the board
# BIF). It goes in the Linux FIT.
#
# Changes, nothing else:
#   1. /chosen/bootargs: console on UART1 (the UART wolfBoot uses) and
#      rdinit=/init (the FIT ramdisk is the root filesystem).
#   2. Memory: low DDR only (0 - 2 GB). The high DDR regions and the CMA
#      pools in them are removed (high DDR is not supported yet).
#   3. reserved-memory: BL31 at 0x1600000 (2 MB, MEM_BASE/MEM_SIZE of the
#      board TF-A build). AMD's DTB reserves only 0x1000000 - 0x1600000.
#   4. OSPI partitions: wolfBoot BOOT / UPDATE / SWAP, as in
#      versal2_vek385_linux.config, in place of AMD's layout.
#
# Usage:
#   ./mk_board_dtb.sh <AMD VEK385 DTB> <out DTB>
#
set -euo pipefail
IN="${1:?usage: mk_board_dtb.sh <AMD VEK385 DTB> <out DTB>}"
OUT="${2:?usage: mk_board_dtb.sh <AMD VEK385 DTB> <out DTB>}"
OSPI=/axi/spi@f1010000
PARTS=$OSPI/flash@0/partitions

for t in fdtput fdtget dtc; do
    command -v $t >/dev/null || { echo "$t not found"; exit 2; }
done
[ -s "$IN" ] || { echo "input DTB missing: $IN"; exit 3; }
MODEL="$(fdtget -t s "$IN" / model)"
[ "$MODEL" = "AMD Versal VEK385 revB" ] \
    || { echo "not the VEK385 Rev B DTB: $MODEL"; exit 3; }

cp "$IN" "$OUT"

# 1. Command line
fdtput -t s "$OUT" /chosen bootargs \
    "earlycon=pl011,mmio32,0xf1930000 console=ttyAMA1,115200 rdinit=/init"

# 2. Low DDR only
fdtput -t x "$OUT" /memory@00000000 reg 0 0 0 80000000
fdtput -r "$OUT" /memory@50000000000
fdtput -r "$OUT" /memory@60000000000
fdtput -r "$OUT" /reserved-memory/cma_reserved@800000000
fdtput -r "$OUT" /reserved-memory/cma_reserved@50000000000
fdtput -r "$OUT" /reserved-memory/cma_reserved@60000000000

# 3. BL31
fdtput -c "$OUT" /reserved-memory/bl31@1600000
fdtput -t x "$OUT" /reserved-memory/bl31@1600000 reg 0 1600000 0 200000
fdtput "$OUT" /reserved-memory/bl31@1600000 no-map

# 4. OSPI partitions
fdtput -r "$OUT" $PARTS
fdtput -c "$OUT" $PARTS
fdtput -t s "$OUT" $PARTS compatible "fixed-partitions"
fdtput -t x "$OUT" $PARTS "#address-cells" 1
fdtput -t x "$OUT" $PARTS "#size-cells" 1
add_part() {
    fdtput -c "$OUT" "$PARTS/partition@$1"
    fdtput -t x "$OUT" "$PARTS/partition@$1" reg "$1" "$2"
    fdtput -t s "$OUT" "$PARTS/partition@$1" label "$3"
}
# fdtput -c puts a new node first, so add them last to first.
add_part fa40000 20000 swap
add_part 49c0000 2c00000 update
add_part 1dc0000 2c00000 boot

# Check
dtc -I dtb -O dts -q "$OUT" > /dev/null || { echo "bad DTB"; exit 4; }
[ "$(fdtget -t x "$OUT" /memory@00000000 reg)" = "0 0 0 80000000" ] \
    || { echo "memory reg wrong"; exit 4; }
[ "$(fdtget -t x "$OUT" /reserved-memory/bl31@1600000 reg)" \
    = "0 1600000 0 200000" ] || { echo "bl31 reg wrong"; exit 4; }
[ "$(fdtget -l "$OUT" $PARTS | tr '\n' ' ')" \
    = "partition@1dc0000 partition@49c0000 partition@fa40000 " ] \
    || { echo "partitions wrong"; exit 4; }

echo "board DTB: $OUT ($(stat -c %s "$OUT") bytes)"
echo "BOARD-DTB-OK"
