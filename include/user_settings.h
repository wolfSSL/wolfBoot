/* user_settings.h
 *
 * Custom configuration for wolfCrypt/wolfSSL.
 * Enabled via WOLFSSL_USER_SETTINGS.
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
#ifndef _WOLFBOOT_USER_SETTINGS_H_
#define _WOLFBOOT_USER_SETTINGS_H_

#ifdef WOLFBOOT_PKCS11_APP
# include "test-app/wcs/user_settings.h"
#else

#include <target.h>

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define WOLFSSL_USER_MUTEX /* avoid wc_port.c wc_InitAndAllocMutex */
#if !(defined WOLFBOOT_ENABLE_WOLFHSM_SERVER && \
        defined(WOLFBOOT_CERT_CHAIN_VERIFY))
#define WOLFCRYPT_ONLY
#endif
#define SIZEOF_LONG_LONG 8
#define HAVE_EMPTY_AGGREGATES 0
#define HAVE_ANONYMOUS_INLINE_AGGREGATES 0

/* Stdlib Types */
#define CTYPE_USER /* don't let wolfCrypt types.h include ctype.h */

#ifndef WOLFSSL_ARMASM
#ifndef toupper
extern int toupper(int c);
#endif
#ifndef tolower
extern int tolower(int c);
#endif
#define XTOUPPER(c)     toupper((c))
#define XTOLOWER(c)     tolower((c))
#endif

#ifdef USE_FAST_MATH
    /* wolfBoot only does public asymmetric operations,
     * so timing resistance and hardening is not required */
#   define WC_NO_HARDEN
#endif

#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
    /* TPM Parameter Encryption */
#   define WOLFBOOT_TPM_PARMENC /* used in this file to gate features */
#endif

#ifdef WOLFCRYPT_SECURE_MODE
    int hal_trng_get_entropy(unsigned char *out, unsigned len);
    #define CUSTOM_RAND_GENERATE_SEED hal_trng_get_entropy
#endif

/* ED25519 and SHA512 */
#if defined(WOLFBOOT_SIGN_ED25519) || defined(WOLFBOOT_SIGN_SECONDARY_ED25519)
#   define HAVE_ED25519
#   define ED25519_SMALL
#   define NO_ED25519_SIGN
#   define NO_ED25519_EXPORT
#   define USE_SLOW_SHA512
#   define WOLFSSL_SHA512
#endif

/* ED448 and SHA3/SHAKE256 */
#if defined(WOLFBOOT_SIGN_ED448) || defined(WOLFBOOT_SIGN_SECONDARY_ED448)
#   define HAVE_ED448
#   define HAVE_ED448_VERIFY
#   define ED448_SMALL
#   define NO_ED448_SIGN
#   define NO_ED448_EXPORT
#   define WOLFSSL_SHA3
#   define WOLFSSL_SHAKE256
#   define WOLFSSL_SHA512
#endif

/* ECC */
#if defined(WOLFBOOT_SIGN_ECC256) || \
    defined(WOLFBOOT_SIGN_ECC384) || \
    defined(WOLFBOOT_SIGN_ECC521) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC256) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC384) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC521) || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)

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
#   if !defined(WOLFCRYPT_SECURE_MODE) && \
       !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#       if !defined(WOLFBOOT_TPM)
#          define NO_ECC_SIGN
#          define NO_ECC_DHE
           /* For Renesas RX do not enable the misc.c constant time code
            * due to issue with 64-bit types */
#          if defined(__RX__)
#              define WOLFSSL_NO_CT_OPS /* don't use constant time ops in misc.c */
#          endif
#          if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
              !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#              define NO_ECC_EXPORT
