#!/bin/bash
# Renode test of CUSTOM_PARTITION_TRAILER on the STM32F411 Black Pill layout:
# one 128KB sector per partition, partition trailers in flash sectors 1-2
# (hal/stm32f4.c), signed image as large as the whole partition.
#
# The update is staged in flash with its trailer set to UPDATING, as the test
# app would leave it. Every boot runs in a fresh Renode instance loaded with
# the flash dumped at the end of the previous one, i.e. a power cycle.
#
# Checks:
#  - the build refuses the same layout without CUSTOM_PARTITION_TRAILER
#  - the sign tool refuses an image one byte larger than the partition
#  - v1 -> v2 (confirmed by the app): v2 boots, and still after a power cycle
#  - v1 -> v3 (not confirmed): v3 boots once, then rolls back to v1
#  - wolfBoot itself is not modified, apart from the trailer sectors
#
# Run from the wolfBoot root, e.g. in the CI container:
#   docker run --rm -v $PWD:/workspace -w /workspace \
#       ghcr.io/wolfssl/wolfboot-ci-renode:v1.5 \
#       tools/scripts/renode-test-custom-trailer.sh
# Warning: replaces .config and the signing keys.
set -u

RENODE=${RENODE:-renode}
WORK=${WORK:-/tmp/renode-custom-trailer}
RUN_TIME=${RUN_TIME:-00:00:10}          # emulated time per boot
CONFIG=config/examples/stm32f411-blackpill.config
OPTS="UART_FLASH=0 IMAGE_HEADER_SIZE=512"

FLASH_BASE=0x08000000
DUMP_SIZE=0x80000
BOOT=0x08020000
UPDATE=0x08040000
PART_SIZE=0x20000
BOOT_TRAILER=0x08007FFB                 # state byte, followed by the magic
UPDATE_TRAILER=0x0800BFFB
TRAILER_START=0x4000                    # wolfboot.bin offsets of the
TRAILER_END=0xC000                      # trailer sectors

RES=0
fail() { echo "FAIL: $*"; RES=1; }

