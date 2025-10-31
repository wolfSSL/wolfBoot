#!/bin/bash
set -e

make clean
make
IMAGE_HEADER_SIZE=512 ./tools/keytools/sign --ecc384 --sha384 test-app/image.bin wolfboot_signing_private_key.der 2
echo -n "pBOOT" > trigger_magic.bin

./tools/bin-assemble/bin-assemble \
    update.bin \
        0x0     wolfboot.bin \
        0xFC00  test-app/image_v1_signed.bin \
        0x27C00 test-app/image_v2_signed.bin \
        0x3FBFB trigger_magic.bin

JLinkExe -CommanderScript tools/scripts/va416x0/flash_va416xx_update.jlink