#              define NO_ECC_KEY_EXPORT
#          endif
#       else
#           define HAVE_ECC_KEY_EXPORT
#       endif
#   else
#       define HAVE_ECC_SIGN
#       define HAVE_ECC_VERIFY
#ifndef PKCS11_SMALL
#       define HAVE_ECC_CDH
#endif
#       define WOLFSSL_SP_MATH
#       define WOLFSSL_SP_SMALL
#       define SP_WORD_SIZE 32
#       define WOLFSSL_HAVE_SP_ECC
#       define WOLFSSL_KEY_GEN
#       define HAVE_ECC_KEY_EXPORT
#       define HAVE_ECC_KEY_IMPORT
#   endif

    /* SP MATH */
#   if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#       define WOLFSSL_SP_MATH
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_HAVE_SP_ECC
#   endif

#define WOLFSSL_PUBLIC_MP

    /* Curve */
#   if defined(WOLFBOOT_SIGN_ECC256) || defined(WOLFCRYPT_SECURE_MODE) || \
        defined(WOLFBOOT_SIGN_SECONDARY_ECC256) || \
        defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#       define HAVE_ECC256
#   endif
#   if defined(WOLFBOOT_SIGN_ECC384) || \
        defined(WOLFBOOT_SIGN_SECONDARY_ECC384) || \
        defined(WOLFCRYPT_SECURE_MODE) || \
        defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#       define HAVE_ECC384
#       define WOLFSSL_SP_384
#   endif
    /* ECC521 only enabled if specifically requested (not for tests - too large) */
#   if defined(WOLFBOOT_SIGN_ECC521) || \
        defined(WOLFBOOT_SIGN_SECONDARY_ECC521) || \
        defined(WOLFCRYPT_SECURE_MODE)
#       define HAVE_ECC521
#       define WOLFSSL_SP_521
#   endif

    /* FP MAX BITS */
#   if defined(HAVE_ECC521)
#   define FP_MAX_BITS ((528 * 2))
#   elif defined(HAVE_ECC384)
#   define FP_MAX_BITS ((384 * 2))
#   elif defined(HAVE_ECC256)
#   define FP_MAX_BITS ((256 + 32))
#   endif

#   if !defined(HAVE_ECC256) && !defined(WOLFBOOT_TPM_PARMENC)
#   define NO_ECC256
#   endif

#   if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#      if !defined(HAVE_ECC521)
#      define WOLFSSL_SP_NO_521
#      endif
#      if !defined(HAVE_ECC384)
#      define WOLFSSL_SP_NO_384
#      endif
#      if !defined(HAVE_ECC256)
#      define WOLFSSL_SP_NO_256
#      endif
#   endif
#endif /* WOLFBOOT_SIGN_ECC521 || WOLFBOOT_SIGN_ECC384 || WOLFBOOT_SIGN_ECC256 ||
        * WOLFBOOT_SIGN_SECONDARY_ECC521 || WOLFBOOT_SIGN_SECONDARY_ECC384 ||
        * WOLFBOOT_SIGN_SECONDARY_ECC256 || WOLFCRYPT_SECURE_MODE */


/* RSA */
#if defined(WOLFBOOT_SIGN_RSA2048) || \
    defined(WOLFBOOT_SIGN_RSA3072) || \
    defined(WOLFBOOT_SIGN_RSA4096) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA2048) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA3072) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA4096) || \
    (defined(WOLFCRYPT_SECURE_MODE) && (!defined(PKCS11_SMALL)))

#   define WC_RSA_BLINDING
#   define WC_RSA_DIRECT
#   define RSA_LOW_MEM
#   define WC_ASN_HASH_SHA256
#   if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
       !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#       define WOLFSSL_RSA_VERIFY_INLINE
