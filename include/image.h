/* image.h
 *
 * Functions to help with wolfBoot image header
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifndef IMAGE_H
#define IMAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "target.h"
#include "wolfboot/wolfboot.h"

#ifdef EXT_FLASH
#include "hal.h"
#endif

#if defined(EXT_ENCRYPTED) && (defined(__WOLFBOOT) || defined(UNIT_TEST))
#include "encrypt.h"
#endif

#ifndef WOLFBOOT_FLAGS_INVERT
#define SECT_FLAG_NEW      0x0F
#define SECT_FLAG_SWAPPING 0x07
#define SECT_FLAG_BACKUP   0x03
#define SECT_FLAG_UPDATED  0x00
#else
#define SECT_FLAG_NEW       0x00
#define SECT_FLAG_SWAPPING  0x08
#define SECT_FLAG_BACKUP    0x0c
#define SECT_FLAG_UPDATED   0x0f
#endif

#ifdef WOLFBOOT_SIGN_ED25519
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_ed25519
#endif
#ifdef WOLFBOOT_SIGN_ED448
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_ed448
#endif
#if defined (WOLFBOOT_SIGN_RSA2048) || \
    defined (WOLFBOOT_SIGN_RSA3072) || \
    defined (WOLFBOOT_SIGN_RSA4096) || \
    defined (WOLFBOOT_SIGN_RSA2048ENC) || \
    defined (WOLFBOOT_SIGN_RSA3072ENC) || \
    defined (WOLFBOOT_SIGN_RSA4096ENC)
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_rsa
#endif
#if defined (WOLFBOOT_SIGN_ECC256) || \
        defined (WOLFBOOT_SIGN_ECC384) || \
        defined (WOLFBOOT_SIGN_ECC521)
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_ecc
#endif
#if defined(WOLFBOOT_SIGN_LMS)
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_lms
#endif
#if defined(WOLFBOOT_SIGN_XMSS )
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_xmss
#endif
#ifdef WOLFBOOT_SIGN_ML_DSA
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_ml_dsa
#endif

#ifdef WOLFBOOT_SIGN_SECONDARY_ED25519
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_ed25519
#endif
#ifdef WOLFBOOT_SIGN_SECONDARY_ED448
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_ed448
#endif
#if defined (WOLFBOOT_SIGN_SECONDARY_RSA2048) || \
    defined (WOLFBOOT_SIGN_SECONDARY_RSA3072) || \
    defined (WOLFBOOT_SIGN_SECONDARY_RSA4096) || \
    defined (WOLFBOOT_SIGN_SECONDARY_RSA2048ENC) || \
    defined (WOLFBOOT_SIGN_SECONDARY_RSA3072ENC) || \
    defined (WOLFBOOT_SIGN_SECONDARY_RSA4096ENC)
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_rsa
#endif
#if defined (WOLFBOOT_SIGN_SECONDARY_ECC256) || \
        defined (WOLFBOOT_SIGN_SECONDARY_ECC384) || \
        defined (WOLFBOOT_SIGN_SECONDARY_ECC521)
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_ecc
#endif
#ifdef WOLFBOOT_SIGN_SECONDARY_LMS
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_lms
#endif
#ifdef WOLFBOOT_SIGN_SECONDARY_XMSS
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_xmss
#endif
#ifdef WOLFBOOT_SIGN_SECONDARY_ML_DSA
#define wolfBoot_verify_signature_secondary wolfBoot_verify_signature_ml_dsa
#endif

#if defined(WOLFBOOT_TPM) && defined (WOLFBOOT_TPM_VERIFY)
#undef wolfBoot_verify_signature_primary
#define wolfBoot_verify_signature_primary wolfBoot_verify_signature_tpm
#endif

/* Validate sector size is larger than image header size */
#if defined(WOLFBOOT_SECTOR_SIZE) && defined(IMAGE_HEADER_SIZE) && \
    (WOLFBOOT_SECTOR_SIZE < IMAGE_HEADER_SIZE)
