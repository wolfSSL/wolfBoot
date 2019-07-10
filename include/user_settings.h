/* user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
 *
 *
 * Copyright (C) 2019 wolfSSL Inc.
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

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
//#define WOLFSSL_SMALL_STACK
#define WOLFCRYPT_ONLY
#define TFM_TIMING_RESISTANT

/* ED25519 and SHA512 */
#ifdef WOLFBOOT_SIGN_ED25519
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define NO_ED25519_SIGN
#   define NO_ED25519_EXPORT
#   define WOLFSSL_SHA512
#   define USE_SLOW_SHA512
#endif

/* ECC and SHA256 */
#ifdef WOLFBOOT_SIGN_ECC256
#   define HAVE_ECC
#   define ECC_TIMING_RESISTANT
#   define USE_FAST_MATH
#   define FP_MAX_BITS (256 + 32)

#   ifdef FREESCALE_USE_LTC
#      define LTC_MAX_ECC_BITS (256)
#      define LTC_MAX_INT_BYTES (128)
#      define LTC_BASE ((LTC_Type *)LTC0_BASE)
#   else
#      define WOLFSSL_SP
#      define WOLFSSL_SP_SMALL
#      define WOLFSSL_SP_MATH
#      define SP_WORD_SIZE 32
#      define WOLFSSL_HAVE_SP_ECC
#   endif

#   define NO_ECC_SIGN
#   define NO_ECC_EXPORT
#   define NO_ECC_DHE
#   define NO_ECC_KEY_EXPORT
#endif

/* Disables - For minimum wolfCrypt build */
#define NO_AES
#define NO_CMAC
#define NO_CODING
#define NO_RSA
#define NO_BIG_INT
#define NO_ASN
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
#define WC_NO_RNG
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK

#endif /* !H_USER_SETTINGS_ */