#       define WOLFSSL_RSA_VERIFY_ONLY
#       define WOLFSSL_RSA_PUBLIC_ONLY
#       define WC_NO_RSA_OAEP
#       define NO_RSA_BOUNDS_CHECK
#   endif
#   if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#       define WOLFSSL_HAVE_SP_RSA
#       define WOLFSSL_SP
#       define WOLFSSL_SP_SMALL
#       define WOLFSSL_SP_MATH
#   endif
#   if defined(WOLFBOOT_SIGN_RSA2048) || defined(WOLFBOOT_SIGN_SECONDARY_RSA2048)
#       define FP_MAX_BITS (2048 * 2)
#       define SP_INT_BITS 2048
#       define WOLFSSL_SP_NO_3072
#       define WOLFSSL_SP_NO_4096
#       define WOLFSSL_SP_2048
#       define RSA_MIN_SIZE 2048
#       define RSA_MAX_SIZE 2048
#   endif
#   if defined(WOLFBOOT_SIGN_RSA3072) || defined(WOLFBOOT_SIGN_SECONDARY_RSA3072)
#       define FP_MAX_BITS (3072 * 2)
#       define SP_INT_BITS 3072
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_4096
#       define WOLFSSL_SP_3072
#       define RSA_MIN_SIZE 3072
#       define RSA_MAX_SIZE 3072
#   endif

#   if defined(WOLFBOOT_SIGN_RSA4096) || defined(WOLFBOOT_SIGN_SECONDARY_RSA4096)
#       define FP_MAX_BITS (4096 * 2)
#       define SP_INT_BITS 4096
#       define WOLFSSL_SP_NO_2048
#       define WOLFSSL_SP_NO_3072
#       define WOLFSSL_SP_4096
#       define RSA_MIN_SIZE 4096
#       define RSA_MAX_SIZE 4096
#   endif
#   ifdef WOLFCRYPT_SECURE_MODE
#       undef FP_MAX_BITS
#       define FP_MAX_BITS (4096 * 2)
#       define SP_INT_BITS 4096
#       define WOLFSSL_SP_2048
#       define WOLFSSL_SP_3072
#       define WOLFSSL_SP_4096
#       define RSA_MIN_SIZE 2048
#       define RSA_MAX_SIZE 4096
#   endif
#else
#   define NO_RSA
#endif /* RSA */

/* ML-DSA (dilithium) */
#if defined(WOLFBOOT_SIGN_ML_DSA) || defined(WOLFBOOT_SIGN_SECONDARY_ML_DSA)
#   define HAVE_DILITHIUM
#   define WOLFSSL_WC_DILITHIUM
#   define WOLFSSL_EXPERIMENTAL_SETTINGS
    /* Wolfcrypt builds ML-DSA (dilithium) to the FIPS 204 final
     * standard by default. Uncomment this if you want the draft
     * version instead. */
#   if 0
#      define WOLFSSL_DILITHIUM_FIPS204_DRAFT
#   endif
#   define WOLFSSL_DILITHIUM_VERIFY_ONLY
#   define WOLFSSL_DILITHIUM_NO_LARGE_CODE
#   define WOLFSSL_DILITHIUM_SMALL
#   define WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM
#   define WOLFSSL_DILITHIUM_VERIFY_NO_MALLOC
#   if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#      define WOLFSSL_DILITHIUM_NO_ASN1
#   endif
    /* dilithium needs these sha functions. */
#   define WOLFSSL_SHA3
#   define WOLFSSL_SHAKE256
#   define WOLFSSL_SHAKE128
#   define WOLFSSL_SP_NO_DYN_STACK
#endif /* WOLFBOOT_SIGN_ML_DSA || WOLFBOOT_SIGN_SECONDARY_ML_DSA */

#ifdef WOLFBOOT_HASH_SHA3_384
#   define WOLFSSL_SHA3
#   if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
        !defined(WOLFCRYPT_SECURE_MODE) && \
        !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#       define NO_SHA256
#   endif
#endif

#ifdef WOLFBOOT_HASH_SHA384
#   define WOLFSSL_SHA384
#   if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
    !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#       define NO_SHA256
#   endif
#ifndef WOLFSSL_SHA512
#define WOLFSSL_SHA512
#endif
#endif

