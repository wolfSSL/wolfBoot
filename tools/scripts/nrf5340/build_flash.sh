#!/bin/bash

# nRF5340 dual core: Creates internal and external flash images for testing
# Signs each with a new version 2 and places into external flash,
# with flag to trigger update (like calling wolfBoot_update_trigger)

# run from wolfBoot root
# Example use:
# Clean build and program factory version 1
# ./tools/scripts/nrf5340/build_flash.sh

# Build full update version 2 and flash to external (also reprograms internal flash)
# ./tools/scripts/nrf5340/build_flash.sh --update

# Build dela update version 3 and flash to external (also reprograms internal flash)
# ./tools/scripts/nrf5340/build_flash.sh --delta

# Defaults
MAKE_ARGS=" DEBUG_SYMBOLS=1"
DO_CLEAN=0
DO_BUILD=0
DO_UPDATE=0
DO_BUILD_DEBUG=0
DO_ERASE_INT=0
DO_ERASE_EXT=0
DO_PROGRAM_INT=0
DO_PROGRAM_EXT=0
DO_DELTA=0
UPDATE_VERSION=1

SIGN_ARGS="--ecc384 --sha384"
#SIGN_ARGS="--ecc256 --sha256"

# Default with no arguments
if [[ $# -eq 0 ]] ; then
  DO_CLEAN=1
  DO_BUILD=1
  DO_ERASE_INT=1
  DO_PROGRAM_INT=1
  echo "Clean build release, erase and program (internal flash only)"
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
      echo "-c, --clean     cleanup build artifacts"
      echo "-b, --build     build release with symbols"
      echo "-d, --debug     build debug"
      echo "-v, --verbose   build verbose"
      echo "--version       use custom version"
      echo "-e, --erase     do erase of internal/external flash"
      echo "-p, --program   program images built"
      echo "-u, --update    build update, sign and program external flash"
      echo "-t, --delta     build update, sign delta and program external flash"
      exit 0
      ;;
    -c|--clean)
      DO_CLEAN=1
      echo "Cleaning build artifacts"
      shift
      ;;
    -b|--build)
      DO_BUILD=1
      echo "Build release with symbols"
      shift
      ;;
    -d|--debug)
      DO_BUILD=1
      MAKE_ARGS+=" DEBUG=1"
      echo "Add debug option"
      shift
      ;;
    -v|--verbose)
      DO_BUILD=1
      MAKE_ARGS+=" V=1"
      echo "Add verbose output"
      shift
      ;;
    -e|--erase)
      DO_ERASE_INT=1
      DO_ERASE_EXT=1
      echo "Do erase"
      shift
      ;;
    -p|--program)
      DO_PROGRAM_INT=1
      DO_PROGRAM_EXT=1
      echo "Do program"
      shift
      ;;
    --version)
      UPDATE_VERSION="$2"
      echo "Use version ${UPDATE_VERSION}"
      shift
      shift
      ;;
    -u|--update)
      DO_UPDATE=1
      UPDATE_VERSION=2
      DO_BUILD=1
      DO_ERASE_INT=1
      DO_ERASE_EXT=1
      DO_PROGRAM_INT=1
      DO_PROGRAM_EXT=1
      echo "Do update build and program"
      shift
      ;;
    -t|--delta)
      DO_DELTA=1
      UPDATE_VERSION=3
      DO_BUILD=1
      DO_UPDATE=0
      DO_CLEAN=0
      DO_ERASE_INT=1
      DO_ERASE_EXT=1
      DO_PROGRAM_INT=1
      DO_PROGRAM_EXT=1
      echo "Do delta build and program"
      shift
      ;;
    *)
      break
      ;;
  esac
done

