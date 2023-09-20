#!/bin/bash

set -x

# variable IMAGE_FILE should be wolfboot_stage1.bin if not defined
IMAGE_FILE=${IMAGE_FILE:-"wolfboot_stage1.bin"}

# take POLICY_FILE FROM ARGUMENT 1
POLICY_FILE="$1"
POLICY_SZ=$(wc -c < "$POLICY_FILE")

# grep stage1/loader_stage1.map for the address of the symbol _start_policy and save in the variable POLICY_START
POLICY_START=$(grep "_start_policy" stage1/loader_stage1.map | awk '{print $1}')
POLICY_SIZE_SYMBOL=$(grep "_policy_size_u32" stage1/loader_stage1.map | awk '{print $1}')

# calculate offsets as length in bytes of IMAGE_FILE - (4GB - offset)
IMAGE_LENGTH=$(wc -c < "$IMAGE_FILE")
POLICY_OFF=$((IMAGE_LENGTH - (4 * 1024 * 1024 * 1024 - POLICY_START)))
POLICY_SZ_OFF=$((IMAGE_LENGTH - (4 * 1024 * 1024 * 1024 - POLICY_SIZE_SYMBOL)))

printf "%08x" $POLICY_SZ | \
 rev  | \
 xxd -r -p | \
 dd conv=notrunc bs=1 seek="$POLICY_SZ_OFF" of="$IMAGE_FILE" bs=1

# overwrite the content of IMAGE_FILE at offset POLICY_OFF with the content of POLICY_FILE
dd if="$POLICY_FILE" of="$IMAGE_FILE" bs=1 seek="$POLICY_OFF" conv=notrunc