/* If SP math is enabled determine word size */
#if defined(WOLFSSL_HAVE_SP_ECC) || defined(WOLFSSL_HAVE_SP_RSA)
#   ifdef __aarch64__
#       define HAVE___UINT128_T
#       define WOLFSSL_SP_ARM64_ASM
#       define SP_WORD_SIZE 64
#   elif defined(ARCH_RISCV64)
#       define HAVE___UINT128_T
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
#   include <time.h>
#   define HAVE_PWDBASED
#   define HAVE_PBKDF2
#   define WOLFPKCS11_CUSTOM_STORE
#   define WOLFBOOT_SECURE_PKCS11
#   define WOLFPKCS11_USER_SETTINGS
#   define WOLFPKCS11_NO_TIME
#ifndef WOLFSSL_AES_COUNTER
#   define WOLFSSL_AES_COUNTER
#endif
#   define HAVE_AESCTR
#ifndef WOLFSSL_AES_DIRECT
#   define WOLFSSL_AES_DIRECT
#endif
#   define WOLFSSL_AES_GCM
#   define GCM_TABLE_4BIT
#   define WOLFSSL_AES_128
#   define HAVE_SCRYPT
#   define HAVE_AESGCM
#   define HAVE_PKCS8
#endif

#if defined(WOLFCRYPT_TZ_PSA)
#   define WOLFSSL_AES_COUNTER
#   define WOLFSSL_AES_GCM
#   define HAVE_AESGCM
#   define HAVE_AESCCM
#   define HAVE_AES_ECB
#   define WOLFSSL_AES_CFB
#   define WOLFSSL_AES_OFB
#   ifndef NO_DES3
#       define NO_DES3
#   endif
#   ifndef NO_DES3_TLS_SUITES
#       define NO_DES3_TLS_SUITES
#   endif
#   define HAVE_CHACHA
#   define HAVE_POLY1305
#   define WOLFSSL_CMAC
#   define WOLFSSL_ECDSA_DETERMINISTIC_K
#   define WOLFSSL_HAVE_PRF
#   define HAVE_HKDF
#   define HAVE_PBKDF2
#   define HAVE_PWDBASED
#   define WOLFSSL_KEY_GEN
#   define WC_RSA_PSS
#   define WOLFSSL_PSS_SALT_LEN_DISCOVER
#   define WOLFSSL_RSA_OAEP
#   define HAVE_ECC_KEY_EXPORT
#   define HAVE_ECC_KEY_IMPORT
#endif
/* PKCS11 for wolfBoot is always static */
#define HAVE_PKCS11_STATIC

#ifndef HAVE_PWDBASED
#   define NO_PWDBASED
#endif

#if (defined(WOLFBOOT_TPM_SEAL) && defined(WOLFBOOT_ATA_DISK_LOCK)) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) || \
    defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#   define WOLFSSL_BASE64_ENCODE
#else
#   define NO_CODING
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
        #include "loader.h"
        #define CUSTOM_RAND_GENERATE_SEED(buf, sz) \
            ({(void)buf; (void)sz; wolfBoot_panic(); 0;}) /* stub, not used */
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
    #define HASH_COUNT 3 /* enable more PCR hash types */

    /* TPM remap printf */
    #if defined(DEBUG_WOLFTPM) && !defined(ARCH_SIM)
        #include "printf.h"
        #define printf wolfBoot_printf
    #endif
#endif

#if !defined(WOLFCRYPT_SECURE_MODE) && !defined(WOLFBOOT_TPM_PARMENC) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#if !(defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
      defined(WOLFBOOT_SIGN_ML_DSA)) && \
    !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#define WC_NO_RNG
#endif
    #define WC_NO_HASHDRBG
    #define NO_AES_CBC
#else
    #if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
        /* Use custom RNG for tests/benchmarks (saves ~7KB vs HASHDRBG).
         * WARNING: my_rng_seed_gen is NOT cryptographically secure.
         * Only used in test-app builds, not in production wolfBoot. */
        #define WC_NO_HASHDRBG
        #define CUSTOM_RAND_GENERATE_SEED my_rng_seed_gen
        #define CUSTOM_RAND_GENERATE_BLOCK my_rng_seed_gen
        extern int my_rng_seed_gen(unsigned char* output, unsigned int sz);
    #else
        #define HAVE_HASHDRBG
        #define WOLFSSL_AES_CFB
    #endif