if [[ $DO_CLEAN == 1 ]]; then
  rm -f ./tools/scripts/nrf5340/*.bin
  rm -f ./tools/scripts/nrf5340/*.hex
fi

if [[ $DO_BUILD == 1 ]]; then
  # Build internal flash images for both cores

  # Build net
  cp config/examples/nrf5340_net.config .config
  make clean
  make $MAKE_ARGS
  cp test-app/image.bin tools/scripts/nrf5340/image_net.bin
  if [ ! -f tools/scripts/nrf5340/factory_net.bin ]; then
    cp test-app/image_v1_signed.bin tools/scripts/nrf5340/image_net_v1_signed.bin
    cp factory.bin tools/scripts/nrf5340/factory_net.bin
  fi

  # Build app
  cp config/examples/nrf5340.config .config
  make clean
  make $MAKE_ARGS
  cp test-app/image.bin tools/scripts/nrf5340/image_app.bin
  cp test-app/image.bin tools/scripts/nrf5340/image_app_v${UPDATE_VERSION}.bin
  if [ ! -f tools/scripts/nrf5340/factory_app.bin ]; then
    cp test-app/image_v1_signed.bin tools/scripts/nrf5340/image_app_v1_signed.bin
    cp factory.bin tools/scripts/nrf5340/factory_app.bin
  fi
fi

if [[ $DO_UPDATE == 1 ]]; then
  # Sign flash update for testing (for network partition using --id 2)
  tools/keytools/sign $SIGN_ARGS --id 2 tools/scripts/nrf5340/image_net.bin wolfboot_signing_private_key.der $UPDATE_VERSION
  tools/keytools/sign $SIGN_ARGS        tools/scripts/nrf5340/image_app.bin wolfboot_signing_private_key.der $UPDATE_VERSION

  # Create a bin footer with wolfBoot trailer "BOOT" and "p" (ASCII for 0x70 == IMG_STATE_UPDATING):
  echo -n "pBOOT" > tools/scripts/nrf5340/trigger_magic.bin
  ./tools/bin-assemble/bin-assemble \
    tools/scripts/nrf5340/update_app.bin \
      0x0     tools/scripts/nrf5340/image_app_v${UPDATE_VERSION}_signed.bin \
      0xEDFFB tools/scripts/nrf5340/trigger_magic.bin

  # Net update does not need triggered
  cp tools/scripts/nrf5340/image_net_v${UPDATE_VERSION}_signed.bin tools/scripts/nrf5340/update_net.bin
fi

if [[ $DO_DELTA == 1 ]]; then
  # Sign flash update for testing (for network partition using --id 2) delta between v1 and v3
  tools/keytools/sign $SIGN_ARGS --id 2 --delta tools/scripts/nrf5340/image_net_v1_signed.bin tools/scripts/nrf5340/image_net.bin wolfboot_signing_private_key.der $UPDATE_VERSION
  tools/keytools/sign $SIGN_ARGS        --delta tools/scripts/nrf5340/image_app_v1_signed.bin tools/scripts/nrf5340/image_app.bin wolfboot_signing_private_key.der $UPDATE_VERSION

  # Create a bin footer with wolfBoot trailer "BOOT" and "p" (ASCII for 0x70 == IMG_STATE_UPDATING):
  echo -n "pBOOT" > tools/scripts/nrf5340/trigger_magic.bin
  ./tools/bin-assemble/bin-assemble \
    tools/scripts/nrf5340/update_app.bin \
      0x0     tools/scripts/nrf5340/image_app_v${UPDATE_VERSION}_signed_diff.bin \
      0xEDFFB tools/scripts/nrf5340/trigger_magic.bin

  # Net update does not need triggered
  cp tools/scripts/nrf5340/image_net_v${UPDATE_VERSION}_signed_diff.bin tools/scripts/nrf5340/update_net.bin
fi

if [[ $DO_ERASE_INT == 1 ]]; then
  nrfjprog -f nrf53 --recover
fi
if [[ $DO_ERASE_EXT == 1 ]]; then
  # QSPI Erase/Write doesn't work without a --recover
  nrfjprog -f nrf53 --qspieraseall
fi

if [[ $DO_PROGRAM_EXT == 1 ]]; then
  # Convert to HEX format for programmer tool
  arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x10000000 tools/scripts/nrf5340/update_app.bin tools/scripts/nrf5340/update_app.hex
  arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x10100000 tools/scripts/nrf5340/update_net.bin tools/scripts/nrf5340/update_net.hex
  # Program external flash
  nrfjprog -f nrf53 --program tools/scripts/nrf5340/update_app.hex --verify
  nrfjprog -f nrf53 --program tools/scripts/nrf5340/update_net.hex --verify
fi

if [[ $DO_PROGRAM_INT == 1 ]]; then
  # Convert to HEX format for programmer tool
  arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x00000000 tools/scripts/nrf5340/factory_app.bin tools/scripts/nrf5340/factory_app.hex
  arm-none-eabi-objcopy -I binary -O ihex --change-addresses 0x01000000 tools/scripts/nrf5340/factory_net.bin tools/scripts/nrf5340/factory_net.hex
  # Program Internal Flash
  #nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_app.hex --verify
  #nrfjprog -f nrf53 --program tools/scripts/nrf5340/factory_net.hex --verify --coprocessor CP_NETWORK
  JLinkExe -CommandFile tools/scripts/nrf5340/flash_net.jlink
  JLinkExe -CommandFile tools/scripts/nrf5340/flash_app.jlink
fi
