#!/bin/bash

WOLFBOOT_DIR=$(pwd)

# 16 MB
BIOS_REGION_PATH=/tmp/bios.bin

set -e

# The FSP-M training data cache holds one board's memory training. A flash
# dump of a board that has run wolfBoot carries it, so blank it rather than
# copy it to every board flashed with this image.
erase_fsp_nvs_cache()
{
    local base size image_size offset

    grep -Eq '^FSP_NVS_CACHE[?]?=1$' .config || return 0
    base=$(sed -n 's/^FSP_NVS_BASE?\{0,1\}=//p' .config)
    size=$(sed -n 's/^FSP_NVS_SIZE?\{0,1\}=//p' .config)
    image_size=$(stat -c%s "$WOLFBOOT_DIR/final_image.bin")
    # the image ends at 4 GB
    offset=$((image_size - (0x100000000 - base)))
    head -c $((size)) /dev/zero | tr '\0' '\377' | \
        dd of="$WOLFBOOT_DIR/final_image.bin" bs=64K oflag=seek_bytes \
        seek=$offset conv=notrunc status=none
}

build_and_sign_image()
{
    # compute the size differences between $FLASH_DUMP and "$WOLFBOOT_DIR"/wolfboot_stage1.bin and store it in SIZE
    FLASH_DUMP_SIZE=$(stat -c%s "$FLASH_DUMP")
    WOLFBOOT_SIZE=$(stat -c%s "$BIOS_REGION_PATH")
    SIZE=$((FLASH_DUMP_SIZE - WOLFBOOT_SIZE))
    cp "$FLASH_DUMP" "$WOLFBOOT_DIR/temp_image.bin"
    truncate -s $SIZE "$WOLFBOOT_DIR/temp_image.bin"
    cat "$WOLFBOOT_DIR/temp_image.bin" "$BIOS_REGION_PATH" > "$WOLFBOOT_DIR/final_image.bin"
    erase_fsp_nvs_cache
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
while getopts "n:" opt; do
    case "$opt" in
        n)
            FLASH_DUMP="$OPTARG"
            ;;
        *)
            echo "Usage: $0 -n FLASH_DUMP"
            echo "-n FLASH_DUMP: assemble an image for being used without IBG"
            exit 1
            ;;
    esac
done

assemble
