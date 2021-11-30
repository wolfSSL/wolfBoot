#!/bin/sh

WORK_DIR=/tmp/wolfBoot_efi
BR_VER=2021.08.2
BR_DIR=buildroot-$BR_VER
IMAGE_DIR=$WORK_DIR/output

if (test ! -d $WORK_DIR);then
    mkdir -p $WORK_DIR
fi

if (test ! -d $WORK_DIR/$BR_DIR);then
    curl https://buildroot.org/downloads/$BR_DIR.tar.gz -o $WORK_DIR/$BR_DIR.tar.gz
    tar xvf $WORK_DIR/$BR_DIR.tar.gz -C $WORK_DIR
fi

BR2_EXTERNAL=$(pwd)/tools/efi/br_ext_dir make -C $WORK_DIR/$BR_DIR tiny_defconfig O=$IMAGE_DIR
make -C $WORK_DIR/$BR_DIR O=$IMAGE_DIR

SIGN_TOOL="python3 ./tools/keytools/sign.py"
if [ -f "./tools/keytools/sign" ]; then
    SIGN_TOOL="./tools/keytools/sign"
fi

$SIGN_TOOL --ed25519 $IMAGE_DIR/images/bzImage ed25519.der 1
$SIGN_TOOL --ed25519 $IMAGE_DIR/images/bzImage ed25519.der 2

mkdir -p /tmp/efi
sudo mount /tmp/efi.disk /tmp/efi
sudo cp $IMAGE_DIR/images/bzImage_v1_signed.bin /tmp/efi/kernel.img
sudo cp $IMAGE_DIR/images/bzImage_v2_signed.bin /tmp/efi/update.img
sudo cp wolfboot.efi /tmp/efi
sudo umount /tmp/efi


