#!/bin/bash

#
# convert elf to hex file
# This simple shell script assumes to be run on WSL or equivarant environment.
# This script is an example. You can update the script based on your environment.
#
# usage
# elf2hex.sh <SCE:0,1> <WOLFBOOT_DIR> <AARCH64_BIN_DIR>
#


if [ $# -ne 3 ];then
    echo "Usage: $0 <0 or 1 for SCE use> WOLFBOOT_DIR AARCH64_BIN_DIR";
    exit 1
fi

SCEUSE_OFFSET=0x20000
VER1_ADDR=0x00010000
VER2_ADDR=0x00080000
RSA_SIGN="--rsa2048"

SCEUSE=$1
WOLFBOOT_DIR=$2
AARCH64_BIN_DIR=$3
CURRENT=$(cd $(dirname $0);pwd)
APP_RA=${CURRENT}/app_RA
AARCH64_OBJCPY_BIN=${AARCH64_BIN_DIR}/aarch64-none-elf-objcopy.exe

PATH=$PATH:${WOLFBOOT_DIR}/tools/keytools

if [ $SCEUSE -eq 1 ]; then
 VER1_ADDR=0x00020000
 VER2_ADDR=0x00090000
 RSA_SIGN="--rsa2048enc"
fi

echo "Version 1 app start address : " $VER1_ADDR 
echo "Version 2 app start address : " $VER2_ADDR 
echo "Signature method : " $RSA_SIGN

echo 
echo COPY app_RA.elf to AARCH64_BIN_DIR to convert bin file
cp ${APP_RA}/Debug/app_RA.elf ${AARCH64_BIN_DIR}

pushd ${AARCH64_BIN_DIR}

echo 
echo Run aarch64-none-elf-objcopy.exe to generate bin
${AARCH64_OBJCPY_BIN} -O binary -j .text -j .data app_RA.elf app_RA.bin

echo 
echo copy app_RA.bin to wolfBoot folder to sign
cp app_RA.bin ${WOLFBOOT_DIR}

pushd ${WOLFBOOT_DIR}

echo 
echo sign app_RA.bin for version 1
sign ${RSA_SIGN} app_RA.bin ./pri-rsa2048.der 1.0

echo 
echo sign app_RA.bin for version 2
sign ${RSA_SIGN} app_RA.bin ./pri-rsa2048.der 2.0

echo 
echo copy app_RA_v1.0/v2.0_signed.bin AARCH64_BIN_DIR
cp app_RA_v1.0_signed.bin ${AARCH64_BIN_DIR}
cp app_RA_v2.0_signed.bin ${AARCH64_BIN_DIR}

popd

echo 
echo Run aarch64-none-elf-objcopy.exe to generate hex for version 1
${AARCH64_OBJCPY_BIN} -I binary -O srec --change-addresses=${VER1_ADDR} app_RA_v1.0_signed.bin app_RA_v1.0_signed.hex

echo 
echo Run aarch64-none-elf-objcopy.exe to generate hex for version 2
${AARCH64_OBJCPY_BIN} -I binary -O srec --change-addresses=${VER2_ADDR} app_RA_v2.0_signed.bin app_RA_v2.0_signed.hex

echo 
echo move *.hex to ${CURRENT}
mv app_RA_v1.0_signed.hex app_RA_v2.0_signed.hex ${CURRENT}

echo 
echo Clean up all copied and generated files
rm -rf app_RA.elf app_RA.bin app_RA_v1.0_signed.bin app_RA_v2.0_signed.bin
popd
