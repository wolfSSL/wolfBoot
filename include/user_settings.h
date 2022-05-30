/* user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
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

#ifndef H_USER_SETTINGS_
#define H_USER_SETTINGS_

#include <target.h>

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define WOLFCRYPT_ONLY
#define SIZEOF_LONG_LONG 8

#ifdef USE_FAST_MATH
#   define WC_NO_HARDEN
#endif

/* ED25519 and SHA512 */
#ifdef WOLFBOOT_SIGN_ED25519
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define NO_ED25519_SIGN
#   define NO_ED25519_EXPORT
#   define WOLFSSL_SHA512
#   define USE_SLOW_SHA512
#   define NO_RSA
#   define NO_ASN
#endif

/* ED448 */
#ifdef WOLFBOOT_SIGN_ED448
#   define HAVE_ED448
#   define HAVE_ED448_VERIFY
#   define ED448_SMALL
#   define NO_ED448_SIGN
#   define NO_ED448_EXPORT
#   define NO_RSA
#   define NO_ASN
#   define WOLFSSL_SHA3
#   define WOLFSSL_SHAKE256
#endif

/* ECC and SHA256 */
#if defined (WOLFBOOT_SIGN_ECC256) ||\
    defined (WOLFBOOT_SIGN_ECC384) ||\
    defined (WOLFBOOT_SIGN_ECC521)
#   define HAVE_ECC
#   define ECC_TIMING_RESISTANT



/* Kinetis LTC support */
#   ifdef FREESCALE_USE_LTC
#      define LTC_MAX_ECC_BITS (256)
#      define LTC_MAX_INT_BYTES (128)
#      ifndef LTC_BASE
#      define LTC_BASE ((LTC_Type *)LTC0_BASE)
#      endif
#   endif

/* SP MATH */
#   ifndef USE_FAST_MATH
#       define WOLFSSL_SP
#       define WOLFSSL_SP_MATH
#       define WOLFSSL_SP_SMALL
#       define SP_WORD_SIZE 32
#       define WOLFSSL_HAVE_SP_ECC
#   endif

/* ECC options disabled to reduce size */
#   define NO_ECC_SIGN
#   define NO_ECC_EXPORT
#   define NO_ECC_DHE
#   define NO_ECC_KEY_EXPORT

/* Curve */
#   define NO_ECC192
#   define NO_ECC224
#ifdef WOLFBOOT_SIGN_ECC256
#   define HAVE_ECC256
#   define FP_MAX_BITS (256 + 32)
#   define NO_ECC384
#   define NO_ECC521
#elif defined WOLFBOOT_SIGN_ECC384
#   define HAVE_ECC384
#   define FP_MAX_BITS (1024 + 32)
#   define WOLFSSL_SP_384
#   define WOLFSSL_SP_NO_256
#   define NO_ECC256
#   define NO_ECC521
#elif defined WOLFBOOT_SIGN_ECC521
#   define HAVE_ECC521
#   define FP_MAX_BITS (544 + 32)
#   define NO_ECC256
#   define NO_ECC384
#endif

#   define NO_RSA
#   define NO_ASN
#endif

#ifdef WOLFBOOT_SIGN_RSA2048
#   define RSA_LOW_MEM
#   define WOLFSSL_RSA_VERIFY_INLINE
#   define WOLFSSL_RSA_VERIFY_ONLY
#   define WC_NO_RSA_OAEP
#   define FP_MAX_BITS (2048 * 2)
    /* sp math */
#   ifndef USE_FAST_MATH
#       define WOLFSSL_HAVE_SP_RSA
#       define WOLFSSL_SP
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_SP_MATH
#       define SP_WORD_SIZE 32
#       define WOLFSSL_SP_NO_3072
#       define WOLFSSL_SP_NO_4096
#   endif
#endif

#ifdef WOLFBOOT_SIGN_RSA3072
#   define RSA_LOW_MEM
#   define WOLFSSL_RSA_VERIFY_INLINE
#   define WOLFSSL_RSA_VERIFY_ONLY
#   define WC_NO_RSA_OAEP
#   define FP_MAX_BITS (3072 * 2)
    /* sp math */
#   ifndef USE_FAST_MATH
#       define WOLFSSL_HAVE_SP_RSA
#       define WOLFSSL_SP
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_SP_MATH
#       define SP_WORD_SIZE 32
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_4096
#   endif
#endif

#ifdef WOLFBOOT_SIGN_RSA4096
#   define RSA_LOW_MEM
#   define WOLFSSL_RSA_VERIFY_INLINE
#   define WOLFSSL_RSA_VERIFY_ONLY
#   define WC_NO_RSA_OAEP
#   define FP_MAX_BITS (4096 * 2)
    /* sp math */
#   ifndef USE_FAST_MATH
#       define WOLFSSL_HAVE_SP_RSA
#       define WOLFSSL_SP
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_SP_MATH
#       define SP_WORD_SIZE 32
#       define WOLFSSL_SP_4096
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_3072
#   endif
#endif

#ifdef WOLFBOOT_HASH_SHA3_384
#   define WOLFSSL_SHA3
#   define NO_SHA256
#endif

#ifdef WOLFBOOT_HASH_SHA384
#   define WOLFSSL_SHA384
#   define NO_SHA256
#endif

#ifdef EXT_ENCRYPTED
#   define HAVE_PWDBASED
#else
#   define NO_PWDBASED
#endif

/* Disables - For minimum wolfCrypt build */
#ifndef WOLFBOOT_TPM
#   if !defined(ENCRYPT_WITH_AES128) && !defined(ENCRYPT_WITH_AES256)
#       define NO_AES
#   endif
#   define NO_HMAC
#endif

#define NO_CMAC
#define NO_CODING
#define WOLFSSL_NO_PEM
#define NO_ASN_TIME
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
#define WC_NO_RNG
#define WC_NO_HASHDRBG
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_IGNORE_FILE_WARN
#define NO_ERROR_STRINGS

#define BENCH_EMBEDDED
#define NO_CRYPT_TEST
#define NO_CRYPT_BENCHMARK

#ifdef __QNX__
#   define WOLFSSL_HAVE_MIN
#   define WOLFSSL_HAVE_MAX
#endif


/* Memory model */
#ifndef WOLFBOOT_SMALL_STACK
#   ifdef WOLFSSL_SP_MATH
#       define WOLFSSL_SP_NO_MALLOC
#       define WOLFSSL_SP_NO_DYN_STACK
#   endif
#   define WOLFSSL_NO_MALLOC
#else
#   define WOLFSSL_SMALL_STACK
#endif


#endif /* !H_USER_SETTINGS_ */
