/* user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
 *
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

/* ECC and SHA256 */
#ifdef WOLFBOOT_SIGN_ECC256
#   define HAVE_ECC
#   define ECC_TIMING_RESISTANT
#   undef USE_FAST_MATH
#   define FP_MAX_BITS (256 + 32)


/* Kinetis LTC support */
#   ifdef FREESCALE_USE_LTC
#      define LTC_MAX_ECC_BITS (256)
#      define LTC_MAX_INT_BYTES (128)
#      ifndef LTC_BASE
#      define LTC_BASE ((LTC_Type *)LTC0_BASE)
#      endif
#   endif

/* SP MATH */
#   define WOLFSSL_SP
#   define WOLFSSL_SP_MATH
#   define WOLFSSL_SP_SMALL
#   define SP_WORD_SIZE 32
#   define WOLFSSL_HAVE_SP_ECC

/* ECC options disabled to reduce size */
#   define NO_ECC_SIGN
#   define NO_ECC_EXPORT
#   define NO_ECC_DHE
#   define NO_ECC_KEY_EXPORT

/* Curve */
#   define NO_ECC192
#   define NO_ECC224
#   define HAVE_ECC256
#   define NO_ECC384
#   define NO_RSA
#   define NO_ASN
#endif

#ifdef WOLFBOOT_SIGN_RSA2048
#  define HAVE_RSA
#  define RSA_LOW_MEM
#  define WOLFSSL_RSA_VERIFY_INLINE
#  define WOLFSSL_HAVE_SP_RSA
#  define WOLFSSL_SP
#  define WOLFSSL_SP_SMALL
#  define WOLFSSL_SP_MATH
#  define SP_WORD_SIZE 32
#  define WOLFSSL_SP_NO_3072
#endif

#ifdef WOLFBOOT_SIGN_RSA4096
#  define HAVE_RSA
#  define RSA_LOW_MEM
#  define WOLFSSL_RSA_PUBLIC_ONLY
#  define WOLFSSL_RSA_VERIFY_INLINE
#  define FP_MAX_BITS (4096 * 2)
#  define WC_RSA_BLINDING
#  define USE_FAST_MATH
#  define TFM_TIMING_RESISTANT
#endif

#ifdef WOLFBOOT_HASH_SHA3_384
# define WOLFSSL_SHA3
# define NO_SHA256
#endif

#ifdef EXT_ENCRYPTED
#  define HAVE_CHACHA
#  define HAVE_PWDBASED
#else
#  define NO_PWDBASED
#endif

/* Disables - For minimum wolfCrypt build */
#ifndef WOLFBOOT_TPM
    #define NO_AES
    #define NO_HMAC
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
#define WOLFSSL_HAVE_MIN
#define WOLFSSL_HAVE_MAX
#endif

#endif /* !H_USER_SETTINGS_ */
