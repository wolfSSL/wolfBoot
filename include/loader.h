/* loader.h
 *
 * Public key information for the signed images
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

#ifndef LOADER_H
#define LOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(WOLFBOOT_SIGN_ED25519)
    extern const unsigned char ed25519_pub_key[];
    extern unsigned int ed25519_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (64)
#elif defined(WOLFBOOT_SIGN_ED448)
    extern const unsigned char ed448_pub_key[];
    extern unsigned int ed448_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (114)
#elif defined(WOLFBOOT_SIGN_ECC256)
    extern const unsigned char ecc256_pub_key[];
    extern unsigned int ecc256_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (64)
#elif defined(WOLFBOOT_SIGN_ECC384)
    extern const unsigned char ecc384_pub_key[];
    extern unsigned int ecc384_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (96)
#elif defined(WOLFBOOT_SIGN_ECC521)
    extern const unsigned char ecc521_pub_key[];
    extern unsigned int ecc521_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (132)
#elif defined(WOLFBOOT_SIGN_RSA2048)
    extern const unsigned char rsa2048_pub_key[];
    extern unsigned int rsa2048_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (256)
#elif defined(WOLFBOOT_SIGN_RSA3072)
    extern const unsigned char rsa3072_pub_key[];
    extern unsigned int rsa3072_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (384)
#elif defined(WOLFBOOT_SIGN_RSA4096)
    extern const unsigned char rsa4096_pub_key[];
    extern unsigned int rsa4096_pub_key_len;
#   define IMAGE_SIGNATURE_SIZE (512)
/* In PQC methods the signature size is a function of
 * the parameters. Therefore IMAGE_SIGNATURE_SIZE is
 * set in options.mk from the .config file. */
#elif defined(WOLFBOOT_SIGN_LMS)
    extern const unsigned char lms_pub_key[];
    extern unsigned int lms_pub_key_len;
#elif defined(WOLFBOOT_SIGN_XMSS)
    extern const unsigned char xmss_pub_key[];
    extern unsigned int xmss_pub_key_len;
#elif defined(WOLFBOOT_SIGN_ML_DSA)
    extern const unsigned char ml_dsa_pub_key[];
    extern unsigned int ml_dsa_pub_key_len;
#elif !defined(WOLFBOOT_NO_SIGN)
#   error "No public key available for given signing algorithm."
#endif /* Algorithm selection */

#ifdef WOLFBOOT_SIGN_PRIMARY_ED25519
#define wolfBoot_verify_signature wolfBoot_verify_signature_ed25519
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_ED448
#define wolfBoot_verify_signature wolfBoot_verify_signature_ed448
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_RSA
#define wolfBoot_verify_signature wolfBoot_verify_signature_rsa
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_ECC
#define wolfBoot_verify_signature wolfBoot_verify_signature_ecc
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_LMS
#define wolfBoot_verify_signature wolfBoot_verify_signature_lms
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_XMSS
#define wolfBoot_verify_signature wolfBoot_verify_signature_xmss
#endif
#ifdef WOLFBOOT_SIGN_PRIMARY_ML_DSA
#define wolfBoot_verify_signature wolfBoot_verify_signature_ml_dsa
#endif

void wolfBoot_start(void);

#if defined(ARCH_ARM) && defined(WOLFBOOT_ARMORED)

/* attempt to jump 5 times to self, causing loop that cannot be glitched past */
#define wolfBoot_panic() \
    asm volatile("b ."); \
    asm volatile("b .-2"); \
    asm volatile("b .-4"); \
    asm volatile("b .-6"); \
    asm volatile("b .-8");

#elif defined(ARCH_SIM)
#include <stdlib.h>
#include <stdio.h>
static inline void wolfBoot_panic(void)
{
    fprintf(stderr, "wolfBoot: PANIC!\n");
    exit('P');
}
#elif defined UNIT_TEST
static int wolfBoot_panicked = 0;
static inline void wolfBoot_panic(void)
{
    fprintf(stderr, "wolfBoot: PANIC!\n");
    wolfBoot_panicked++;
}
#else
#include "printf.h"
static inline void wolfBoot_panic(void)
{
    wolfBoot_printf("wolfBoot: PANIC!\n");
    while(1)
        ;
}
#endif

#ifdef WOLFCRYPT_SECURE_MODE
void wcs_Init(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LOADER_H */
