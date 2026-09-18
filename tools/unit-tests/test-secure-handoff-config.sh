#!/bin/sh

# test-secure-handoff-config.sh
#
# Copyright (C) 2026 wolfSSL Inc.
#
# This file is part of wolfBoot.
#
# wolfBoot is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# wolfBoot is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

expect_rejected()
{
    expected=$1
    shift
    if output=$(make -C "$ROOT" -n "$@" wolfboot.bin 2>&1); then
        printf 'configuration unexpectedly accepted: %s\n' "$expected" >&2
        exit 1
    fi
    printf '%s\n' "$output" | grep -Fq "$expected"
}

expect_stack_usage()
{
    expected=$1
    shift
    output=$(make -C "$ROOT" -pn "$@" 2>/dev/null || true)
    actual=$(printf '%s\n' "$output" | awk \
        '$1 == "STACK_USAGE" && $2 == "=" { value = $3 } END { print value }')
    if [ "$actual" != "$expected" ]; then
        printf 'STACK_USAGE: expected %s, got %s\n' \
            "$expected" "$actual" >&2
        exit 1
    fi
}

expect_rejected \
    "SIGN=NONE is incompatible with the authenticated secure-app handoff" \
    TARGET=stm32h5 WOLFBOOT_SECURE_APP=1 SIGN=NONE

expect_rejected \
    "WOLFBOOT_SKIP_BOOT_VERIFY=1 is incompatible with the authenticated secure-app handoff" \
    TARGET=stm32h5 WOLFBOOT_SECURE_APP=1 WOLFBOOT_SKIP_BOOT_VERIFY=1 \
    WOLFBOOT_SELF_HEADER=1 SELF_UPDATE_MONOLITHIC=1 \
    WOLFBOOT_PARTITION_SELF_HEADER_ADDRESS=0x0C140000

expect_stack_usage 25000 TARGET=stm32h5 WOLFBOOT_SECURE_APP=1 \
    WOLFCRYPT_TZ=0 WOLFCRYPT_TZ_PSA=0 WOLFCRYPT_TZ_PKCS11=0 SIGN=ML_DSA

expect_stack_usage 69232 TARGET=stm32h5 WOLFBOOT_SECURE_APP=1 \
    WOLFCRYPT_TZ=0 WOLFCRYPT_TZ_PSA=0 WOLFCRYPT_TZ_PKCS11=0 \
    SIGN=RSA4096 SPMATH=0

expect_stack_usage 16688 TARGET=stm32h5 WOLFBOOT_SECURE_APP=1 \
    WOLFCRYPT_TZ=0 WOLFCRYPT_TZ_PSA=0 WOLFCRYPT_TZ_PKCS11=0 SIGN=ECC256