mkdir -p $WORK
rm -f $WORK/*

cp $CONFIG .config || exit 2
make keysclean >/dev/null 2>&1
make keytools >$WORK/build.log 2>&1 || { tail $WORK/build.log; exit 2; }

echo "== Build without CUSTOM_PARTITION_TRAILER must fail"
make clean >/dev/null 2>&1
if make $OPTS CUSTOM_PARTITION_TRAILER=0 wolfboot.bin >$WORK/guard.log 2>&1; then
    fail "single-sector partitions built without CUSTOM_PARTITION_TRAILER"
elif grep -q "WOLFBOOT_PARTITION_SIZE too small" $WORK/guard.log; then
    echo "refused: OK"
else
    tail $WORK/guard.log; fail "build failed for another reason"
fi

echo "== Build with CUSTOM_PARTITION_TRAILER=1"
make clean >/dev/null 2>&1
make $OPTS wolfboot.bin test-app/image.bin >>$WORK/build.log 2>&1 \
    || { tail -20 $WORK/build.log; exit 2; }

sign() { # $1 = payload, $2 = version
    IMAGE_HEADER_SIZE=512 WOLFBOOT_PARTITION_SIZE=$PART_SIZE \
    WOLFBOOT_SECTOR_SIZE=$PART_SIZE CUSTOM_PARTITION_TRAILER=1 \
        tools/keytools/sign --ed25519 --sha256 $1 \
        wolfboot_signing_private_key.der $2 >>$WORK/sign.log 2>&1
}

echo "== Sign images as large as the partition"
cp test-app/image.bin $WORK/big.bin
truncate -s $((PART_SIZE - 512 + 1)) $WORK/big.bin
if sign $WORK/big.bin 1; then
    fail "sign accepted an image larger than the partition"
else
    echo "partition size + 1 refused: OK"
fi
cp test-app/image.bin $WORK/app.bin
truncate -s $((PART_SIZE - 512)) $WORK/app.bin
for v in 1 2 3; do
    sign $WORK/app.bin $v || { tail $WORK/sign.log; exit 2; }
done
[ $(stat -c %s $WORK/app_v1_signed.bin) = $((PART_SIZE)) ] \
    || fail "signed image is not as large as the partition"

# Boot the flash image $1 in a fresh Renode, dump the flash to $2 when the
# emulated time is over, print the first version reported on the UART.
power_cycle() {
    rm -f $WORK/uart.bin $2
    cat > $WORK/boot.resc <<EOF
mach create "f4"
machine LoadPlatformDescription @$PWD/tools/renode/stm32f4_flash_alias.repl
sysbus.usart1 CreateFileBackend @$WORK/uart.bin true
sysbus LoadBinary @$1 $FLASH_BASE
emulation RunFor "$RUN_TIME"
python "import System; System.IO.File.WriteAllBytes('$2', monitor.Machine.SystemBus.ReadBytes($FLASH_BASE, $DUMP_SIZE))"
quit
EOF
    timeout 300 $RENODE --disable-xwt --console -e "include @$WORK/boot.resc" \
        </dev/null >>$WORK/renode.log 2>&1
    # the test app reports '*' and its version, big endian
    python3 -I - $WORK/uart.bin <<'EOF'
import sys
try:
    d = open(sys.argv[1], 'rb').read()
except OSError:
    sys.exit(0)
i = d.find(b'*')
while 0 <= i <= len(d) - 5:
    if d[i+1:i+4] == b'\0\0\0':
        print(d[i+4])
        break
    i = d.find(b'*', i + 1)
EOF
}

byte_at() { od -An -tx1 -j $(($2 - FLASH_BASE)) -N1 $1 | tr -d ' '; }
version_at() { od -An -tu4 -j $(($2 - FLASH_BASE + 12)) -N4 $1 | tr -d ' '; }

# $1 = update version, $2 = expected version at first boot, $3 = after the
# power cycle, $4 = expected header versions in boot and update partitions
scenario() {
    local F=$WORK/factory-v$1.bin A=$WORK/flash-v$1-a.bin B=$WORK/flash-v$1-b.bin V N
    echo "== Update v1 -> v$1"
    tools/bin-assemble/bin-assemble $F $FLASH_BASE wolfboot.bin \
        $BOOT $WORK/app_v1_signed.bin $UPDATE $WORK/app_v$1_signed.bin \
        >/dev/null 2>&1 || exit 2
    printf '\x70BOOT' | dd of=$F bs=1 seek=$((UPDATE_TRAILER - FLASH_BASE)) \
        conv=notrunc 2>/dev/null
    V=$(power_cycle $F $A)
    echo "boot 1: version '$V' (expect $2)"
    [ "$V" = "$2" ] || fail "v$1 boot 1"
    [ -s $A ] || { fail "no flash dump"; return; }
    V=$(power_cycle $A $B)
    echo "boot 2, after power cycle: version '$V' (expect $3)"
    [ "$V" = "$3" ] || fail "v$1 boot 2"
    [ -s $B ] || { fail "no flash dump"; return; }
    N=$(cmp -l -n $(stat -c %s wolfboot.bin) wolfboot.bin $B | \
        awk -v s=$((TRAILER_START)) -v e=$((TRAILER_END)) \
        '{ a = $1 - 1; if (a < s || a >= e) n++ } END { print n + 0 }')
    [ "$N" = 0 ] && echo "wolfBoot intact" || fail "wolfBoot modified: $N bytes"
    V="$(version_at $B $BOOT)/$(version_at $B $UPDATE)"
    echo "partitions: v$V (expect $4)"
    [ "$V" = "$4" ] || fail "v$1 partition contents"
    V=$(byte_at $B $BOOT_TRAILER)
    echo "boot state: 0x$V (expect 0x00, SUCCESS)"
    [ "$V" = 00 ] || fail "v$1 boot state"
}

scenario 2 2 2 2/1
scenario 3 3 1 1/3

if [ $RES = 0 ]; then echo "custom partition trailer: PASS"; else
    echo "custom partition trailer: FAIL"; echo "----- Renode log -----"
    tail -n 40 $WORK/renode.log
fi
exit $RES
