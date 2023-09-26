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

#ifdef WOLFBOOT_PKCS11_APP
# include "test-app/wcs/user_settings.h"
#else


#include <target.h>

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define WOLFCRYPT_ONLY
#define SIZEOF_LONG_LONG 8

#define CTYPE_USER /* don't let wolfCrypt types.h include ctype.h */
extern int toupper(int c);
extern int tolower(int c);
#define XTOUPPER(c)     toupper((c))
#define XTOLOWER(c)     tolower((c))

#ifdef USE_FAST_MATH
#   define WC_NO_HARDEN
#endif

#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
#   define WOLFBOOT_TPM_PARMENC /* used in this file to gate features */
#endif

#ifdef WOLFCRYPT_SECURE_MODE
    int hal_trng_get_entropy(unsigned char *out, unsigned len);
    #define CUSTOM_RAND_GENERATE_SEED hal_trng_get_entropy
#endif

/* ED25519 and SHA512 */
#ifdef WOLFBOOT_SIGN_ED25519
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define NO_ED25519_SIGN
#   define NO_ED25519_EXPORT
#   define WOLFSSL_SHA512
#   define USE_SLOW_SHA512
#endif

/* ED448 */
#ifdef WOLFBOOT_SIGN_ED448
#   define HAVE_ED448
#   define HAVE_ED448_VERIFY
#   define ED448_SMALL
#   define NO_ED448_SIGN
#   define NO_ED448_EXPORT
#   define WOLFSSL_SHA3
#   define WOLFSSL_SHAKE256
#endif

/* ECC */
#if defined(WOLFBOOT_SIGN_ECC256) || \
    defined(WOLFBOOT_SIGN_ECC384) || \
    defined(WOLFBOOT_SIGN_ECC521) || \
    defined(WOLFCRYPT_SECURE_MODE)

#   define HAVE_ECC
#   define ECC_TIMING_RESISTANT
#   define ECC_USER_CURVES /* enables only 256-bit by default */

/* Kinetis LTC support */
#   ifdef FREESCALE_USE_LTC
#      define FREESCALE_COMMON
#      define FSL_HW_CRYPTO_MANUAL_SELECTION
#      define FREESCALE_LTC_ECC
#      define FREESCALE_LTC_TFM
#   endif


/* Some ECC options are disabled to reduce size */
#   if !defined(WOLFCRYPT_SECURE_MODE)
#       if !defined(WOLFBOOT_TPM)
#          define NO_ECC_SIGN
#          define NO_ECC_DHE
#          define NO_ECC_EXPORT
#          define NO_ECC_KEY_EXPORT
#       else
#           define HAVE_ECC_KEY_EXPORT
#       endif
#   else
#       define HAVE_ECC_SIGN
#       define HAVE_ECC_CDH
#       define WOLFSSL_SP
#       define WOLFSSL_SP_MATH
#       define WOLFSSL_SP_SMALL
#       define SP_WORD_SIZE 32
#       define WOLFSSL_HAVE_SP_ECC
#       define WOLFSSL_KEY_GEN
#       define HAVE_ECC_KEY_EXPORT
#   endif

    /* SP MATH */
#   if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#       define WOLFSSL_SP
#       define WOLFSSL_SP_MATH
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_HAVE_SP_ECC
#   endif


/* Curve */
#   ifdef WOLFBOOT_SIGN_ECC256
#       define HAVE_ECC256
#       define FP_MAX_BITS (256 + 32)
#       if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#           define WOLFSSL_SP_NO_384
#           define WOLFSSL_SP_NO_521
#       endif
#   elif defined(WOLFBOOT_SIGN_ECC384)
#       define HAVE_ECC384
#       define FP_MAX_BITS (384 * 2)
#       if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#           define WOLFSSL_SP_384
#           define WOLFSSL_SP_NO_256
#       endif
#       if !defined(WOLFBOOT_TPM_PARMENC)
#           define NO_ECC256
#       endif
#   elif defined(WOLFBOOT_SIGN_ECC521)
#       define HAVE_ECC521
#       define FP_MAX_BITS (528 * 2)
#       if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#           define WOLFSSL_SP_521
#           define WOLFSSL_SP_NO_256
#       endif
#       if !defined(WOLFBOOT_TPM_PARMENC)
#           define NO_ECC256
#       endif
#   endif
#endif /* WOLFBOOT_SIGN_ECC521 || WOLFBOOT_SIGN_ECC384 || WOLFBOOT_SIGN_ECC256 */


