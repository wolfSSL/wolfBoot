
/* xmalloc.c
 *
 * Fixed-pool implementation of malloc/free for wolfBoot
 *
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
#include <wolfssl/wolfcrypt/settings.h>
#ifndef USE_FAST_MATH
    #include <wolfssl/wolfcrypt/sp.h>
    #include <wolfssl/wolfcrypt/sp_int.h>
#else
    #include <wolfssl/wolfcrypt/tfm.h>
#endif
#include "target.h"


#ifdef WOLFBOOT_DEBUG_MALLOC
#include <stdio.h>
#endif



struct xmalloc_slot {
    uint8_t *addr;
    uint32_t size;
    uint32_t in_use;
};

#define MP_DIGIT_SIZE (sizeof(mp_digit))

#ifdef WOLFBOOT_HASH_SHA256
#   include <wolfssl/wolfcrypt/sha256.h>
#   define HASH_BLOCK_SIZE WC_SHA256_BLOCK_SIZE
#elif defined WOLFBOOT_HASH_SHA384
#   include <wolfssl/wolfcrypt/sha512.h>
#   define HASH_BLOCK_SIZE (WC_SHA384_BLOCK_SIZE / sizeof(uint32_t))
#elif defined WOLFBOOT_HASH_SHA3_384
#   include <wolfssl/wolfcrypt/sha3.h>
#   define HASH_BLOCK_SIZE WC_SHA3_384_BLOCK_SIZE
#else
#   error "No hash mechanism selected."
#endif


#if defined(WOLFBOOT_SIGN_ECC256) || defined(WOLFBOOT_SIGN_ECC384) || defined(WOLFBOOT_SIGN_ECC521)

#ifndef USE_FAST_MATH
    /* SP MATH */
    #ifdef WOLFBOOT_SIGN_ECC256
        #define MP_SCHEME "SP ECC256"
        #define MP_CURVE_SPECS_SIZE (76)
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MP_POINT_SIZE (196)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 8)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * 2 * 8 * 6)
        #else
            #define MP_POINT_SIZE (220)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 9)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * (4 * 9 + 3))
            #define MP_DIGITS_BUFFER_SIZE_2 (MP_DIGIT_SIZE * (2 * 9 * 6))
            #define MP_MONTGOMERY_SIZE (sizeof(int64_t) * 2 * 8)
        #endif
    #endif /* WOLFBOOT_SIGN_ECC256 */
    #ifdef WOLFBOOT_SIGN_ECC384
        #define MP_SCHEME "SP ECC384"
        #define MP_CURVE_SPECS_SIZE (108)
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MP_POINT_SIZE (292)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 12)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * 2 * 12 * 6)
            #define MP_MONTGOMERY_SIZE (sizeof(int64_t) * 12)
        #else
            #define MP_POINT_SIZE (364)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 15)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * (4 * 15 + 3))
            #define MP_DIGITS_BUFFER_SIZE_2 (MP_DIGIT_SIZE * (2 * 15 * 6))
            #define MP_MONTGOMERY_SIZE (sizeof(int64_t) * 2 * 12)
        #endif
    #endif /* WOLFBOOT_SIGN_ECC384 */
    #ifdef WOLFBOOT_SIGN_ECC521
        #define MP_SCHEME "SP ECC521"
        #define MP_CURVE_SPECS_SIZE (148)
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MP_POINT_SIZE (412)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 17)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * 2 * 17 * 6)
            #define MP_MONTGOMERY_SIZE (sizeof(int64_t) * 12)
        #else
            #define MP_POINT_SIZE (508)
            #define MP_DIGITS_BUFFER_SIZE_0 (MP_DIGIT_SIZE * 18 * 21)
            #define MP_DIGITS_BUFFER_SIZE_1 (MP_DIGIT_SIZE * (4 * 21 + 3))
            #define MP_DIGITS_BUFFER_SIZE_2 (MP_DIGIT_SIZE * (2 * 21 * 6))
            #define MP_MONTGOMERY_SIZE (sizeof(int64_t) * 2 * 12)
        #endif
    #endif /* WOLFBOOT_SIGN_ECC521 */
    #ifndef WC_NO_CACHE_RESISTANT
    static uint8_t mp_points_3[MP_POINT_SIZE];
    #endif
    static uint8_t mp_points_0[MP_POINT_SIZE * 2];
    static uint8_t mp_points_1[MP_POINT_SIZE * 2];
    static uint8_t mp_points_2[MP_POINT_SIZE * (16 + 1)];
    static uint8_t mp_digits_buffer_0[MP_DIGITS_BUFFER_SIZE_0];
    static uint8_t mp_digits_buffer_1[MP_DIGITS_BUFFER_SIZE_1];
    #if !defined(WOLFSSL_SP_ARM_CORTEX_M_ASM) && (defined(WOLFBOOT_SIGN_ECC256) || defined(WOLFBOOT_SIGN_ECC384) || defined(WOLFBOOT_SIGN_ECC521))
    static uint8_t mp_digits_buffer_2[MP_DIGITS_BUFFER_SIZE_2];
    static uint8_t mp_montgomery[MP_MONTGOMERY_SIZE];
    #elif defined(WOLFBOOT_SIGN_ECC384) || defined (WOLFBOOT_SIGN_ECC521)
    static uint8_t mp_montgomery[MP_MONTGOMERY_SIZE];
    #endif
