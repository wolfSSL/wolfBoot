/* user_settings.h
 *
 * wolfCrypt build settings for signing tool
 * Enabled via WOLFSSL_USER_SETTINGS.
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

#ifndef H_USER_SETTINGS_
#define H_USER_SETTINGS_

#include <stdint.h>

/* System */
#define SINGLE_THREADED
#define WOLFCRYPT_ONLY

/* Math */
#if 0
    #define USE_FAST_MATH
    #define FP_MAX_BITS (4096 * 2)
#else
    #define WOLFSSL_SP_MATH
    #define WOLFSSL_HAVE_SP_ECC
    #define WOLFSSL_SP_384
    #define WOLFSSL_SP_521
    #define WOLFSSL_HAVE_SP_RSA
    #define WOLFSSL_SP_4096
#endif

#define TFM_TIMING_RESISTANT

/* ECC */
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define ECC_USER_CURVES
#undef  NO_ECC256
#define HAVE_ECC384
#define HAVE_ECC521

/* ED25519 */
#define HAVE_ED25519

/* ED448 */
#define HAVE_ED448
#define WOLFSSL_SHAKE256

/* RSA */
#define HAVE_RSA
#define WC_RSA_BLINDING
#define WOLFSSL_KEY_GEN

/* Hashing */
#define WOLFSSL_SHA512 /* Required for ED25519 */
#define WOLFSSL_SHA384 /* Required for ED25519 */
#define WOLFSSL_SHA3
#undef  NO_SHA256

/* ML-DSA (dilithium) */
#if defined(WOLFBOOT_SIGN_ML_DSA)
#   define HAVE_DILITHIUM
#   define WOLFSSL_WC_DILITHIUM
#   define WOLFSSL_EXPERIMENTAL_SETTINGS
    /* Wolfcrypt builds ML-DSA (dilithium) to the FIPS 204 final
     * standard by default. Uncomment this if you want the draft
     * version instead. */
    #if 0
    #define WOLFSSL_DILITHIUM_FIPS204_DRAFT
    #endif
    /* dilithium needs these sha functions. */
#   define WOLFSSL_SHAKE128
#endif /* WOLFBOOT_SIGN_ML_DSA */

/* ASN */
#define WOLFSSL_ASN_TEMPLATE

/* Chacha stream cipher */
#define HAVE_CHACHA

/* AES */
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_DIRECT

/* Disables */
#define NO_CMAC
#define NO_HMAC
#define NO_RC4
#define NO_SHA
#define NO_DH
#define NO_DSA
#define NO_MD4
#define NO_RABBIT
#define NO_MD5
#define NO_SIG_WRAPPER
#define NO_CERT
#define NO_SESSION_CACHE
#define NO_HC128
#define NO_DES3
#define NO_PWDBASED
#define NO_WRITEV
#define NO_FILESYSTEM
#define NO_OLD_RNGNAME
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_IGNORE_FILE_WARN

#define BENCH_EMBEDDED
#define NO_CRYPT_TEST
#define NO_CRYPT_BENCHMARK

#ifdef DEBUG_WOLFSSL
    #define XSNPRINTF snprintf
#else
    #define XSNPRINTF /* not used */
#endif

#endif /* !H_USER_SETTINGS_ */
