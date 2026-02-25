#!/bin/bash

#
# convert elf to hex file
# This simple shell script assumes to be run on WSL or equivarant environment.
# This script is an example. You can update the script based on your environment.
#
# usage
# elf2hex.sh <SCE:0,1> <WOLFBOOT_DIR> <OBJCPY_BIN_DIR>
#


if [ $# -ne 3 ];then
    echo "Usage: $0 <0 or 1 for SCE use> WOLFBOOT_DIR OBJCPY_BIN_DIR";
    exit 1
fi

SCEUSE_OFFSET=0x20000
VER1_ADDR=0x00010000
VER2_ADDR=0x00080000
RSA_SIGN="--rsa2048"

SCEUSE=$1
WOLFBOOT_DIR=$2
OBJCPY_BIN_DIR=$3
CURRENT=$(cd $(dirname $0);pwd)
APP_RA_ELF=${CURRENT}/app_RA/Debug/app_RA.elf
OBJCPY_BIN="${OBJCPY_BIN_DIR}/arm-none-eabi-objcopy.exe"

PATH=$PATH:${WOLFBOOT_DIR}/tools/keytools

if [ $SCEUSE -eq 1 ]; then
 VER1_ADDR=0x00020000
 VER2_ADDR=0x00090000
 RSA_SIGN="--rsa2048enc"
fi

echo "Version 1 app start address : " $VER1_ADDR 
echo "Version 2 app start address : " $VER2_ADDR 
echo "Signature method : " $RSA_SIGN

#echo cp ${APP_RA_ELF} ${OBJCPY_BIN_DIR}
#cp ${APP_RA_ELF} ${OBJCPY_BIN_DIR}
#exit
#pushd "${OBJCPY_BIN_DIR}"
pwd
echo 
echo Run objcopy.exe to generate bin
echo "${OBJCPY_BIN}" -O binary --gap-fill=0xff -j '__flash_vectors$$' -j '__flash_readonly$$' -j '__flash_ctor$$' -j '__flash_preinit_array$$' -j '__flash_.got$$' -j '__flash_init_array$$' -j '__flash_fini_array$$' -j '__flash_arm.extab$$' -j '__flash_arm.exidx$$' -j '__ram_from_flash$$' "$(wslpath -w "${APP_RA_ELF}")" app_RA.bin
"${OBJCPY_BIN}" -O binary --gap-fill=0xff \
  -j '__flash_vectors$$' \
  -j '__flash_readonly$$' \
  -j '__flash_ctor$$' \
  -j '__flash_preinit_array$$' \
  -j '__flash_.got$$' \
  -j '__flash_init_array$$' \
  -j '__flash_fini_array$$' \
  -j '__flash_arm.extab$$' \
  -j '__flash_arm.exidx$$' \
  -j '__ram_from_flash$$' \
  "$(wslpath -w "${APP_RA_ELF}")" app_RA.bin

echo 
echo copy app_RA.bin to wolfBoot folder to sign
cp app_RA.bin "${WOLFBOOT_DIR}"

pushd "${WOLFBOOT_DIR}"

echo 
echo sign app_RA.bin for version 1
sign ${RSA_SIGN} app_RA.bin ./pri-rsa2048.der 1.0

echo 
echo sign app_RA.bin for version 2
sign ${RSA_SIGN} app_RA.bin ./pri-rsa2048.der 2.0

echo 
echo move app_RA_v1.0/v2.0_signed.bin OBJCPY_BIN DIR
mv app_RA_v1.0_signed.bin "${CURRENT}"
mv app_RA_v2.0_signed.bin "${CURRENT}"
APP_V1_BIN=${CURRENT}/app_RA_v1.0_signed.bin
APP_V2_BIN=${CURRENT}/app_RA_v2.0_signed.bin

popd

echo 
echo Run aarch64-none-elf-objcopy.exe to generate hex for version 1
"${OBJCPY_BIN}" -I binary -O srec --change-addresses=${VER1_ADDR} "$(wslpath -w "${APP_V1_BIN}")" app_RA_v1.0_signed.hex

echo 
echo Run aarch64-none-elf-objcopy.exe to generate hex for version 2
"${OBJCPY_BIN}" -I binary -O srec --change-addresses=${VER2_ADDR} "$(wslpath -w "${APP_V2_BIN}")" app_RA_v2.0_signed.hex

#echo 
#echo move *.hex to ${CURRENT}
#mv app_RA_v1.0_signed.hex app_RA_v2.0_signed.hex ${CURRENT}

#echo 
#echo Clean up all copied and generated files
#rm -rf app_RA.elf app_RA.bin app_RA_v1.0_signed.bin app_RA_v2.0_signed.bin
#popd
