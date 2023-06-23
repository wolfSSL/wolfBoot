#!/bin/bash

#
# convert elf to hex file
# This simple shell script assumes to be run on WSL or equivarant environment.
# This script is an example. You can update the script based on your environment.
#
# usage
# elf2hex.sh <TSIP:0,1> <WOLFBOOT_DIR> <RXELF_BIN_DIR>
#


if [ $# -ne 4 ];then
    echo "Usage: $0 <0, 1 for TSIP LIB use or 2 for TSIP SRC use> WOLFBOOT_DIR RXELF_BIN_DIR <signature method>";
    echo " sig : 0,1 : rsa-2048 (Default)"
    echo "       2 : rsa-3072"
    echo "       3 : ed25519"
    echo "       4 : ed448"
    echo "       5 : ecc256"
    echo "       6 : ecc384"
    echo "       7 : ecc521"
    exit 1
fi

VER1_ADDR=0xffc10000
VER2_ADDR=0xffdf0000

# signature method
RSA2048_SIGN="rsa2048"
RSA3072_SIGN="rsa3072"
ED25519_SIGN="ed25519"
ED488_SIGN="ed448"
ECC256_SIGN="ecc256"
ECC384_SIGN="ecc384"
ECC512_SIGN="ecc521"

SIGN_METHOD=${RSA2048_SIGN}
SIGN_METHOD_EX=""

TSIPUSE=$1
WOLFBOOT_DIR="$2"
RXELF_BIN_DIR="$3"
CURRENT=$(cd $(dirname $0);pwd)
APP_RX=${CURRENT}/app_RenesasRx01
RXELF_OBJCPY_BIN="${RXELF_BIN_DIR}/rx-elf-objcopy.exe"

PATH=$PATH:${WOLFBOOT_DIR}/tools/keytools

case $4 in
    0) ;;
    1) ;;
    2) SIGN_METHOD=${RSA3072_SIGN} ;;
    3) SIGN_METHOD=${ED25519_SIGN} ;;
    4) SIGN_METHOD=${ED488_SIGN} ;;
    5) SIGN_METHOD=${ECC256_SIGN} ;;
    6) SIGN_METHOD=${ECC384_SIGN} ;;
    7) SIGN_METHOD=${ECC512_SIGN} ;;
    *) echo "invalid signature method $4. Please specify [0-8] for sign."
       exit 1 ;;
esac

if [ $TSIPUSE -eq 1 -o $TSIPUSE -eq 2 ]; then
 if [ $TSIPUSE -eq 2 ]; then
     VER1_ADDR=0xffc10000
     VER2_ADDR=0xffdf0000
 else
     VER1_ADDR=0xffc70000
     VER2_ADDR=0xffe20000
 fi
 # only support rsa2048 now
 case $4 in
    0) ;;
    1) ;;
    *) echo "invalid signature mehtod $4. Please specifiy [0-8] for sign."
       exit 1 ;;
 esac
 SIGN_METHOD_EX="enc"

fi

echo "Version 1 app start address : " $VER1_ADDR 
echo "Version 2 app start address : " $VER2_ADDR 
echo "Signature method : " $SIGN_METHOD

echo 
echo COPY app_RenesasRx01.x to RXELF_BIN_DIR to convert bin file
cp ${APP_RX}/HardwareDebug/app_RenesasRx01.x "${RXELF_BIN_DIR}"

pushd "${RXELF_BIN_DIR}"

echo 
echo Run rx-elf-objcopy.exe to generate bin
"${RXELF_OBJCPY_BIN}" -O binary -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30' -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT app_RenesasRx01.x app_RenesasRx01.bin

echo 
echo copy app_RenesasRx01.bin to wolfBoot folder to sign
cp app_RenesasRx01.bin ${WOLFBOOT_DIR}

pushd ${WOLFBOOT_DIR}

echo "generate key"
keygen --${SIGN_METHOD} -g ./pri-${SIGN_METHOD}.der


echo 
echo sign app_RenesasRx01.bin for version 1
sign --${SIGN_METHOD}${SIGN_METHOD_EX} app_RenesasRx01.bin ./pri-${SIGN_METHOD}.der 1.0

echo 
echo sign app_RenesasRx01.bin for version 2
sign --${SIGN_METHOD}${SIGN_METHOD_EX} app_RenesasRx01.bin ./pri-${SIGN_METHOD}.der 2.0

echo 
echo copy app_RenesasRx01_v1.0/v2.0_signed.bin RXELF_BIN_DIR
cp app_RenesasRx01_v1.0_signed.bin "${RXELF_BIN_DIR}"
cp app_RenesasRx01_v2.0_signed.bin "${RXELF_BIN_DIR}"

popd

echo 
echo Run rx-elf-objcopy.exe to generate hex for version 1
"${RXELF_OBJCPY_BIN}" -I binary -O srec --change-addresses=${VER1_ADDR} app_RenesasRx01_v1.0_signed.bin app_RenesasRx01_v1.0_signed.hex

echo 
echo Run rx-elf-objcopy.exe to generate hex for version 2
"${RXELF_OBJCPY_BIN}" -I binary -O srec --change-addresses=${VER2_ADDR} app_RenesasRx01_v2.0_signed.bin app_RenesasRx01_v2.0_signed.hex

echo 
echo move *.hex to ${CURRENT}
mv app_RenesasRx01_v1.0_signed.hex app_RenesasRx01_v2.0_signed.hex ${CURRENT}

echo 
echo Clean up all copied and generated files
rm -rf app_RenesasRx01.x app_RenesasRx01.bin app_RenesasRx01_v1.0_signed.bin app_RenesasRx01_v2.0_signed.bin
popd