#endif


#if !defined(ENCRYPT_WITH_AES128) && !defined(ENCRYPT_WITH_AES256) && \
    !defined(WOLFBOOT_TPM_PARMENC) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(SECURE_PKCS11) && !defined(WOLFCRYPT_TZ_PSA) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
    #define NO_AES
#endif

#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#   define NO_HMAC
#endif

#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#   if !(defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
       defined(WOLFBOOT_SIGN_ML_DSA)) &&          \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#   define WC_NO_RNG
#   endif
#   define WC_NO_HASHDRBG
#   define NO_DEV_RANDOM
#   if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#     define NO_ECC_KEY_EXPORT
#     if defined(NO_RSA)
#       define NO_ASN
#     endif
#   endif
#endif

/* Algorithms and features not used */
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
#ifndef NO_DES3
#define NO_DES3
#endif
#define NO_WRITEV
#ifndef WOLFBOOT_PARTITION_FILENAME
#define NO_FILESYSTEM
#endif
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_IGNORE_FILE_WARN
#define NO_ERROR_STRINGS
#define NO_PKCS12
#define NO_PKCS8
#define NO_CHECK_PRIVATE_KEY
#define NO_KDF

/* wolfCrypt Test/Benchmark Configuration */
#ifdef WOLFCRYPT_TEST
    /* Skip extended tests to save memory */
    #define NO_CRYPT_TEST_EXTENDED
    /* Use smaller certificate buffers */
    #define USE_CERT_BUFFERS_256
    /* Override default NO_CRYPT_TEST */
    #undef NO_CRYPT_TEST
#else
    #define NO_CRYPT_TEST
#endif

#ifdef WOLFCRYPT_BENCHMARK
    /* Embedded benchmark mode */
    #ifndef BENCH_EMBEDDED
        #define BENCH_EMBEDDED
    #endif
    /* Override default NO_CRYPT_BENCHMARK */
    #undef NO_CRYPT_BENCHMARK
#else
    #define NO_CRYPT_BENCHMARK
#endif

/* Common optimizations when test/benchmark enabled */
#if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
    #define NO_WRITE_TEMP_FILES

    /* Use static memory pool to avoid system malloc dependency.
     * benchmark.c provides gBenchMemory static buffer.
     * Default is 50KB with BENCH_EMBEDDED, override for smaller targets */
    #ifndef WOLFSSL_STATIC_MEMORY
        #define WOLFSSL_STATIC_MEMORY
    #endif
    #ifndef WOLFSSL_STATIC_MEMORY_TEST_SZ
        #define WOLFSSL_STATIC_MEMORY_TEST_SZ (10 * 1024)
    #endif

    /* Enable SP math digit operations */
    #define WOLFSSL_SP_MUL_D

    /* User time functions provided */
    #define WOLFSSL_USER_CURRTIME
    #define XTIME my_time
    extern unsigned long my_time(unsigned long* timer);
#endif

#if !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
    #define BENCH_EMBEDDED
#endif

#if defined(WOLFCRYPT_TZ_PSA)
#undef NO_CMAC
#undef NO_KDF
#endif

#ifdef __QNX__
#   define WOLFSSL_HAVE_MIN
#   define WOLFSSL_HAVE_MAX
#endif


/* Memory model */
#if defined(WOLFSSL_SP_MATH) || defined(WOLFSSL_SP_MATH_ALL)
    /* Disable VLAs */
#   define WOLFSSL_SP_NO_DYN_STACK
#endif

#if defined(WOLFBOOT_SMALL_STACK)
#   if defined(WOLFBOOT_HUGE_STACK)
#       error "Cannot use SMALL_STACK=1 with HUGE_STACK=1"
#   endif
#   define WOLFSSL_SMALL_STACK
#else
#   if defined(WOLFSSL_SP_MATH) || defined(WOLFSSL_SP_MATH_ALL)
#       define WOLFSSL_SP_NO_MALLOC
#       define WOLFSSL_SP_NO_DYN_STACK
#   endif
#   if !defined(SECURE_PKCS11) && !defined(WOLFCRYPT_TZ_PSA) && \
       !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
       !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#       define NO_WOLFSSL_MEMORY
