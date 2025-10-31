#!/bin/bash
set -e

make clean
make wolfboot.bin
make test-app/image.bin

IMAGE_HEADER_SIZE=512 ./tools/keytools/sign --ecc384 --sha384 test-app/image.bin wolfboot_signing_private_key.der 1

dd if=/dev/zero of=blank_update.bin bs=1K count=97

./tools/bin-assemble/bin-assemble \
    factory.bin \
        0x0     wolfboot.bin \
        0xFC00  test-app/image_v1_signed.bin \
        0x27C00 blank_update.bin

JLinkExe -CommanderScript tools/scripts/va416x0/flash_va416xx.jlink