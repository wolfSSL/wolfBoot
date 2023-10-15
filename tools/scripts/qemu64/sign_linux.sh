#!/usr/bin/env bash

SIGN=${SIGN:-"--ecc256"}
HASH=${HASH:-"--sha256"}

cp /tmp/br-linux-wolfboot/output/images/bzImage .
tools/keytools/sign $SIGN $HASH bzImage wolfboot_signing_private_key.der 8
tools/keytools/sign $SIGN $HASH bzImage wolfboot_signing_private_key.der 2

cp base-part-image app.bin
dd if=bzImage_v8_signed.bin of=app.bin bs=1k seek=1024 conv=notrunc
dd if=bzImage_v2_signed.bin of=app.bin bs=1k seek=17408 conv=notrunc



