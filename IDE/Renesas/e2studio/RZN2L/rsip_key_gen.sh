#!/bin/bash

#
# convert elf to hex file
# This simple shell script assumes to be run on WSL or equivarant environment.
# This script is an example. You can update the script based on your environment.
#
# usage
# rsip_key_gen.sh <SKMT PATH> ufpk w-ufpk
#

if [ $# -ne 4 ];then
    echo "Usage: $0 <SKMT PATH> <ufpk> <w-ufpk> <public-key pem>";
    exit 1
fi

WOLFBOOT_ADDRESS=0x00102000
APP_RAM_ADDR=0x10010000
FLASH_VER1_ADDR=0x60100000
FLASH_VER2_ADDR=0x60180000
RSA_SIGN="--rsa2048"

SKMT_PATH=$1
UFPK_F=$2
WUFPK_F=$3
PUB_KEY=$4
CURRENT=$(cd $(dirname $0);pwd)

PATH=$PATH:${SKMT_PATH}

echo "wolfBoot starts address in RAM  : " $WOLFBOOT_ADDRESS
echo "app starts RAM address          : " $APP_RAM_ADDR
echo "Version 2 app flash address : " $FLASH_VER1_ADDR
echo "Version 1 app flash address : " $FLASH_VER1_ADDR
echo "Version 2 app flash address : " $FLASH_VER1_ADDR

echo
echo Generate RSA Public encrypted key : csource

pushd ../../../../

skmt.exe /genkey /ufpk file=${UFPK_F} /wufpk file=${WUFPK_F} -key file=${PUB_KEY} -mcu RZ-TSIP -keytype RSA-2048-public /output rsa_pub.c /filetype "csource" /keyname rsa_public

cp rsa_pub.h ${CURRENT}/wolfboot/src

skmt.exe /genkey /genkey /ufpk file=${UFPK_F} /wufpk file=${WUFPK_F} -key file=${PUB_KEY} -mcu RZ-TSIP -keytype RSA-2048-public /output rsa_pub2048.bin /filetype "bin"

cp rsa_pub2048.bin ${CURRENT}/flash_app/

popd