#if defined(WOLFBOOT_SIGN_RSA2048) || \
    defined(WOLFBOOT_SIGN_RSA3072) || \
    defined(WOLFBOOT_SIGN_RSA4096) || \
    defined(WOLFCRYPT_SECURE_MODE)


#   define WC_RSA_BLINDING 
#   define WC_RSA_DIRECT
#   define RSA_LOW_MEM
#   define WC_ASN_HASH_SHA256
#   if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE)
#       define WOLFSSL_RSA_VERIFY_INLINE
#       define WOLFSSL_RSA_VERIFY_ONLY
#       define WC_NO_RSA_OAEP
#   endif
#   if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#       define WOLFSSL_HAVE_SP_RSA
#       define WOLFSSL_SP
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_SP_MATH
#   endif
#   ifdef WOLFBOOT_SIGN_RSA2048
#       define FP_MAX_BITS (2048 * 2)
#       define WOLFSSL_SP_NO_3072
#       define WOLFSSL_SP_NO_4096
#       define WOLFSSL_SP_2048
#   endif
#   ifdef WOLFBOOT_SIGN_RSA3072
#       define FP_MAX_BITS (3072 * 2)
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_4096
#       define WOLFSSL_SP_3072
#   endif

#   ifdef WOLFBOOT_SIGN_RSA4096
#       define FP_MAX_BITS (4096 * 2)
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_3072
#       define WOLFSSL_SP_4096
#   endif
#   ifdef WOLFCRYPT_SECURE_MODE
#       undef FP_MAX_BITS
#       define FP_MAX_BITS (4096 * 2)
#       define WOLFSSL_SP_2048
#       define WOLFSSL_SP_3072
#       define WOLFSSL_SP_4096
#   endif
#else
#   define NO_RSA
#endif /* RSA */

#ifdef WOLFBOOT_HASH_SHA3_384
#   define WOLFSSL_SHA3
#   if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
    !defined(WOLFCRYPT_SECURE_MODE)
#       define NO_SHA256
#   endif
#endif

#ifdef WOLFBOOT_HASH_SHA384
#   define WOLFSSL_SHA384
#   if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
    !defined(WOLFCRYPT_SECURE_MODE)
#       define NO_SHA256
#   endif
#endif

/* If SP math is enabled determine word size */
#if defined(WOLFSSL_HAVE_SP_ECC) || defined(WOLFSSL_HAVE_SP_RSA)
#   ifdef __aarch64__
#       define HAVE___UINT128_T
#       define WOLFSSL_SP_ARM64_ASM
#       define SP_WORD_SIZE 64
#   elif defined(ARCH_x86_64) && !defined(FORCE_32BIT)
#       define SP_WORD_SIZE 64
#       ifndef NO_ASM
#           define WOLFSSL_SP_X86_64_ASM
#       endif
#   else
#       define SP_WORD_SIZE 32
#   endif

        /* SP Math needs to understand long long */
#   ifndef ULLONG_MAX
#       define ULLONG_MAX 18446744073709551615ULL
#   endif
#endif

#if defined(EXT_ENCRYPTED)
#   define HAVE_PWDBASED
#endif

#if defined(SECURE_PKCS11)
#   define HAVE_PWDBASED
#	define HAVE_PBKDF2
#	define WOLFPKCS11_CUSTOM_STORE
# 	define WOLFBOOT_SECURE_PKCS11
#	define WOLFPKCS11_USER_SETTINGS
# 	define WOLFPKCS11_NO_TIME
# 	define WOLFSSL_AES_COUNTER
#   define WOLFSSL_AES_DIRECT
#   define WOLFSSL_AES_GCM
# 	define ENCRYPT_WITH_AES128
#   define WOLFSSL_AES_128
# 	define HAVE_SCRYPT
# 	define HAVE_AESGCM
	typedef unsigned long time_t;
#endif

#ifndef HAVE_PWDBASED
#   define NO_PWDBASED
#endif

