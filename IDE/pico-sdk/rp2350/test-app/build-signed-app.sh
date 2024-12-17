#!/bin/bash

mkdir -p build
cd build
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_PLATFORM=rp2350
cat pico_flash_region.ld | sed -e "s/0x10000000/0x10040400/g" >pico_flash_region_wolfboot.ld
cp pico_flash_region_wolfboot.ld pico_flash_region.ld

# Get off-tree source file from raspberry pico-examples
curl -o blink.c https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/tags/sdk-2.1.0/blink/blink.c

make clean && make

IMAGE_HEADER_SIZE=1024 ../../../../../tools/keytools/sign --sha256 --ecc256 blink.bin \
    ../../../../../wolfboot_signing_private_key.der 1

cd ..

JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript flash_app.jlink
