
cp /tmp/br-linux-wolfboot/output/images/bzImage .
tools/keytools/sign --ecc256 --sha256 bzImage wolfboot_signing_private_key.der 8
tools/keytools/sign --ecc256 --sha256 bzImage wolfboot_signing_private_key.der 2

cp base-part-image app.bin
dd if=bzImage_v8_signed.bin of=app.bin bs=1K seek=1024 conv=notrunc
dd if=bzImage_v2_signed.bin of=app.bin bs=1K seek=17408 conv=notrunc



