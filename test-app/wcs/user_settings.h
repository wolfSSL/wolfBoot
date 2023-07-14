/* nonsecure_user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL (PKCS11 client example)
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

#define WOLFCRYPT_ONLY
#define WOLFSSL_SP_MATH_ALL
//#define NO_RSA
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
#define HAVE_PKCS11
#define HAVE_WOLF_BIGINT
//#define WOLFSSL_SP_MATH
//#define USE_FAST_MATH
#define HAVE_PKCS11_STATIC
#define WOLF_CRYPTO_CB

#define NO_RSA

/* ECC */
#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define ECC_USER_CURVES /* enables only 256-bit by default */
#define HAVE_ECC_SIGN
#define HAVE_ECC_CDH
#define HAVE_ECC256
#define HAVE_ECC384
#define HAVE_ECC521
#ifdef WOLFSSL_SP_MATH
#define WOLFSSL_HAVE_SP_ECC
#endif


#ifndef NO_RSA
    /* RSA */
    #define WOLFSSL_KEY_GEN
    #define HAVE_RSA
    #define RSA_LOW_MEM
    #define WOLFSSL_RSA_VERIFY_INLINE
    #define WC_ASN_HASH_SHA256
    #define FP_MAX_BITS (4096 * 2)
    #ifdef WOLFSSL_SP_MATH
        #define WOLFSSL_HAVE_SP_RSA
    #endif
#endif


/* SHA */
//#define WOLFSSL_SHA3
#define WOLFSSL_SHA384

/* HMAC */
//#define WOLFSSL_HMAC
//#define HAVE_HKDF


/* PWDBASED */
//#define HAVE_PWDBASED

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
#ifndef WOLFSSL_AES_DIRECT
#define WOLFSSL_AES_DIRECT
#endif

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
//#define NO_SIG_WRAPPER
#define NO_CERT
#define NO_SESSION_CACHE
#define NO_HC128
#define NO_DES3
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

#define BENCH_EMBEDDED
#define NO_CRYPT_TEST
#define NO_CRYPT_BENCHMARK

#define CUSTOM_RAND_GENERATE_BLOCK wcs_get_random



#endif /* !H_USER_SETTINGS_ */
