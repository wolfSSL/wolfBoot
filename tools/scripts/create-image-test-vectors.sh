#!/bin/sh

cp config/examples/raspi3.config .config
make distclean
env LANG=C env LC_ALL=C make ARCH_FLASH_OFFSET=0x80000

cd $(dirname $0)/../..
WBDIR=$(pwd)
mkdir -p image-test-vectors
cd image-test-vectors

echo "INFO: create dummy images and download a DTB"

curl -o test.dtb https://github.com/raspberrypi/firmware/blob/master/boot/bcm2710-rpi-3-b.dtb
dd bs=1m count=1 if=/dev/urandom of=1m.bin
dd bs=20m count=1 if=/dev/urandom of=20m.bin

echo "INFO: sign test material"

python3 ${WBDIR}/tools/keytools/sign.py --rsa4096 --sha3 test.dtb ${WBDIR}/rsa4096.der 1
python3 ${WBDIR}/tools/keytools/sign.py --rsa4096 --sha3 1m.bin ${WBDIR}/rsa4096.der 1
python3 ${WBDIR}/tools/keytools/sign.py --rsa4096 --sha3 20m.bin ${WBDIR}/rsa4096.der 1

echo "INFO: create a forged signature"

cp 1m.bin 1m-forged.bin
python3 ${WBDIR}/tools/keytools/keygen.py --rsa4096 rsa4096-forged.c
mv rsa4096.der rsa4096-forged.der
python3 ${WBDIR}/tools/keytools/sign.py --rsa4096 --sha3 1m-forged.bin rsa4096-forged.der 1

echo "INFO: tamper with signed test material"

cp tes_v1_signed.bin tes_v1_signed-but-tampered.bin
cp 1m_v1_signed.bin 1m_v1_signed-but-tampered.bin
dd if=/dev/urandom of=tes_v1_signed-but-tampered.bin bs=1 count=1 seek=10k conv=notrunc
dd if=/dev/urandom of=1m_v1_signed-but-tampered.bin bs=1 count=1 seek=10k conv=notrunc

echo "INFO: create test images"

cat ${WBDIR}/wolfboot-align.bin 1m_v1_signed.bin > test-05-no-dtb-partition.img
cat ${WBDIR}/wolfboot-align.bin 1m-forged_v1_signed.bin > kernel8-forged.img
cat ${WBDIR}/wolfboot-align.bin 1m.bin > test-01-no-boot-partition.img
cp test-05-no-dtb-partition.img test-07-with-signed-dtb.img
cp test-05-no-dtb-partition.img test-08-with-raw-dtb.img
cp kernel8-forged.img test-04-forged-boot-image-with-signed-dtb.img
cp test-05-no-dtb-partition.img test-06-with-tampered-dtb.img

cat ${WBDIR}/wolfboot-align.bin 1m_v1_signed-but-tampered.bin > test-03-tampered-boot-image-with-signed-dtb.img

cat ${WBDIR}/wolfboot-align.bin 20m_v1_signed.bin > kernel8-oversized.img
cp kernel8-oversized.img test-02-oversized-boot-image-with-signed-dtb.img

dd if=tes_v1_signed.bin of=test-07-with-signed-dtb.img bs=1 seek=128k conv=notrunc
dd if=test.dtb of=test-08-with-raw-dtb.img bs=1 seek=128k conv=notrunc
dd if=tes_v1_signed.bin of=test-04-forged-boot-image-with-signed-dtb.img bs=1 seek=128k conv=notrunc
dd if=tes_v1_signed-but-tampered.bin of=test-06-with-tampered-dtb.img bs=1 seek=128k conv=notrunc
dd if=tes_v1_signed.bin of=test-02-oversized-boot-image-with-signed-dtb.img bs=1 seek=128k conv=notrunc

echo "INFO: clean-up"

rm rsa4096-forged.* kernel8-forged.img kernel8-oversized.img *.bin test.dtb

echo "INFO: Created following image test vectors, boot them for debug prints"
cd ..
ls -la image-test-vectors/*.img
