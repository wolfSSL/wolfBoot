#!/bin/bash

# nRF5340 dual core: Creates internal and external flash images for testing

# run from wolfBoot root
# ./tools/scripts/nrf5340/build_flash.sh
# optionally run with "erase" argument to rease both internal and external flash
# or provide make arguments "DEBUG=1"


if [ "$1" == "erase" ]; then
  DO_ERASE=1
  MAKE_ARGS="$2"
else
  DO_ERASE=0
  MAKE_ARGS="$1"
fi

rm -f ./tools/scripts/nrf5340/*.bin
rm -f ./tools/scripts/nrf5340/*.hex

# Build internal flash images for both cores

# Build net
cp config/examples/nrf5340_net.config .config
make clean
make $MAKE_ARGS
cp factory.bin tools/scripts/nrf5340/factory_net.bin
# Sign flash update for testing (use partition type 2 for network update)
tools/keytools/sign --ecc256 --id 2 test-app/image.bin wolfboot_signing_private_key.der 2
cp test-app/image_v2_signed.bin tools/scripts/nrf5340/image_v2_signed_net.bin

# Build app
cp config/examples/nrf5340.config .config
make clean
make $MAKE_ARGS

cp factory.bin tools/scripts/nrf5340/factory_app.bin
# Sign flash update for testing
tools/keytools/sign --ecc256 test-app/image.bin wolfboot_signing_private_key.der 2
cp test-app/image_v2_signed.bin tools/scripts/nrf5340/image_v2_signed_app.bin

# Create a bin footer with wolfBoot trailer "BOOT" and "p" (ASCII for 0x70 == IMG_STATE_UPDATING):
echo -n "pBOOT" > tools/scripts/nrf5340/trigger_magic.bin
./tools/bin-assemble/bin-assemble \
  tools/scripts/nrf5340/update_app_v2.bin \
    0x0     tools/scripts/nrf5340/image_v2_signed_app.bin \
    0xEDFFB tools/scripts/nrf5340/trigger_magic.bin


# Convert to HEX format for programmer tool
arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x00000000 tools/scripts/nrf5340/factory_app.bin tools/scripts/nrf5340/factory_app.hex
arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x01000000 tools/scripts/nrf5340/factory_net.bin tools/scripts/nrf5340/factory_net.hex

arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x10000000 tools/scripts/nrf5340/update_app_v2.bin tools/scripts/nrf5340/update_app_v2.hex
arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x10100000 tools/scripts/nrf5340/image_v2_signed_net.bin tools/scripts/nrf5340/image_v2_signed_net.hex


if [ "$DO_ERASE" == "1" ]; then
    nrfjprog -f nrf53 --recover
    nrfjprog -f nrf53 --qspieraseall
fi

# Program external flash
nrfjprog -f nrf53 --program tools/scripts/nrf5340/update_app_v2.hex --verify
nrfjprog -f nrf53 --program tools/scripts/nrf5340/image_v2_signed_net.hex --verify


# Program Internal Flash
#nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_app.hex --verify
#nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_net.hex --verify --coprocessor CP_NETWORK
JLinkExe -CommandFile tools/scripts/nrf5340/flash_net.jlink
JLinkExe -CommandFile tools/scripts/nrf5340/flash_app.jlink