#error WOLFBOOT_SECTOR_SIZE must be larger than IMAGE_HEADER_SIZE
#endif


#if (defined(WOLFBOOT_ARMORED) && defined(__WOLFBOOT))
#if !defined(ARCH_ARM) || (!defined(__GNUC__) && \
    !(defined(__ICCARM__) && defined(__IAR_SYSTEMS_ICC__)))
#   error WOLFBOOT_ARMORED only available for ARM with IAR or gcc compilers
#endif

#if defined(__GNUC__)
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
#else
#define likely(x)      (x)
#define unlikely(x)    (x)
#endif

struct wolfBoot_image {
    uint8_t *hdr;
#ifdef EXT_FLASH
    uint8_t *hdr_cache;
#endif
    uint8_t *trailer;
    uint8_t *sha_hash;
    uint8_t *fw_base;
    uint32_t fw_size;
    uint32_t part;
    uint32_t hdr_ok;
    uint32_t canary_FEED4567;
    uint32_t signature_ok;
    uint32_t canary_FEED6789;
    uint32_t not_signature_ok;
    uint32_t canary_FEED89AB;
    uint32_t sha_ok;
    uint32_t not_ext; /* image is no longer external */
};


/**
 * This function sets the flag that indicates the signature is valid for the
 * wolfBoot_image.
 *
 * With ARMORED setup, the flag is redundant, and the information is wrapped in
 * between canary variables, to mitigate attacks based on memory corruptions.
 */
static void __attribute__((noinline)) wolfBoot_image_confirm_signature_ok(
    struct wolfBoot_image *img)
{
    img->canary_FEED4567 = 0xFEED4567UL;
    img->signature_ok = 1UL;
    img->canary_FEED6789 = 0xFEED6789UL;
    img->not_signature_ok = ~(1UL);
    img->canary_FEED89AB = 0xFEED89ABUL;
}

static void __attribute__((noinline)) wolfBoot_image_clear_signature_ok(
	struct wolfBoot_image *img)
{
	img->canary_FEED4567 = 0xFEED4567UL;
	img->signature_ok = 0UL;
	img->canary_FEED6789 = 0xFEED6789UL;
	img->not_signature_ok = 1UL;
	img->canary_FEED89AB = 0xFEED89ABUL;
}

/**
 * Final sanity check, performed just before do_boot, or before starting an
 * update that has been verified.
 *
 * This procedure detects if any of the previous checks has been skipped.
 * If any of the required flags does not match the expected value, wolfBoot
 * panics.
 */