#if defined(WOLFBOOT_TPM_SEAL) && defined(WOLFBOOT_ATA_DISK_LOCK)
#define WOLFSSL_BASE64_ENCODE
#else
#define NO_CODING
#endif

#ifdef WOLFBOOT_TPM
    /* Do not use heap */
    #define WOLFTPM2_NO_HEAP
    /* small stack options */
    #ifdef WOLFTPM_SMALL_STACK
        #define MAX_COMMAND_SIZE 1024
        #define MAX_RESPONSE_SIZE 1350
        #define WOLFTPM2_MAX_BUFFER 1500
        #define MAX_SESSION_NUM 2
        #define MAX_DIGEST_BUFFER 973
    #endif

    #ifdef WOLFBOOT_TPM_PARMENC
        /* Enable AES CFB (parameter encryption) and HMAC (for KDF) */
        #define WOLFSSL_AES_CFB

        /* Get access to mp_* math API's for ECC encrypt */
        #define WOLFSSL_PUBLIC_MP

        /* Configure RNG seed */
        #define CUSTOM_RAND_GENERATE_SEED(buf, sz) ({(void)buf; (void)sz; 0;}) /* stub, not used */
        #define WC_RNG_SEED_CB
    #endif

    #ifdef WOLFTPM_MMIO
        /* IO callback it above TIS and includes Address and if read/write */
        #define WOLFTPM_ADV_IO
    #endif

    /* add delay */
    #if !defined(XTPM_WAIT) && defined(WOLFTPM_MMIO)
        void delay(int msec);
        #define XTPM_WAIT() delay(1);
    #endif
    #ifndef XTPM_WAIT
        #define XTPM_WAIT() /* no delay */
    #endif

    /* TPM remap printf */
    #if defined(DEBUG_WOLFTPM) && !defined(ARCH_SIM)
        #include "printf.h"
        #define printf wolfBoot_printf
    #endif
#endif

#if !defined(WOLFCRYPT_SECURE_MODE) && !defined(WOLFBOOT_TPM_PARMENC)
    #define WC_NO_RNG
    #define WC_NO_HASHDRBG
    #define NO_AES_CBC
#else
    #define HAVE_HASHDRBG
    #define WOLFSSL_AES_CFB
#endif


#if !defined(ENCRYPT_WITH_AES128) && !defined(ENCRYPT_WITH_AES256) && \
    !defined(WOLFBOOT_TPM_PARMENC) && !defined(WOLFCRYPT_SECURE_MODE)
    #define NO_AES
#endif

#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE)
#   define NO_HMAC
#   define WC_NO_RNG
#   define WC_NO_HASHDRBG
#   define NO_DEV_RANDOM
#   define NO_ECC_KEY_EXPORT
#   ifdef NO_RSA
#       define NO_ASN
#   endif
#endif

#define NO_CMAC
#define NO_DH
#define WOLFSSL_NO_PEM
#define NO_ASN_TIME
#define NO_RC4
#define NO_SHA
#define NO_DSA
#define NO_MD4
#define NO_RABBIT
#define NO_MD5
#define NO_SIG_WRAPPER
#define NO_CERT
#define NO_SESSION_CACHE
#define NO_HC128
#define NO_DES3
#define NO_WRITEV
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

#if defined(WOLFSSL_SP_MATH) || defined(WOLFSSL_SP_MATH_ALL)
    /* Disable VLAs */
    #define WOLFSSL_SP_NO_DYN_STACK
#endif

#ifndef WOLFBOOT_SMALL_STACK
#   ifdef WOLFSSL_SP_MATH
#       define WOLFSSL_SP_NO_MALLOC
#       define WOLFSSL_SP_NO_DYN_STACK
#   endif
#   if !defined(ARCH_SIM) && !defined(SECURE_PKCS11)
#       define WOLFSSL_NO_MALLOC
#   endif
#else
#   if defined(WOLFBOOT_HUGE_STACK)
#       error "Cannot use SMALL_STACK=1 with HUGE_STACK=1"
#endif
#   define WOLFSSL_SMALL_STACK
#endif

#ifdef WOLFCRYPT_SECURE_MODE
typedef unsigned long time_t;
#endif

#endif /* WOLFBOOT_PKCS11_APP */

#endif /* !H_USER_SETTINGS_ */
