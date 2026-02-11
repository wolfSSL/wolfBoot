#!/bin/bash
#
function run_on_board() {
    # GPIO2 (pin 27): RST (output) -- connected to STM32H5 NRST
    # GPIO4 (pin 16): BOOT (input) -- connected to STM32H5 PA14

    openocd  -f board/st_nucleo_h5.cfg -c "reset_config connect_assert_srst; init; program wolfboot.bin 0x08000000; program test-app/image_v1_signed.bin 0x08060000; shutdown" &>/dev/null
    sleep 1
    python3 tools/scripts/boot-time.py
}

function set_benchmark {
    NAME=$1
    shift
    CONFIG=$@
    # Name
    echo -n "| "
    echo -n "$NAME"
    echo -n " | "
    # Configuration
    echo -n $CONFIG | tr -d '\n'
    echo -n " | "
    make clean &>/dev/null
    make keysclean &>/dev/null
    make WOLFBOOT_TEST_FILLER=1 WOLFBOOT_TIME_TEST=1 $@ &>/dev/null
    make WOLFBOOT_TEST_FILLER=1 WOLFBOOT_TIME_TEST=1 $@ stack-usage &>/dev/null
    make WOLFBOOT_TEST_FILLER=1 WOLFBOOT_TIME_TEST=1 $@ image-header-size &>/dev/null
    # Bootloader size
    echo -n `ls -l wolfboot.bin | cut -d " " -f 5 | tr -d '\n'`
    echo -n " | "
    # Stack size
    cat .stack_usage | tr -d '\n'
    echo -n " | "
    # Image header size
    cat .image_header_size | tr -d '\n'
    # Application size
    echo -n " | "
    echo -n `ls -l test-app/image.bin | cut -d " " -f 5 | tr -d '\n'`
    # Boot time
    echo -n " | "
    run_on_board 2>&1 | tr -d '\n'
    echo " |"
}

make keytools &>/dev/null
cp config/examples/stm32h5-no-tz.config .config

# Output benchmark results in a Markdown table
echo "| Name | Configuration | Bootloader size | Stack size | Image header size | Application size | Boot time |"
echo "|------|---------------|-----------------|------------|-------------------|------------------|-----------|"

set_benchmark "SHA2 only" SIGN=NONE
set_benchmark "SHA384 only" SIGN=NONE HASH=SHA384
set_benchmark "SHA3 only" SIGN=NONE HASH=SHA3
set_benchmark "SHA2 only,small" SIGN=NONE NO_ASM=1
set_benchmark "rsa2048" SIGN=RSA2048
set_benchmark "rsa3072" SIGN=RSA3072
set_benchmark "rsa4096" SIGN=RSA4096
set_benchmark "rsa4096 with sha384" SIGN=RSA4096 HASH=SHA384
set_benchmark "ecdsa256" SIGN=ECC256
set_benchmark "ecdsa384" SIGN=ECC384
set_benchmark "ecdsa521" SIGN=ECC521
set_benchmark "ecdsa256 with small stack" SIGN=ECC384 WOLFBOOT_SMALL_STACK=1
set_benchmark "ecdsa256 with fast math" SIGN=ECC384 SP_MATH=0
set_benchmark "ecdsa256, no asm" SIGN=ECC256 NO_ASM=1
set_benchmark "ecdsa384, no asm" SIGN=ECC384 NO_ASM=1
set_benchmark "ecdsa521, no asm" SIGN=ECC521 NO_ASM=1
set_benchmark "ecdsa384 with sha384" SIGN=ECC384 HASH=SHA384
set_benchmark "ed25519 with sha384, small" SIGN=ED25519 HASH=SHA384 NO_ASM=1
set_benchmark "ed25519 fast" SIGN=ED25519 NO_ASM=0
set_benchmark "ed448" SIGN=ED448
set_benchmark "ML_DSA-44" SIGN=ML_DSA ML_DSA_LEVEL=2 IMAGE_SIGNATURE_SIZE=2420 IMAGE_HEADER_SIZE=8192
set_benchmark "ML_DSA-65" SIGN=ML_DSA ML_DSA_LEVEL=3 IMAGE_SIGNATURE_SIZE=3309 IMAGE_HEADER_SIZE=8192
set_benchmark "ML_DSA-87" SIGN=ML_DSA ML_DSA_LEVEL=5 IMAGE_SIGNATURE_SIZE=4627 IMAGE_HEADER_SIZE=12288
set_benchmark "LMS 1-10-8" SIGN=LMS LMS_LEVELS=1 LMS_HEIGHT=10 LMS_WINTERNITZ=8 IMAGE_HEADER_SIZE=4096 IMAGE_SIGNATURE_SIZE=1456
set_benchmark "XMSS-SHA2_10_256'" XMSS_PARAMS='XMSS-SHA2_10_256' SIGN=XMSS IMAGE_SIGNATURE_SIZE=2500 IMAGE_HEADER_SIZE=8192
set_benchmark "ML_DSA-65 hybrid with ECDSA384" SIGN=ML_DSA ML_DSA_LEVEL=3 IMAGE_SIGNATURE_SIZE=3309 IMAGE_HEADER_SIZE=8192 SIGN_SECONDARY=ECC384 WOLFBOOT_UNIVERSAL_KEYSTORE=1
set_benchmark "ML_DSA-87 hybrid with ECDSA521" SIGN=ML_DSA ML_DSA_LEVEL=5 IMAGE_SIGNATURE_SIZE=4627 IMAGE_HEADER_SIZE=12288 SIGN_SECONDARY=ECC521 WOLFBOOT_UNIVERSAL_KEYSTORE=1