#define PART_SANITY_CHECK(p) \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading hdr_ok flag, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->hdr_ok):"r2"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading hdr_ok flag, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->sha_ok):"r2"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading signature_ok flag, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->signature_ok):"r2"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0"); \
    asm volatile("mov r2, #0"); \
    asm volatile("mov r2, #0"); \
    asm volatile("mov r2, #0"); \
    asm volatile("mov r2, #0"); \
    /* Loading ~(signature_ok) flag, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->not_signature_ok):"r2"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r2, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading canary value, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->canary_FEED6789):"r2"); \
    asm volatile("mov r0, %0" ::"r"(0xFEED6789):"r0"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading canary value, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->canary_FEED4567):"r2"); \
    asm volatile("mov r0, %0" ::"r"(0xFEED4567):"r0"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-12"); \
    /* Redundant set of r2=0 */ \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    asm volatile("mov r2, #0":::"r2"); \
    /* Loading canary value, verifying */ \
    asm volatile("mov r2, %0" ::"r"((p)->canary_FEED89AB):"r2"); \
    asm volatile("mov r0, %0" ::"r"(0xFEED89AB):"r0"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-4"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-8"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("cmp r2, r0":::"cc"); \
    asm volatile("bne .-12") \

/**
 * First part of RSA verification. Ensure that the function is called by
 * double checking its return value contains a valid
 * len (>= WOLFBOOT_SHA_DIGEST_SIZE).
 *
 */
#define RSA_VERIFY_FN(ret,fn,...) \
    { \
        /* Redundant set of r0=0 */ \
        asm volatile("mov r0, #0":::"r0"); \
        asm volatile("mov r0, #0":::"r0"); \
        asm volatile("mov r0, #0":::"r0"); \
        /* Call the function */ \
        int tmp_ret = fn(__VA_ARGS__); \
        ret = -1; \
        /* Redundant set of r2=SHA_DIGEST_SIZE */ \
        asm volatile("mov r2, %0" ::"r"(WOLFBOOT_SHA_DIGEST_SIZE):"r2"); \
        asm volatile("mov r2, %0" ::"r"(WOLFBOOT_SHA_DIGEST_SIZE):"r2"); \
        asm volatile("mov r2, %0" ::"r"(WOLFBOOT_SHA_DIGEST_SIZE):"r2"); \
        /* Redundant check for fn() return value >= r2 */ \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("blt nope"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("blt nope"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("blt nope"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("cmp r0, r2":::"cc"); \
        asm volatile("blt nope"); \
        /* Return value is set here in case of success */ \
        ret = tmp_ret; \
        asm volatile("nope:"); \
        asm volatile("nop"); \
    }

/**
 * Second part of RSA verification.
 *
 * Compare the digest twice, then confirm via
 * wolfBoot_image_confirm_signature_ok();
 */
#define RSA_VERIFY_HASH(img,digest) \
    { \
        volatile int compare_res; \
        if (!img || !digest)    \
            asm volatile("b hnope"); \
        /* Redundant set of r0=50*/ \
        asm volatile("mov r0, #50":::"r0"); \
        asm volatile("mov r0, #50":::"r0"); \
        asm volatile("mov r0, #50":::"r0"); \
        compare_res = XMEMCMP(digest, img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE); \
        /* Redundant checks that ensure the function actually returned 0 */ \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope":::"cc"); \
        asm volatile("cmp r0, #0"); \
        asm volatile("cmp r0, #0"); \
        asm volatile("cmp r0, #0"); \
        asm volatile("bne hnope":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        /* Repeat memcmp call */ \
        compare_res = XMEMCMP(digest, img->sha_hash, WOLFBOOT_SHA_DIGEST_SIZE); \
        compare_res; \
        /* Redundant checks that ensure the function actually returned 0 */ \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("cmp r0, #0":::"cc"); \
        asm volatile("bne hnope"); \
        /* Confirm that the signature is OK */ \
        wolfBoot_image_confirm_signature_ok(img); \
        asm volatile("hnope:"); \
        asm volatile("nop"); \
    }

/**
 * ECC / Ed / PQ signature verification.
 * Those verify functions set an additional value 'p_res'
 * which is passed as a pointer.
 *
 * Ensure that the verification function has been called, and then
 * set the return value accordingly.
 *
 * Double check by reading the value in p_res from memory a few times.
 */
#if defined(__GNUC__)

#define VERIFY_FN(img,p_res,fn,...) \
    /* Redundant set of r0=50*/ \
    asm volatile("mov r0, #50":::"r0"); \
    asm volatile("mov r0, #50":::"r0"); \
    asm volatile("mov r0, #50":::"r0"); \
    /* Call the verify function */ \
    fn(__VA_ARGS__); \
    /* Redundant checks that ensure the function actually returned 0 */ \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("cmp r0, #0":::"cc"); \
    asm volatile("bne nope"); \
    /* Check that res = 1, a few times, reading the value from memory */ \
    asm volatile("ldr r2, [%0]" ::"r"(p_res)); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("mvn r3, r2":::"r3"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("ldr r2, [%0]" ::"r"(p_res)); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("mvn r3, r2":::"r3"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("ldr r2, [%0]" ::"r"(p_res)); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("mvn r3, r2":::"r3"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("ldr r2, [%0]" ::"r"(p_res)); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("cmp r2, #1":::"cc"); \
    asm volatile("bne nope"); \
    asm volatile("mvn r3, r2":::"r3"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("cmp r3, #0xFFFFFFFE":::"cc"); \
    asm volatile("bne nope"); \
    /* Confirm that the signature is OK */ \
    wolfBoot_image_confirm_signature_ok(img); \
    asm volatile("nope:"); \
    asm volatile("nop") \

#elif defined(__ICCARM__) && defined(__IAR_SYSTEMS_ICC__)

#define VERIFY_FN(img, p_res, fn, ...) \
    do { \
        __asm volatile( \
            "mov r0, #50\n" \
            "mov r0, #50\n" \
            "mov r0, #50\n" \
            : /* No output operands */ \
            : /* No input operands */ \
            : "r0" /* Clobbered registers */ \
        ); \
        void (*confirm_func)(struct wolfBoot_image *) = \
            wolfBoot_image_confirm_signature_ok; \
        fn(__VA_ARGS__); \
        __asm volatile( \
            "cmp r0, #0\n" \
            "cmp r0, #0\n" \
            "cmp r0, #0\n" \
            "beq 30f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "30:\n" \
            "ldr r2, [%0]\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "beq 31f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "31:\n" \
            "mvn r3, r2\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "beq 32f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "32:\n" \
            "ldr r2, [%0]\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "beq 33f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "33:\n" \
            "mvn r3, r2\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "beq 34f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "34:\n" \
            "ldr r2, [%0]\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "beq 35f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "35:\n" \
            "mvn r3, r2\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "beq 36f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "36:\n" \
            "ldr r2, [%0]\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "cmp r2, #1\n" \
            "beq 37f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "37:\n" \
            "mvn r3, r2\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "cmp r3, #0xFFFFFFFE\n" \
            "beq 38f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "38:\n" \
            /* Load 'img' into r0 (first argument to the function) */ \
            "mov r0, %1\n" \
            /* Load the function pointer into r3 */ \
            "mov r3, %2\n" \
            "blx r3\n"\
            "b 2f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "1:\n" \
            "nop\n" \
            "2:\n" \
            : /* No output operands */ \
            : "r"(p_res), "r"(img), "r"(confirm_func) /* Input operands */ \
            : "r0", "r2", "r3", "lr" /* Clobbered registers */ \
        ); \
    } while (0)
#endif


/**
 * This macro is only invoked after a successful update version check, prior to
 * initiating the update installation.
 *
 * At this point, wolfBoot thinks that the version check has been successful.
 *
 *
 * The fallback flag (checked with redundancy) causes wolfBoot to skip the
 * redundant version checks.
 *
 * The redundant checks here ensure that the image version is read twice per
 * each partition, and the two return values are the same.
 *
 *
 * The comparison is also redundant, causing wolfBoot to panic if the update
 * version is not strictly greater than the current one.
 *
 */
#if defined(__GNUC__)

#define VERIFY_VERSION_ALLOWED(fb_ok) \
    asm volatile( \
        "push {r4, r5, r6, r7}\n" \
        "mov r0, #0\n" \
        "mov r4, #1\n" \
        "mov r5, #0\n" \
        "mov r6, #2\n" \
        "mov r7, #0\n" \
        "mov r0, #0\n" \
        "mov r4, #1\n" \
        "mov r5, #0\n" \
        "mov r6, #2\n" \
        "mov r7, #0\n" \
        "mov r0, %0\n" \
        "mov r4, %0\n" \
        "cmp r0, #1\n" \
        "cmp r0, #1\n" \
        "cmp r0, #1\n" \
        "beq 20f\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "20:\n" \
        "cmp r4, #1\n" \
        "cmp r4, #1\n" \
        "cmp r4, #1\n" \
        "beq 21f\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "21:\n" \
        "cmp r0, r4\n" \
        "cmp r0, r4\n" \
        "cmp r0, r4\n" \
        "beq 22f\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "22:\n" \
        "cmp r0, #1\n" \
        "cmp r0, #1\n" \
        "cmp r0, #1\n" \
        "beq 23f\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "bne do_check\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "23:\n" \
        "b end_check\n" \
        "do_check:\n" \
        "mov r0, #1\n" \
        "mov r0, #1\n" \
        "mov r0, #1\n" \
        "bl wolfBoot_get_image_version\n" \
        "mov r5, r0\n" \
        "mov r5, r0\n" \
        "mov r5, r0\n" \
        "mov r0, #1\n" \
        "mov r0, #1\n" \
        "mov r0, #1\n" \
        "bl wolfBoot_get_image_version\n" \
        "mov r7, r0\n" \
        "mov r7, r0\n" \
        "mov r7, r0\n" \
        "cmp r5, r7\n" \
        "cmp r5, r7\n" \
        "cmp r5, r7\n" \
        "beq 24f\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "24:\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "bl wolfBoot_get_image_version\n" \
        "mov r4, r0\n" \
        "mov r4, r0\n" \
        "mov r4, r0\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "bl wolfBoot_get_image_version\n" \
        "mov r6, r0\n" \
        "mov r6, r0\n" \
        "mov r6, r0\n" \
        "cmp r4, r6\n" \
        "cmp r4, r6\n" \
        "cmp r4, r6\n" \
        "beq 25f\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "bne ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "25:\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "mov r0, #0\n" \
        "cmp r4, r5\n" \
        "cmp r4, r5\n" \
        "cmp r4, r5\n" \
        "blo 26f\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "26:\n" \
        "cmp r6, r7\n" \
        "cmp r6, r7\n" \
        "cmp r6, r7\n" \
        "blo 27f\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "27:\n" \
        "cmp r4, r5\n" \
        "cmp r4, r5\n" \
        "cmp r4, r5\n" \
        "blo 28f\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "28:\n" \
        "cmp r6, r7\n" \
        "cmp r6, r7\n" \
        "cmp r6, r7\n" \
        "blo 29f\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "bhs ver_panic\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "29:\n" \
        "b end_check\n" \
        "ver_panic:\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "b .\n" \
        "end_check:\n" \
        "pop {r4, r5, r6, r7}\n" \
        : \
        : "r"(fb_ok) \
        : "r0", "r4", "r5", "r6", "r7", "lr", "cc", "memory" \
    )

#elif defined(__ICCARM__) && defined(__IAR_SYSTEMS_ICC__)

#define VERIFY_VERSION_ALLOWED(fb_ok) \
    do { \
        __asm volatile( \
            "push {r4, r5, r6, r7}\n" \
            "mov r0, #0\n" \
            "mov r4, #1\n" \
            "mov r5, #0\n" \
            "mov r6, #2\n" \
            "mov r7, #0\n" \
            "mov r0, #0\n" \
            "mov r4, #1\n" \
            "mov r5, #0\n" \
            "mov r6, #2\n" \
            "mov r7, #0\n" \
            "mov r0, %0\n" \
            "mov r4, %0\n" \
            "cmp r0, #0\n" \
            "cmp r0, #0\n" \
            "cmp r0, #0\n" \
            "beq 4f\n" \
            "bne 6f\n" \
            "bne 6f\n" \
            "bne 6f\n" \
            "bne 6f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "6:\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "beq 4f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "4:\n" \
            "cmp r4, #0\n" \
            "cmp r4, #0\n" \
            "cmp r4, #0\n" \
            "beq 5f\n" \
            "bne 7f\n" \
            "bne 7f\n" \
            "bne 7f\n" \
            "bne 7f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "7:\n" \
            "cmp r4, #1\n" \
            "cmp r4, #1\n" \
            "cmp r4, #1\n" \
            "beq 5f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "5:\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "beq 8f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "8:\n" \
            "cmp r4, #1\n" \
            "cmp r4, #1\n" \
            "cmp r4, #1\n" \
            "beq 9f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "9:\n" \
            "cmp r0, r4\n" \
            "cmp r0, r4\n" \
            "cmp r0, r4\n" \
            "beq 10f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "10:\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "cmp r0, #1\n" \
            "beq 11f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "bne 1f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "11:\n" \
            "b 2f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "1:\n" \
            "mov r0, #1\n" \
            "mov r0, #1\n" \
            "mov r0, #1\n" \
            "bl wolfBoot_get_image_version\n" \
            "mov r5, r0\n" \
            "mov r5, r0\n" \
            "mov r5, r0\n" \
            "mov r0, #1\n" \
            "mov r0, #1\n" \
            "mov r0, #1\n" \
            "bl wolfBoot_get_image_version\n" \
            "mov r7, r0\n" \
            "mov r7, r0\n" \
            "mov r7, r0\n" \
            "cmp r5, r7\n" \
            "cmp r5, r7\n" \
            "cmp r5, r7\n" \
            "beq 12f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "12:\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "bl wolfBoot_get_image_version\n" \
            "mov r4, r0\n" \
            "mov r4, r0\n" \
            "mov r4, r0\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "bl wolfBoot_get_image_version\n" \
            "mov r6, r0\n" \
            "mov r6, r0\n" \
            "mov r6, r0\n" \
            "cmp r4, r6\n" \
            "cmp r4, r6\n" \
            "cmp r4, r6\n" \
            "beq 13f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "bne 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "13:\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "mov r0, #0\n" \
            "cmp r4, r5\n" \
            "cmp r4, r5\n" \
            "cmp r4, r5\n" \
            "blo 14f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "14:\n" \
            "cmp r6, r7\n" \
            "cmp r6, r7\n" \
            "cmp r6, r7\n" \
            "blo 15f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "15:\n" \
            "cmp r4, r5\n" \
            "cmp r4, r5\n" \
            "cmp r4, r5\n" \
            "blo 16f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "16:\n" \
            "cmp r6, r7\n" \
            "cmp r6, r7\n" \
            "cmp r6, r7\n" \
            "blo 17f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "bhs 3f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "17:\n" \
            "b 2f\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "3:\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "b .\n" \
            "2:\n" \
            "pop {r4, r5, r6, r7}\n" \
            : /* No output operands */ \
            : "r"(fb_ok) /* Input operands */ \
            : "r0", "r4", "r5", "r6", "r7" /* Clobbered registers */ \
        ); \
    } while (0)
#endif


#define CONFIRM_MASK_VALID(id, mask) \
    asm volatile("mov r1, %0" :: "r"(id):"r1");  \
    /* id &= 0x0F */ \
    asm volatile("and.w r1, r1, #15":::"r1"); \
    asm volatile("mov r0, %0" :: "r"(mask):"r0"); \
    asm volatile("movs r2, #1":::"r2"); \
    asm volatile("lsls r2, r1":::"r2","cc"); \
    asm volatile("ands r2, r0":::"r2","cc"); \
    asm volatile("movs r0, #1":::"cc"); \
    asm volatile("lsls r0, r1":::"r0","cc"); \
    asm volatile("cmp r0, r2"); \
    asm volatile("cmp r0, r2"); \
    asm volatile("cmp r0, r2"); \
    asm volatile("bne ."); \
    asm volatile("mov r0, %0" :: "r"(mask)); \
    asm volatile("movs r2, #1":::"r2"); \
    asm volatile("lsls r2, r1":::"r2", "cc"); \
    asm volatile("ands r2, r0":::"r2", "cc"); \
    asm volatile("movs r0, #1":::"r0"); \
    asm volatile("lsls r0, r1":::"r0", "cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("bne ."); \
    asm volatile("mov r0, %0" :: "r"(mask):"r0"); \
    asm volatile("movs r2, #1":::"r2"); \
    asm volatile("lsls r2, r1":::"r2", "cc"); \
    asm volatile("ands r2, r0":::"r2", "cc"); \
    asm volatile("movs r0, #1":::"r0"); \
    asm volatile("lsls r0, r1":::"r0", "cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("cmp r0, r2":::"cc"); \
    asm volatile("bne ."); \

#else

struct wolfBoot_image {
    uint8_t *hdr;
#ifdef EXT_FLASH
    uint8_t *hdr_cache;
#endif
    uint8_t *trailer;
    uint8_t *sha_hash;
    uint8_t *fw_base;
    uint32_t fw_size;
    uint8_t part;
    uint8_t hdr_ok : 1;
    uint8_t signature_ok : 1;
    uint8_t sha_ok : 1;
    uint8_t not_ext : 1; /* image is no longer external */
};


/* do not warn if this is not used */
static void UNUSEDFUNCTION wolfBoot_image_confirm_signature_ok(
    struct wolfBoot_image *img)
{
    img->signature_ok = 1;
}
static void UNUSEDFUNCTION wolfBoot_image_clear_signature_ok(
    struct wolfBoot_image *img)
{
	img->signature_ok = 0;
}

#define likely(x) (x)
#define unlikely(x) (x)

#define VERIFY_FN(img,p_res,fn,...) {\
    int ret = fn(__VA_ARGS__); \
    if ((ret == 0) && (*p_res == 1)) \
        wolfBoot_image_confirm_signature_ok(img); \
    }

#define RSA_VERIFY_FN(ret,fn,...) \
    ret = fn(__VA_ARGS__);

#define RSA_VERIFY_HASH(img,digest) \
    if (XMEMCMP(img->sha_hash, digest, WOLFBOOT_SHA_DIGEST_SIZE) == 0) \
        wolfBoot_image_confirm_signature_ok(img);

#define PART_SANITY_CHECK(p) \
    if (((p)->hdr_ok != 1) || ((p)->sha_ok != 1) || ((p)->signature_ok != 1)) \
        wolfBoot_panic()

#define CONFIRM_MASK_VALID(id, mask) \
    if ((mask & (1UL << id)) != (1UL << id)) \
        wolfBoot_panic()

#define VERIFY_VERSION_ALLOWED(fb_ok) do{} while(0) /* okay */

#endif

/* Defined in image.c */
int wolfBoot_open_image(struct wolfBoot_image *img, uint8_t part);
#ifdef EXT_FLASH
int wolfBoot_open_image_external(struct wolfBoot_image* img, uint8_t part, uint8_t* addr);
#endif
int wolfBoot_open_image_address(struct wolfBoot_image* img, uint8_t* image);
#ifdef WOLFBOOT_SELF_HEADER
int wolfBoot_open_self(struct wolfBoot_image *img);
int wolfBoot_open_self_address(struct wolfBoot_image *img, uint8_t *hdr,
    uint8_t *image);
#endif
int wolfBoot_verify_integrity(struct wolfBoot_image *img);
int wolfBoot_verify_authenticity(struct wolfBoot_image *img);
int wolfBoot_set_partition_state(uint8_t part, uint8_t newst);
int wolfBoot_get_update_sector_flag(uint16_t sector, uint8_t *flag);
int wolfBoot_set_update_sector_flag(uint16_t sector, uint8_t newflag);

#ifdef WOLFBOOT_ELF_FLASH_SCATTER
/* Support for ELF scatter/gather format */
int wolfBoot_load_flash_image_elf(int part, unsigned long* entry_out,
                                  int ext_flash);
int wolfBoot_check_flash_image_elf(uint8_t part, unsigned long* entry_out);
#endif

uint8_t* wolfBoot_peek_image(struct wolfBoot_image *img, uint32_t offset,
        uint32_t* sz);

/* get header type for image */
uint16_t wolfBoot_get_header(struct wolfBoot_image *img, uint16_t type, uint8_t **ptr);

/* Find the key slot ID based on the SHA hash of the key. */
int keyslot_id_by_sha(const uint8_t *hint);

#ifdef EXT_FLASH
# ifdef PART_BOOT_EXT
#  define BOOT_EXT 1
# else
#  define BOOT_EXT 0
# endif
# ifdef PART_UPDATE_EXT
#  define UPDATE_EXT 1
# else
#  define UPDATE_EXT 0
# endif
# ifdef PART_SWAP_EXT
#  define SWAP_EXT 1
# else
#  define SWAP_EXT 0
# endif
# if defined(WOLFBOOT_SELF_HEADER_EXT)
#  define SELF_HEADER_EXT 1
# else
#  define SELF_HEADER_EXT 0
# endif
# define PARTN_IS_EXT(pn) \
    ((pn == PART_BOOT || pn == PART_DTS_BOOT) ? BOOT_EXT: \
        ((pn == PART_UPDATE || pn == PART_DTS_UPDATE) ? UPDATE_EXT : \
            ((pn == PART_SWAP) ? SWAP_EXT : \
                 ((pn == PART_SELF) ? SELF_HEADER_EXT : 0))))
# define PART_IS_EXT(x) (!(x)->not_ext) && PARTN_IS_EXT(((x)->part))


#if defined(EXT_ENCRYPTED) && (defined(__WOLFBOOT) || defined(UNIT_TEST))
#define ext_flash_check_write ext_flash_encrypt_write
#define ext_flash_check_read ext_flash_decrypt_read
#else
#define ext_flash_check_write ext_flash_write
#define ext_flash_check_read ext_flash_read
#endif

static inline int wb_flash_erase(struct wolfBoot_image *img, uint32_t off,
    uint32_t size)
{
    if (PART_IS_EXT(img))
        return ext_flash_erase((uintptr_t)(img->hdr) + off, size);
    else
        return hal_flash_erase((uintptr_t)(img->hdr) + off, size);
}

static inline int wb_flash_write(struct wolfBoot_image *img, uint32_t off,
    const void *data, uint32_t size)
{
    if (PART_IS_EXT(img))
        return ext_flash_check_write((uintptr_t)(img->hdr) + off, data, size);
    else
        return hal_flash_write((uintptr_t)(img->hdr) + off, data, size);
}

static inline int wb_flash_write_verify_word(struct wolfBoot_image *img,
    uint32_t off, uint32_t word)
{
    int ret;
    volatile uint32_t copy;
    if (PART_IS_EXT(img))
    {
        ext_flash_check_read((uintptr_t)(img->hdr) + off, (void *)&copy,
            sizeof(uint32_t));
        while (copy != word) {
            ret = ext_flash_check_write((uintptr_t)(img->hdr) + off,
                (void *)&word, sizeof(uint32_t));
            if (ret < 0)
                return ret;
            ext_flash_check_read((uintptr_t)(img->hdr) + off, (void *)&copy,
                sizeof(uint32_t));
        }
    } else {
        volatile uint32_t *pcopy = (volatile uint32_t*)(img->hdr + off);
        while(*pcopy != word) {
            hal_flash_write((uintptr_t)pcopy, (void *)&word, sizeof(uint32_t));
        }
    }
    return 0;
}


#else

# define SWAP_EXT (0)
# define BOOT_EXT (0)
# define UPDATE_EXT (0)
# define PART_IS_EXT(x) (0)
# define PARTN_IS_EXT(x) (0)
# define wb_flash_erase(im, of, siz) \
    hal_flash_erase(((uintptr_t)(((im)->hdr)) + of), siz)
# define wb_flash_write(im, of, dat, siz) \
    hal_flash_write(((uintptr_t)((im)->hdr)) + of, dat, siz)

#endif /* EXT_FLASH */

/* -- Image Formats -- */
/* Legacy U-Boot Image */
#define UBOOT_IMG_HDR_MAGIC 0x56190527UL
#define UBOOT_IMG_HDR_SZ    64

/* --- Flattened Device Tree Blob */
#ifdef MMU
#include "fdt.h"
#endif

#ifndef EXT_ENCRYPTED
#define WOLFBOOT_MAX_SPACE (WOLFBOOT_PARTITION_SIZE - \
    (TRAILER_SKIP + sizeof(uint32_t) + \
    (WOLFBOOT_PARTITION_SIZE + 1 / (WOLFBOOT_SECTOR_SIZE * 8))))
#else
#define WOLFBOOT_MAX_SPACE (WOLFBOOT_PARTITION_SIZE - ENCRYPT_TMP_SECRET_OFFSET)
#endif

#ifdef __cplusplus
}
#endif

#endif /* !IMAGE_H */