#       define WOLFSSL_NO_MALLOC
#   endif
#endif


/* Renesas */
#if defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP) || \
    defined(WOLFBOOT_RENESAS_SCEPROTECT)

    #define WOLFBOOT_SMALL_STACK
    #define WOLF_CRYPTO_CB
    #define WOLF_CRYPTO_CB_ONLY_ECC
    #define WOLF_CRYPTO_CB_ONLY_RSA
    #define WOLFSSL_NO_SW_MATH
    #define MAX_CRYPTO_DEVID_CALLBACKS 2
    #define WC_NO_DEFAULT_DEVID
    #define WOLFSSL_AES_SMALL_TABLES

    #ifdef WOLFBOOT_RENESAS_TSIP
        #define WOLFSSL_RENESAS_TSIP
        #define WOLFSSL_RENESAS_TSIP_VER  117
        #define WOLFSSL_RENESAS_TSIP_CRYPT
        #define WOLFSSL_RENESAS_TSIP_CRYPTONLY
        #define NO_WOLFSSL_RENESAS_TSIP_CRYPT_HASH
        #define RENESAS_TSIP_INSTALLEDKEY_ADDR 0xFFFF0000
        #ifndef RENESAS_TSIP_INSTALLEDENCKEY_ADDR
            #define RENESAS_TSIP_INSTALLEDENCKEY_ADDR \
                (RENESAS_TSIP_INSTALLEDKEY_ADDR + 0x100)
        #endif
        #define ENCRYPTED_KEY_BYTE_SIZE ENC_PUB_KEY_SIZE
        #define RENESAS_DEVID 7890
    #endif
    #ifdef WOLFBOOT_RENESAS_SCEPROTECT
        #define WOLFSSL_RENESAS_SCEPROTECT_CRYPTONLY
        #define RENESAS_SCE_INSTALLEDKEY_ADDR 0x08001000U
        #define SCE_ID 7890
    #endif
    #ifdef WOLFBOOT_RENESAS_RSIP
        #define WOLFSSL_RENESAS_FSPSM
        #define WOLFSSL_RENESAS_FSPSM_CRYPTONLY
        #define WOLFSSL_RENESAS_RSIP_CRYPTONLY
        #undef  WOLFSSL_RENESAS_FSPSM_TLS
        #define RENESAS_RSIP_INSTALLEDKEY_FLASH_ADDR  0x60200000
        #define RENESAS_RSIP_INSTALLEDKEY_RAM_ADDR    0x10000100
        #define RENESAS_DEVID 7890
    #endif
#endif
#endif /* WOLFBOOT_PKCS11_APP */

#ifndef XTOLOWER
#define XTOLOWER(x) (x)
#endif

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#   define WOLF_CRYPTO_CB
#   undef  HAVE_ANONYMOUS_INLINE_AGGREGATES
#   define HAVE_ANONYMOUS_INLINE_AGGREGATES 1
#   define WOLFSSL_KEY_GEN
#endif /* WOLFBOOT_ENABLE_WOLFHSM_CLIENT || WOLFBOOT_ENABLE_WOLFHSM_SERVER */

#if defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
    defined(WOLFBOOT_CERT_CHAIN_VERIFY)
#   define NO_TLS
#   define NO_OLD_TLS
#   define WOLFSSL_NO_TLS12
#   define WOLFSSL_USER_IO
#   define WOLFSSL_SP_MUL_D
#   define WOLFSSL_PEM_TO_DER
#   define WOLFSSL_ALLOW_NO_SUITES
#endif

#ifdef WOLFSSL_STM32_PKA
#define HAVE_UINTPTR_T /* make sure stdint.h is included */
#endif

#endif /* !_WOLFBOOT_USER_SETTINGS_H_ */
