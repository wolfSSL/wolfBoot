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
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sp.h>

#define SP_DIGIT_SIZE (4)



#ifdef WOLFBOOT_SIGN_RSA2048
    #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
        #define SPDIGIT_BUF0_SIZE (SP_DIGIT_SIZE * 64 * 5)
    #else
        #define SPDIGIT_BUF0_SIZE (SP_DIGIT_SIZE * 90 * 5)
        #define SPDIGIT_BUF1_SIZE (SP_DIGIT_SIZE * (90 * 4 + 3))
        static uint8_t sp_digit_buf1[SPDIGIT_BUF1_SIZE];
    #endif
#endif

#ifdef WOLFBOOT_SIGN_RSA4096
    #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
        #define SPDIGIT_BUF0_SIZE (SP_DIGIT_SIZE * 128 * 5)
    #else
        #define SPDIGIT_BUF0_SIZE (SP_DIGIT_SIZE * 180 * 5)
        #define SPDIGIT_BUF1_SIZE (SP_DIGIT_SIZE * (180 * 4 + 3))
        static uint8_t sp_digit_buf1[SPDIGIT_BUF1_SIZE];
    #endif
#endif


static uint8_t sp_digit_buf0[SPDIGIT_BUF0_SIZE];

struct xmalloc_slot {
    uint8_t *addr;
    uint32_t size;
    uint32_t in_use;
};

static uint32_t sha_block[WC_SHA256_BLOCK_SIZE];

static struct xmalloc_slot rsa_xmalloc_slots[] = {
#ifdef WOLFBOOT_HASH_SHA256
    { (uint8_t *)sha_block, WC_SHA256_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { sp_digit_buf0, SPDIGIT_BUF0_SIZE, 0},
#ifndef WOLFSSL_SP_ARM_CORTEX_M_ASM
    { sp_digit_buf1, SPDIGIT_BUF1_SIZE, 0},
#endif
    { NULL, 0, 0}
};

void* XMALLOC(size_t n, void* heap, int type)
{
    int i = 0;

    while (rsa_xmalloc_slots[i].addr) {
        if ((n == rsa_xmalloc_slots[i].size) &&
            (rsa_xmalloc_slots[i].in_use == 0)) {
            rsa_xmalloc_slots[i].in_use++;
            return rsa_xmalloc_slots[i].addr;
        }
        i++;
    }
    return NULL;
}

void XFREE(void *ptr, void *heap, int type)
{
    int i = 0;
    while (rsa_xmalloc_slots[i].addr) {
        if ((ptr == (void *)(rsa_xmalloc_slots[i].addr)) && rsa_xmalloc_slots[i].in_use) {
            rsa_xmalloc_slots[i].in_use = 0;
            return;
        }
        i++;
    }
}
