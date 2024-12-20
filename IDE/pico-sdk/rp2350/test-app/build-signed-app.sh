#!/bin/bash

mkdir -p build
cd build
cmake .. -DPICO_SDK_PATH=$PICO_SDK_PATH -DPICO_PLATFORM=rp2350

# Get off-tree source file from raspberry pico-examples
curl -o blink.c https://raw.githubusercontent.com/raspberrypi/pico-examples/refs/tags/sdk-2.1.0/blink/blink.c

make clean && make

IMAGE_HEADER_SIZE=1024 ../../../../../tools/keytools/sign --sha256 --ecc256 blink.bin \
    ../../../../../wolfboot_signing_private_key.der 1

cd ..

JLinkExe -Device RP2350_M33_0 -If swd -Speed 4000 -CommanderScript flash_app.jlink
