#!/bin/bash

SIGN_TOOL="python3 ./tools/keytools/sign.py"
if [ -f "./tools/keytools/sign" ]; then
    SIGN_TOOL="./tools/keytools/sign"
fi

# SIZE is WOLFBOOT_PARTITION_SIZE - 49 (44B: key + nonce, 5B: "pBOOT")
SIZE=131023
VERSION=8
APP=test-app/image_v"$VERSION"_signed_and_encrypted.bin

# Create test key
echo -n "0123456789abcdef0123456789abcdef0123456789ab" > enc_key.der

$SIGN_TOOL --encrypt enc_key.der test-app/image.bin ecc256.der $VERSION
dd if=/dev/zero bs=$SIZE count=1 2>/dev/null | tr "\000" "\377" > update.bin
dd if=$APP of=update.bin bs=1 conv=notrunc
printf "pBOOT"  >> update.bin
