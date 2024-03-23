#!/bin/bash

#
# convert elf to hex file
# This simple shell script assumes to be run on WSL or equivarant environment.
# This script is an example. You can update the script based on your environment.
#
# usage
# sign.sh <RSIP:0,1> WOLFBOOT-FOLDER
#


if [ $# -ne 2 ];then
    echo "Usage: $0 <0 or 1 for RSIP use> WOLFBOOT-FOLDER";
    exit 1
fi

WOLFBOOT_ADDRESS=0x00102000
APP_RAM_ADDR=0x10010000
FLASH_VER1_ADDR=0x60100000
FLASH_VER2_ADDR=0x60180000
RSA_SIGN="--rsa2048"

RSIPUSE=$1
WOLFBOOT_DIR=$2
CURRENT=$(cd $(dirname $0);pwd)
APP_RZ=${CURRENT}/app_RZ

PATH=$PATH:${WOLFBOOT_DIR}/tools/keytools

if [ $RSIPUSE -eq 1 ]; then
 RSA_SIGN="--rsa2048enc"
fi

echo "wolfBoot starts address in RAM  : " $WOLFBOOT_ADDRESS
echo "app starts RAM address          : " $APP_RAM_ADDR
echo "Version 1 app flash address : " $FLASH_VER1_ADDR
echo "Version 2 app flash address : " $FLASH_VER2_ADDR
echo "Signature method : " $RSA_SIGN

echo
echo sign app_RZ.bin

pushd ${APP_RZ}/Debug

echo
echo sign app_RA.bin for version 1
sign ${RSA_SIGN} app_RZ.bin ../../../../../../pri-rsa2048.der 1.0

echo
echo sign app_RA.bin for version 2
sign ${RSA_SIGN} app_RZ.bin ../../../../../../pri-rsa2048.der 2.0

popd
