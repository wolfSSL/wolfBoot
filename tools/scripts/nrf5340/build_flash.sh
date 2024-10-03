#!/bin/bash

# nRF5340 dual core: Creates internal and external flash images for testing
# Signs each with a new version 2 and places into external flash,
# with flag to trigger update (like calling wolfBoot_update_trigger)

# run from wolfBoot root
# ./tools/scripts/nrf5340/build_flash.sh

# optionally run with "--erase" argument to rease both internal and external flash
# example: ./tools/scripts/nrf5340/build_flash.sh --debug --erase --program

# Defaults
MAKE_ARGS=
DO_BUILD=0
DO_BUILD_DEBUG=0
DO_ERASE=0
DO_PROGRAM=0
if [[ $# -eq 0 ]] ; then
  DO_BUILD=1
  DO_BUILD_DEBUG=0
  DO_ERASE=1
  DO_PROGRAM=1
  echo "Build release with symbols, erase and program"
fi

while test $# -gt 0; do
  case "$1" in
    -h|--help|-?)
      echo "nRF5340 build / flash script"
      echo " "
      echo "default: build, erase and program"
      echo " "
      echo "options:"
      echo "-h, --help      show brief help"
      echo "-b, --build     build release with symbols"
      echo "-d, --debug     build debug"
      echo "-v, --verbose   build verbose"
      echo "-e, --erase     do erase of internal/external flash"
      echo "-p, --program   program images built"
      exit 0
      ;;
    -b|--build)
      DO_BUILD=1
      MAKE_ARGS+=" DEBUG_SYMBOLS=1"
      echo "Build release with symbols"
      shift
      ;;
    -d|--debug)
      DO_BUILD=1
      MAKE_ARGS+=" DEBUG=1"
      echo "Build with debug"
      shift
      ;;
    -v|--verbose)
      DO_BUILD=1
      MAKE_ARGS+=" V=1"
      echo "Build with verbose output"
      shift
      ;;
    -e|--erase)
      DO_ERASE=1
      echo "Do erase"
      shift
      ;;
    -p|--program)
      DO_PROGRAM=1
      echo "Do program"
      shift
      ;;
    *)
      break
      ;;
  esac
done

if [[ $DO_BUILD == 1 ]]; then
  rm -f ./tools/scripts/nrf5340/*.bin
  rm -f ./tools/scripts/nrf5340/*.hex

  # Build internal flash images for both cores

  # Build net
  cp config/examples/nrf5340_net.config .config
  make clean
  make $MAKE_ARGS
  cp factory.bin tools/scripts/nrf5340/factory_net.bin
  # Sign flash update for testing (use partition type 2 for network update)
  tools/keytools/sign --ecc384 --sha384 --id 2 test-app/image.bin wolfboot_signing_private_key.der 2
  cp test-app/image_v2_signed.bin tools/scripts/nrf5340/image_v2_signed_net.bin

  # Build app
  cp config/examples/nrf5340.config .config
  make clean
  make $MAKE_ARGS

  cp factory.bin tools/scripts/nrf5340/factory_app.bin
  # Sign flash update for testing
  tools/keytools/sign --ecc384 --sha384 test-app/image.bin wolfboot_signing_private_key.der 2
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
fi

if [[ $DO_ERASE == 1 ]]; then
    nrfjprog -f nrf53 --recover
    nrfjprog -f nrf53 --qspieraseall
fi

if [[ $DO_PROGRAM == 1 ]]; then
  # Program external flash
  nrfjprog -f nrf53 --program tools/scripts/nrf5340/update_app_v2.hex --verify
  nrfjprog -f nrf53 --program tools/scripts/nrf5340/image_v2_signed_net.hex --verify


  # Program Internal Flash
  #nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_app.hex --verify
  #nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_net.hex --verify --coprocessor CP_NETWORK
  JLinkExe -CommandFile tools/scripts/nrf5340/flash_net.jlink
  JLinkExe -CommandFile tools/scripts/nrf5340/flash_app.jlink
fi
