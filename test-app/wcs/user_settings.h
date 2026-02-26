/* nonsecure_user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL (PKCS11 client example)
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

#ifndef H_USER_SETTINGS_
#define H_USER_SETTINGS_

#include <target.h>
#include <time.h>
int clock_gettime (clockid_t clock_id, struct timespec *tp);
#include "wolfboot/wc_secure.h"

#define WOLFCRYPT_ONLY
#define WOLFSSL_SMALL_CERT_VERIFY
#define WOLFSSL_LEAN_PSK

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define SIZEOF_LONG_LONG 8

#define CTYPE_USER /* don't let wolfCrypt types.h include ctype.h */
extern int toupper(int c);
extern int tolower(int c);
#define XTOUPPER(c)     toupper((c))
#define XTOLOWER(c)     tolower((c))

/* PKCS11 */
#ifdef WOLFBOOT_TZ_PKCS11
  #define HAVE_PKCS11
  #define HAVE_WOLF_BIGINT
  #define HAVE_PKCS11_STATIC
  #define WOLF_CRYPTO_CB
  #define MAX_CRYPTO_DEVID_CALLBACKS 2
#endif



/* ECC */
#define ECC_USER_CURVES
#define HAVE_ECC
#undef  NO_ECC256
#define HAVE_ECC384
#ifdef WOLFSSL_HAVE_SP_ECC
  #define WOLFSSL_SP_384
#endif

#ifndef NO_RSA
  /* RSA */
  #define HAVE_RSA
  #define RSA_LOW_MEM
  #define WOLFSSL_RSA_VERIFY_INLINE
  #define WC_ASN_HASH_SHA256
  #define FP_MAX_BITS (4096 * 2)
#endif


/* SHA */
#define WOLFSSL_SHA3
#define WOLFSSL_SHA384

/* HMAC */
#define WOLFSSL_HMAC
#define HAVE_HKDF


/* PWDBASED */
#define HAVE_PWDBASED

/* BASE64 */
#define WOLFSSL_BASE64_DECODE
#define WOLFSSL_BASE64_ENCODE


/* AES */
#ifndef WOLFSSL_AES_128
#define WOLFSSL_AES_128
#endif

#ifndef WOLFSSL_AES_256
#define WOLFSSL_AES_256
#endif
#ifndef WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_COUNTER
#endif
#ifndef WOLFSSL_AES_CBC
#define WOLFSSL_AES_CBC
#endif
#ifndef WOLFSSL_AES_DIRECT
#define WOLFSSL_AES_DIRECT
#endif

#define HAVE_AESGCM
#define GCM_TABLE_4BIT

/* Hardening */
#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

/* Exclude */
#define NO_CMAC
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
#ifndef NO_DES3
#define NO_DES3
#endif
#define NO_WRITEV
#define NO_DEV_RANDOM
#define NO_FILESYSTEM
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME
#define NO_OLD_TLS
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_IGNORE_FILE_WARN
#define NO_ERROR_STRINGS
#define NO_KDF

#ifdef WOLF_CRYPTO_CB
    #define WC_TEST_NO_CRYPTOCB_SW_TEST
#endif
#define BENCH_EMBEDDED

#define HAVE_ECC_KEY_EXPORT
#define HAVE_PKCS8
#define HAVE_PKCS12

#ifdef SECURE_PKCS11

static inline int wcs_cmse_get_random(unsigned char* output, int sz)
{
    return wcs_get_random(output, sz);
}

#define CUSTOM_RAND_GENERATE_BLOCK wcs_cmse_get_random
#endif

/* Disable VLAs */
#define WOLFSSL_SP_NO_DYN_STACK


struct timespec;
int clock_gettime (unsigned long clock_id, struct timespec *tp);

#endif /* !H_USER_SETTINGS_ */
