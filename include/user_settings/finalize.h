/* user_settings/finalize.h
 *
 * Reconciliation header. Always included LAST after every fragment has had
 * a chance to declare what it needs. Translates positive intent markers
 * (WOLFBOOT_NEEDS_*) into wolfCrypt negative flags (NO_*, WC_NO_*), and
 * sets the always-on global "off" defaults wolfBoot uses to strip code
 * size from the wolfCrypt linker output.
 *
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
#ifndef _WOLFBOOT_USER_SETTINGS_FINALIZE_H_
#define _WOLFBOOT_USER_SETTINGS_FINALIZE_H_

/* WOLFBOOT_NEEDS_* reconciliation
 * ------------------------------------------------------------------
 * Positive intent markers from cascade.h or feature fragments map here
 * to wolfCrypt's negative (NO_*, WC_NO_*) flags. The full marker
 * vocabulary is documented in the refactor plan. */
#ifndef WOLFBOOT_NEEDS_CMAC
#  define NO_CMAC
#endif
#ifndef WOLFBOOT_NEEDS_KDF
#  define NO_KDF
#endif

/* HAVE_PWDBASED is opted into by EXT_ENCRYPTED, SECURE_PKCS11, and
 * WOLFCRYPT_TZ_PSA. If none of them set it, default to NO_PWDBASED. */
#ifndef HAVE_PWDBASED
#  define NO_PWDBASED
#endif

/* RNG / HASHDRBG / NO_AES_CBC: today disabled unless any of the
 * "needs entropy" features are active. */
#if !defined(WOLFCRYPT_SECURE_MODE) && !defined(WOLFBOOT_TPM_PARMENC) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  if !(defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
        defined(WOLFBOOT_SIGN_ML_DSA)) && \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define WC_NO_RNG
#  endif
#  define WC_NO_HASHDRBG
#  define NO_AES_CBC
#else
#  if defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#    if defined(WOLFSSL_NXP_LPC55S69_WITH_HWACCEL) \
        || defined(WOLFSSL_NXP_LPC55S69_NO_HWACCEL)
       /* use actual rng hardware for seed, HASHDRBG for generation */
#      define HAVE_HASHDRBG
#      define HAVE_AES_ECB
#      define WOLFSSL_AES_OFB
#      define WOLFSSL_AES_CFB
#      define WOLFSSL_AES_COUNTER
#      define WOLFSSL_STATIC_MEMORY_TEST_SZ (30 * 1024)
#      define WOLFSSL_SHA256
#      define WOLFSSL_SHA384
#      define WOLFSSL_SHA512
#    else
       /* Use custom RNG for tests/benchmarks (saves ~7KB vs HASHDRBG).
        * WARNING: my_rng_seed_gen is NOT cryptographically secure.
        * Only used in test-app builds, not in production wolfBoot. */
#      define WC_NO_HASHDRBG
#      define CUSTOM_RAND_GENERATE_SEED my_rng_seed_gen
#      define CUSTOM_RAND_GENERATE_BLOCK my_rng_seed_gen
       extern int my_rng_seed_gen(unsigned char* output, unsigned int sz);
#    endif

#    define HAVE_AESGCM
#    define GCM_TABLE
#  else
#    define HAVE_HASHDRBG
#    define WOLFSSL_AES_CFB
#  endif
#endif

/* AES core: stripped unless any AES-using fragment is active. */
#if !defined(ENCRYPT_WITH_AES128) && !defined(ENCRYPT_WITH_AES256) && \
    !defined(WOLFBOOT_TPM_PARMENC) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(SECURE_PKCS11) && !defined(WOLFCRYPT_TZ_PSA) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define NO_AES
#endif

/* HMAC: stripped unless TPM / secure mode / test/bench is active. */
#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define NO_HMAC
#endif

/* RNG / ECC key export / ASN: second copy of the negated chain for the
 * "no TPM, no secure, no test/bench" path. Distinct from the block above
 * because it runs after WC_NO_HASHDRBG / NO_DEV_RANDOM are decided. */
#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  if !(defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
        defined(WOLFBOOT_SIGN_ML_DSA)) && \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define WC_NO_RNG
#  endif
#  define WC_NO_HASHDRBG
#  define NO_DEV_RANDOM
#  if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define NO_ECC_KEY_EXPORT
#    if defined(NO_RSA)
#      define NO_ASN
#    endif
#  endif
#endif

/* BASE64 / NO_CODING: opt-in via TPM_SEAL+ATA_DISK_LOCK or wolfHSM. */
#if (defined(WOLFBOOT_TPM_SEAL) && defined(WOLFBOOT_ATA_DISK_LOCK)) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  define WOLFSSL_BASE64_ENCODE
#else
#  define NO_CODING
#endif

/* Always-on disables (no fragment opts out). */
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
#  define NO_DES3
#endif
#define NO_WRITEV
#ifndef WOLFBOOT_PARTITION_FILENAME
#  define NO_FILESYSTEM
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

/* BENCH_EMBEDDED is the default outside explicit test/benchmark mode. */
#if !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define BENCH_EMBEDDED
#endif

/* Memory model. */
#if defined(WOLFSSL_SP_MATH) || defined(WOLFSSL_SP_MATH_ALL)
   /* Disable VLAs */
#  define WOLFSSL_SP_NO_DYN_STACK
#endif

#if defined(WOLFBOOT_SMALL_STACK)
#  if defined(WOLFBOOT_HUGE_STACK)
#    error "Cannot use SMALL_STACK=1 with HUGE_STACK=1"
#  endif
#  define WOLFSSL_SMALL_STACK
#else
#  if defined(WOLFSSL_SP_MATH) || defined(WOLFSSL_SP_MATH_ALL)
#    define WOLFSSL_SP_NO_MALLOC
#    define WOLFSSL_SP_NO_DYN_STACK
#  endif
#  if !defined(SECURE_PKCS11) && !defined(WOLFCRYPT_TZ_PSA) && \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
      !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#    define NO_WOLFSSL_MEMORY
#    define WOLFSSL_NO_MALLOC
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_FINALIZE_H_ */
