/* xmalloc_ecc.c
 *
 * Implementations of minimal malloc/free
 *
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <wolfssl/wolfcrypt/sp.h>
#include "target.h"

#define SP_CURVE_SPECS_SIZE (80)
#define SP_DIGIT_SIZE (4)

#ifdef WOLFBOOT_HASH_SHA256
#include <wolfssl/wolfcrypt/sha256.h>
#   define HASH_BLOCK_SIZE WC_SHA256_BLOCK_SIZE
#elif defined WOLFBOOT_HASH_SHA3_384
#include <wolfssl/wolfcrypt/sha3.h>
#   define HASH_BLOCK_SIZE WC_SHA3_384_BLOCK_SIZE
#else
#   error "No hash mechanism selected."
#endif


#ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
    #define SP_POINT_SIZE (196)
    #define SP_DIGITS_BUFFER_SIZE_0 (SP_DIGIT_SIZE * 16 * 8)
    #define SP_DIGITS_BUFFER_SIZE_1 (SP_DIGIT_SIZE * 2 * 8 * 5)
#else
    #define SP_POINT_SIZE (244)
    #define SP_DIGITS_BUFFER_SIZE_0 (SP_DIGIT_SIZE * 16 * 10)
    #define SP_DIGITS_BUFFER_SIZE_1 (SP_DIGIT_SIZE * (3 * 10 + 1))
    #define SP_DIGITS_BUFFER_SIZE_2 (SP_DIGIT_SIZE * (2 * 10 * 5))
#endif



struct xmalloc_slot {
    uint8_t *addr;
    uint32_t size;
    uint32_t in_use;
};

static uint8_t sp_curve_specs[SP_CURVE_SPECS_SIZE];
static uint8_t sp_points_0[SP_POINT_SIZE * 2];
static uint8_t sp_points_1[SP_POINT_SIZE * 2];
static uint8_t sp_points_2[SP_POINT_SIZE * (16 + 1)];
#ifndef WC_NO_CACHE_RESISTANT
static uint8_t sp_points_3[SP_POINT_SIZE];
#endif
static uint8_t sp_digits_buffer_0[SP_DIGITS_BUFFER_SIZE_0];
static uint8_t sp_digits_buffer_1[SP_DIGITS_BUFFER_SIZE_1];
#ifndef WOLFSSL_SP_ARM_CORTEX_M_ASM
static uint8_t sp_digits_buffer_2[SP_DIGITS_BUFFER_SIZE_2];
static uint8_t sp_montgomery[sizeof(int64_t) * 2 * 8];
#endif
static uint32_t sha_block[HASH_BLOCK_SIZE];

static struct xmalloc_slot ecc_xmalloc_slots[] = {
#ifdef WOLFBOOT_HASH_SHA256
    { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { (uint8_t *)sp_curve_specs, SP_CURVE_SPECS_SIZE, 0 },
    { (uint8_t *)sp_points_0, SP_POINT_SIZE * 2, 0 },
#ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
    { (uint8_t *)sp_points_1, SP_POINT_SIZE * 2, 0 },
#else
    { (uint8_t *)sp_points_1, SP_POINT_SIZE * 3, 0 },
    { (uint8_t *)sp_digits_buffer_2, SP_DIGITS_BUFFER_SIZE_2, 0 },
    { (uint8_t *)sp_montgomery, sizeof(int64_t) * 2 * 8, 0 },
#endif
    { (uint8_t *)sp_points_2, SP_POINT_SIZE * (16 + 1), 0 },
    { (uint8_t *)sp_digits_buffer_0, SP_DIGITS_BUFFER_SIZE_0, 0},
    { (uint8_t *)sp_digits_buffer_1, SP_DIGITS_BUFFER_SIZE_1, 0},
#ifndef WC_NO_CACHE_RESISTANT
    { (uint8_t *)sp_points_3, SP_POINT_SIZE, 0 },
#endif
    { NULL, 0, 0}
};

void* XMALLOC(size_t n, void* heap, int type)
{
    int i = 0;

    while (ecc_xmalloc_slots[i].addr) {
        if ((n == ecc_xmalloc_slots[i].size) &&
            (ecc_xmalloc_slots[i].in_use == 0)) {
            ecc_xmalloc_slots[i].in_use++;
            return ecc_xmalloc_slots[i].addr;
        }
        i++;
    }
    return NULL;
}

void XFREE(void *ptr, void *heap, int type)
{
    int i = 0;
    while (ecc_xmalloc_slots[i].addr) {
        if ((ptr == (void *)(ecc_xmalloc_slots[i].addr)) && ecc_xmalloc_slots[i].in_use) {
            ecc_xmalloc_slots[i].in_use = 0;
            return;
        }
        i++;
    }
}
