#!/usr/bin/env bash

SIGN_TOOL="python3 ./tools/keytools/sign.py"
if [ -f "./tools/keytools/sign" ]; then
    SIGN_TOOL="./tools/keytools/sign"
fi

# SIZE is WOLFBOOT_PARTITION_SIZE - 49 (44B: key + nonce, 5B: "pBOOT")
SIZE=131023
#SIZE=65487
VERSION=8
APP=test-app/image_v"$VERSION"_signed_and_encrypted.bin

# Create test key
echo -n "0123456789abcdef0123456789abcdef0123456789ab" > enc_key.der

$SIGN_TOOL --ecc256 --encrypt enc_key.der test-app/image.bin wolfboot_signing_private_key.der $VERSION
dd if=/dev/zero bs=$SIZE count=1 2>/dev/null | tr "\000" "\377" > update.bin
dd if=$APP of=update.bin bs=1 conv=notrunc

printf "pBOOT"  >> update.bin

#Make a 1MB rom image for SPI
rm -f update.rom
dd if=/dev/zero bs=1M count=1 2>/dev/null | tr "\000" "\377" > update.rom
dd if=update.bin of=update.rom bs=1 conv=notrunc