#else
    /* TFM */
    #define MP_INT_TYPE_SIZE ((sizeof (fp_int)))
    #ifdef WOLFBOOT_SIGN_ECC256
        #define MP_SCHEME "TFM ECC256"
        #define MP_CURVE_SPECS_SIZE (MP_INT_TYPE_SIZE)
        #define MP_CURVE_FIELD_COUNT_SIZE (380)
        #define ECC_POINT_SIZE (228)
        #define MP_INT_BUFFER_SIZE (MP_INT_TYPE_SIZE * 6)
        #define MP_DIGIT_BUFFER_MONT_SIZE (sizeof(fp_digit)*(FP_SIZE + 1))
    #endif
    #ifdef WOLFBOOT_SIGN_ECC384
        #define MP_SCHEME "TFM ECC384"
        #define MP_CURVE_SPECS_SIZE (MP_INT_TYPE_SIZE)
        #define MP_CURVE_FIELD_COUNT_SIZE (380)
        #define ECC_POINT_SIZE (408)
        #define MP_INT_BUFFER_SIZE (MP_INT_TYPE_SIZE * 5)
        #define MP_INT_BUFFER_SIZE_1 (MP_INT_TYPE_SIZE * 6)
        #define MP_DIGIT_BUFFER_MONT_SIZE (sizeof(fp_digit)*(FP_SIZE + 1))
    #endif
    #ifdef WOLFBOOT_SIGN_ECC521
        #define MP_SCHEME "TFM ECC521"
        #define MP_CURVE_SPECS_SIZE (MP_INT_TYPE_SIZE)
        #define MP_CURVE_FIELD_COUNT_SIZE (380)
        #define ECC_POINT_SIZE (516)
        #define MP_INT_BUFFER_SIZE (MP_INT_TYPE_SIZE * 5)
        #define MP_INT_BUFFER_SIZE_1 (MP_INT_TYPE_SIZE * 6)
        #define MP_DIGIT_BUFFER_MONT_SIZE (sizeof(fp_digit)*(FP_SIZE + 1))
    #endif

    static uint8_t mp_curve_field_count[MP_CURVE_FIELD_COUNT_SIZE];
    static uint8_t mp_int_v[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_w[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_u1[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_u2[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_t[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_tmp0[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_tmp1[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_q[MP_INT_TYPE_SIZE * 5];
    static uint8_t ecc_point0[ECC_POINT_SIZE];
    static uint8_t ecc_point1[ECC_POINT_SIZE];
    static uint8_t ecc_point2[ECC_POINT_SIZE];
    static uint8_t ecc_point3[ECC_POINT_SIZE];
    static uint8_t ecc_point4[ECC_POINT_SIZE];
    static uint8_t ecc_point5[ECC_POINT_SIZE];
    static uint8_t mp_buffer0[MP_INT_BUFFER_SIZE];
    #ifdef MP_INT_BUFFER_SIZE_1
    static uint8_t mp_buffer1[MP_INT_BUFFER_SIZE_1];
    #endif
    static uint8_t mp_digits_buffer[MP_DIGIT_BUFFER_MONT_SIZE];
#endif

static uint8_t mp_curve_specs[MP_CURVE_SPECS_SIZE];




static uint32_t sha_block[HASH_BLOCK_SIZE];
static struct xmalloc_slot xmalloc_pool[] = {
#if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
    { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { (uint8_t *)mp_curve_specs, MP_CURVE_SPECS_SIZE, 0 },
#ifndef USE_FAST_MATH
    { (uint8_t *)mp_points_0, MP_POINT_SIZE * 2, 0 },
    #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
    { (uint8_t *)mp_points_1, MP_POINT_SIZE * 2, 0 },
        #if defined(WOLFBOOT_SIGN_ECC384) || defined(WOLFBOOT_SIGN_ECC521)
    { (uint8_t *)mp_montgomery, MP_MONTGOMERY_SIZE, 0 },
        #endif
    #else
    { (uint8_t *)mp_points_1, MP_POINT_SIZE * 3, 0 },
    { (uint8_t *)mp_digits_buffer_2, MP_DIGITS_BUFFER_SIZE_2, 0 },
    { (uint8_t *)mp_montgomery, MP_MONTGOMERY_SIZE, 0 },
    #endif
    { (uint8_t *)mp_points_2, MP_POINT_SIZE * (16 + 1), 0 },
    { (uint8_t *)mp_digits_buffer_0, MP_DIGITS_BUFFER_SIZE_0, 0},
    { (uint8_t *)mp_digits_buffer_1, MP_DIGITS_BUFFER_SIZE_1, 0},
    #ifndef WC_NO_CACHE_RESISTANT
    { (uint8_t *)mp_points_3, MP_POINT_SIZE, 0 },
    #endif
#else
    { mp_curve_field_count, MP_CURVE_FIELD_COUNT_SIZE, 0},
    { mp_int_v, MP_INT_TYPE_SIZE, 0},
    { mp_int_w, MP_INT_TYPE_SIZE, 0},
    { mp_int_u1, MP_INT_TYPE_SIZE, 0},
    { mp_int_u2, MP_INT_TYPE_SIZE, 0},
    { mp_int_t, MP_INT_TYPE_SIZE, 0},
    { mp_int_tmp0, MP_INT_TYPE_SIZE, 0},
    { mp_int_tmp1, MP_INT_TYPE_SIZE, 0},
    { mp_int_q, MP_INT_TYPE_SIZE * 5, 0},
    { ecc_point0, ECC_POINT_SIZE, 0},
    { ecc_point1, ECC_POINT_SIZE, 0},
    { ecc_point2, ECC_POINT_SIZE, 0},
    { ecc_point3, ECC_POINT_SIZE, 0},
    { ecc_point4, ECC_POINT_SIZE, 0},
    { ecc_point5, ECC_POINT_SIZE, 0},
    { mp_buffer0, MP_INT_BUFFER_SIZE, 0},
    #ifdef MP_INT_BUFFER_SIZE_1
    { mp_buffer1, MP_INT_BUFFER_SIZE_1, 0},
    #endif
    { mp_digits_buffer, MP_DIGIT_BUFFER_MONT_SIZE, 0},
#endif
    { NULL, 0, 0}
};

#elif defined WOLFBOOT_SIGN_ED25519

#define MP_SCHEME "ED25519"
static uint32_t sha_block[HASH_BLOCK_SIZE];
static uint32_t sha512_block[sizeof(word64) * 16];

static struct xmalloc_slot xmalloc_pool[] = {
#if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
    { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { (uint8_t *)sha512_block, sizeof(word64) * 16, 0 },
    { NULL, 0, 0}
};


#elif defined WOLFBOOT_SIGN_ED448

#include <wolfssl/wolfcrypt/ge_448.h>

#define MP_SCHEME "ED448"
#define GE448_WINDOW_BUF_SIZE 448

static uint32_t aslide[GE448_WINDOW_BUF_SIZE / sizeof(uint32_t)];
static uint32_t bslide[GE448_WINDOW_BUF_SIZE / sizeof(uint32_t)];
static ge448_p2 pi, p2;
static uint32_t sha_block[HASH_BLOCK_SIZE];

static struct xmalloc_slot xmalloc_pool[] = {
#if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
    { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { (uint8_t *)aslide, GE448_WINDOW_BUF_SIZE, 0 },
    { (uint8_t *)bslide, GE448_WINDOW_BUF_SIZE, 0 },
    { (uint8_t *)&pi, sizeof(ge448_p2), 0 },
    { (uint8_t *)&p2, sizeof(ge448_p2), 0},
    { NULL, 0, 0}
};




#elif defined(WOLFBOOT_SIGN_RSA2048) || defined(WOLFBOOT_SIGN_RSA4096) || \
      defined(WOLFBOOT_SIGN_RSA3072)

static uint32_t sha_block[HASH_BLOCK_SIZE];

#ifndef _LP64
#define ASNCHECK_BUF_SIZE (224)
#else
#define ASNCHECK_BUF_SIZE (320)
#endif
static uint8_t asncheck_buf[ASNCHECK_BUF_SIZE];

#ifndef USE_FAST_MATH
    #ifdef WOLFBOOT_SIGN_RSA2048
        #define MP_SCHEME "SP RSA2048"
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 64 * 5)
        #else
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 72 * 5)
            #define MPDIGIT_BUF1_SIZE (MP_DIGIT_SIZE * (72 * 4 + 3))
            static uint8_t mp_digit_buf1[MPDIGIT_BUF1_SIZE];
        #endif
    #elif defined WOLFBOOT_SIGN_RSA3072
        #define MP_SCHEME "SP RSA3072"
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 96 * 5)
        #else
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 106 * 5)
            #define MPDIGIT_BUF1_SIZE (MP_DIGIT_SIZE * (106 * 4 + 3))
            static uint8_t mp_digit_buf1[MPDIGIT_BUF1_SIZE];
        #endif


    #else
        #define MP_SCHEME "SP RSA4096"
        #ifdef WOLFSSL_SP_ARM_CORTEX_M_ASM
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 128 * 5)
        #else
            #define MPDIGIT_BUF0_SIZE (MP_DIGIT_SIZE * 142 * 5)
            #define MPDIGIT_BUF1_SIZE (MP_DIGIT_SIZE * (142 * 4 + 3))
            static uint8_t mp_digit_buf1[MPDIGIT_BUF1_SIZE];
        #endif
    #endif
    static uint8_t mp_digit_buf0[MPDIGIT_BUF0_SIZE];
    static struct xmalloc_slot xmalloc_pool[] = {
    #if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
        { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
    #endif
        { asncheck_buf, ASNCHECK_BUF_SIZE, 0 },
        { mp_digit_buf0, MPDIGIT_BUF0_SIZE, 0},
    #ifndef WOLFSSL_SP_ARM_CORTEX_M_ASM
        { mp_digit_buf1, MPDIGIT_BUF1_SIZE, 0},
    #endif
        { NULL, 0, 0}
    };
#else
    #define MP_SCHEME "TFM RSA"
    #define MP_INT_TYPE_SIZE (sizeof(mp_int))
    #define MP_MONT_REDUCE_BUF_SIZE (sizeof(fp_digit)*(FP_SIZE + 1))
    static uint8_t mp_int_buffer0[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_buffer1[MP_INT_TYPE_SIZE * 3];
    static uint8_t mp_int_buffer2[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_buffer3[MP_INT_TYPE_SIZE];
    static uint8_t mp_int_buffer4[MP_INT_TYPE_SIZE * 5];
    static uint8_t mp_mont_reduce_buffer[MP_MONT_REDUCE_BUF_SIZE];
    static struct xmalloc_slot xmalloc_pool[] = {
    #if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
        { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
    #endif
        { asncheck_buf, ASNCHECK_BUF_SIZE, 0 },
        { mp_int_buffer0, MP_INT_TYPE_SIZE, 0},
        { mp_int_buffer1, MP_INT_TYPE_SIZE * 3, 0},
        { mp_int_buffer2, MP_INT_TYPE_SIZE, 0},
        { mp_int_buffer3, MP_INT_TYPE_SIZE, 0},
        { mp_int_buffer4, MP_INT_TYPE_SIZE * 5, 0},
        { mp_mont_reduce_buffer, MP_MONT_REDUCE_BUF_SIZE, 0 },
        { NULL, 0, 0}
    };
#endif

#elif defined WOLFBOOT_NO_SIGN

#define MP_SCHEME "NONE"
static uint32_t sha_block[HASH_BLOCK_SIZE];
static struct xmalloc_slot xmalloc_pool[] = {
#if defined(WOLFBOOT_HASH_SHA256) || defined(WOLFBOOT_HASH_SHA384)
    { (uint8_t *)sha_block, HASH_BLOCK_SIZE * sizeof(uint32_t), 0 },
#endif
    { NULL, 0, 0}
};

#else
#   error "No cipher selected."
#endif

#ifdef WOLFBOOT_DEBUG_MALLOC
    static void dump_pool(void)
    {
        size_t i;
        for (i=0; i<sizeof(xmalloc_pool)/sizeof(struct xmalloc_slot); i++) {
            printf("Addr %p, Size %d, In Use %d\n",
                xmalloc_pool[i].addr,
                xmalloc_pool[i].size,
                xmalloc_pool[i].in_use);
        }
    }
#endif


void* XMALLOC(size_t n, void* heap, int type)
{
    int i = 0;

#ifdef WOLFBOOT_DEBUG_MALLOC
    static int detect_init = 0;
    if (detect_init++ == 0) {
        printf("MP_SCHEME %s\n", MP_SCHEME);
        dump_pool();
    }
    printf("MALLOC: Type %d, Size %zd", type, n);
#endif

    while (xmalloc_pool[i].addr) {
        if ((n == xmalloc_pool[i].size) &&
                (xmalloc_pool[i].in_use == 0)) {
            xmalloc_pool[i].in_use++;
        #ifdef WOLFBOOT_DEBUG_MALLOC
            printf(" Index %d, Ptr %p\n", i, xmalloc_pool[i].addr);
        #endif
            return xmalloc_pool[i].addr;
        }
        i++;
    }
    (void)heap;
    (void)type;
#ifdef WOLFBOOT_DEBUG_MALLOC
    printf(" OUT OF MEMORY!\n");
    dump_pool();
#endif
    return NULL;
}

void XFREE(void *ptr, void *heap, int type)
{
    int i = 0;
#ifdef WOLFBOOT_DEBUG_MALLOC
    printf("FREE: Type %d, Ptr %p\n", type, ptr);
#endif
    while (xmalloc_pool[i].addr) {
        if ((ptr == (void *)(xmalloc_pool[i].addr)) && xmalloc_pool[i].in_use) {
            xmalloc_pool[i].in_use = 0;
            return;
        }
        i++;
    }
    (void)heap;
    (void)type;
}
