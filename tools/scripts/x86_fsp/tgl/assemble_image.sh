#!/bin/bash

WOLFBOOT_DIR=$(pwd)

# 16 MB
BIOS_REGION_PATH=/tmp/bios.bin

set -e

build_and_sign_image()
{
    # compute the size differences between $FLASH_DUMP and "$WOLFBOOT_DIR"/wolfboot_stage1.bin and store it in SIZE
    FLASH_DUMP_SIZE=$(stat -c%s "$FLASH_DUMP")
    WOLFBOOT_SIZE=$(stat -c%s "$BIOS_REGION_PATH")
    SIZE=$((FLASH_DUMP_SIZE - WOLFBOOT_SIZE))
    cp "$FLASH_DUMP" "$WOLFBOOT_DIR/temp_image.bin"
    truncate -s $SIZE "$WOLFBOOT_DIR/temp_image.bin"
    cat "$WOLFBOOT_DIR/temp_image.bin" "$BIOS_REGION_PATH" > "$WOLFBOOT_DIR/final_image.bin"
    if grep -q '^WOLFBOOT_TPM_SEAL=1$' .config; then
        PCR0=$(python ./tools/scripts/x86_fsp/compute_pcr.py "$WOLFBOOT_DIR"/final_image.bin | tail -n 1)
        "$WOLFBOOT_DIR"/tools/tpm/policy_sign -ecc256 -key=tpm_seal_key.key -pcr=0 -pcrdigest="$PCR0"
        IMAGE_FILE="$WOLFBOOT_DIR"/final_image.bin "$WOLFBOOT_DIR"/tools/scripts/x86_fsp/tpm_install_policy.sh policy.bin.sig
    fi
}

assemble()
{
    cp "$WOLFBOOT_DIR/wolfboot_stage1.bin" $BIOS_REGION_PATH
    build_and_sign_image
}

# Parse command line options
while getopts "s:n:m:" opt; do
    case "$opt" in
        n)
            FLASH_DUMP="$OPTARG"
            ;;
        *)
            echo "Usage: $0 [-k] [-s FLASH_DUMP]"
            echo "-k: make keys"
            echo "-n FLASH_DUMP: assemble an image for being used without IBG"
            exit 1
            ;;
    esac
done

assemble
