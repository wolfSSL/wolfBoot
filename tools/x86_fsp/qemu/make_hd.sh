#!/bin/bash
SIGN=${SIGN:-"--ecc256"}
HASH=${HASH:-"--sha256"}

IMAGE=${IMAGE:-"bzImage"}

set -e

dd if=/dev/zero of=app.bin bs=1M count=64
/sbin/fdisk app.bin <<EOF
g
n
1

+16M
n


+16M
w
EOF

cp ${IMAGE} "image.bin"
tools/keytools/sign $SIGN $HASH image.bin wolfboot_signing_private_key.der 1
tools/keytools/sign $SIGN $HASH image.bin wolfboot_signing_private_key.der 2
dd if=image_v1_signed.bin of=app.bin bs=512 seek=2048 conv=notrunc
dd if=image_v2_signed.bin of=app.bin bs=512 seek=34816 conv=notrunc
