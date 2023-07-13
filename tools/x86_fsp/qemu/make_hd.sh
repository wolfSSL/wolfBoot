#!/bin/bash
IMAGE=bzImage

dd if=/dev/zero of=app.bin bs=1M count=64
fdisk app.bin <<EOF 
g
n
1

+16M
n


+16M
w
EOF
# copy bzImage in the root folder
tools/keytools/sign --ecc256 --sha256 ${IMAGE} wolfboot_signing_private_key.der 1
tools/keytools/sign --ecc256 --sha256 ${IMAGE} wolfboot_signing_private_key.der 2
dd if=${IMAGE}_v1_signed.bin of=app.bin bs=512 seek=2048 conv=notrunc
dd if=${IMAGE}_v2_signed.bin of=app.bin bs=512 seek=34816 conv=notrunc
