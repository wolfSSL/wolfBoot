/* user_settings.h
 *
 * wolfBoot and wolfCrypt configuration for the RX71M CC-RX projects:
 * ECC256 signatures, SHA256 hash. Enabled via WOLFSSL_USER_SETTINGS.
 *
 * Copyright (C) 2026 wolfSSL Inc.
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

/* keep include/user_settings.h out; CC-RX can find it next to wolfBoot headers */
#define _WOLFBOOT_USER_SETTINGS_H_

/* wolfBoot */
#define WOLFBOOT_SIGN_ECC256
#define WOLFBOOT_HASH_SHA256
#define IMAGE_HEADER_SIZE 256
#define NVM_FLASH_WRITEONCE
#define PRINTF_ENABLED
/* Hand over to the application on the LOCO, as after a reset */
#define WOLFBOOT_RESTORE_CLOCK

#include <machine.h> /* min() and max() */
#define WOLFSSL_HAVE_MIN
#define WOLFSSL_HAVE_MAX

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define WOLFSSL_USER_MUTEX
#define WOLFCRYPT_ONLY
#define SIZEOF_LONG_LONG 8
#define WC_NO_RNG_SIMPLE

/* ECC256 with SP math */
#define HAVE_ECC
#define HAVE_ECC256
#define ECC_TIMING_RESISTANT
#define ECC_USER_CURVES
#define FP_MAX_BITS (256 + 32)
#define WOLFSSL_SP
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_NO_384
#define WOLFSSL_SP_NO_521
#define NO_ECC_SIGN
#define NO_ECC_EXPORT
#define NO_ECC_DHE
#define NO_ECC_KEY_EXPORT
#define NO_RSA
#define NO_ASN

/* Disables - for a minimum wolfCrypt build */
#define NO_AES
#define NO_HMAC
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
#define NO_PWDBASED
#define NO_KDF
#define NO_PKCS8
#define NO_PKCS12
#define NO_CHECK_PRIVATE_KEY
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

/* Memory model: no heap */
#define WOLFSSL_SP_NO_MALLOC
#define WOLFSSL_SP_NO_DYN_STACK
#define WOLFSSL_NO_MALLOC
#define NO_WOLFSSL_MEMORY

#endif /* !H_USER_SETTINGS_ */
