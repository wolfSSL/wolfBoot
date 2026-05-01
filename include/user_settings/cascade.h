/* user_settings/cascade.h
 *
 * Lift Make-side feature implications into preprocessor cascades, and
 * declare WOLFBOOT_NEEDS_* positive intent markers used by the rest
 * of the user_settings/ fragments and reconciled in finalize.h.
 *
 * Idempotent: every #define is #ifndef-guarded, so it's a no-op when
 * options.mk has already emitted the same -D flag.
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
#ifndef _WOLFBOOT_USER_SETTINGS_CASCADE_H_
#define _WOLFBOOT_USER_SETTINGS_CASCADE_H_

/* ------------------------------------------------------------------
 * Feature-flag cascades
 * ------------------------------------------------------------------ */

/* Any feature that requires a hardware TPM 2.0 implies WOLFBOOT_TPM.
 * Mirrors options.mk:34-92 where the same Make variables force WOLFTPM:=1. */
#if defined(WOLFBOOT_TPM_VERIFY)   || \
    defined(WOLFBOOT_MEASURED_BOOT) || \
    defined(WOLFBOOT_TPM_KEYSTORE) || \
    defined(WOLFBOOT_TPM_SEAL)
#  ifndef WOLFBOOT_TPM
#    define WOLFBOOT_TPM
#  endif
#endif

/* TPM keystore and seal both require TPM session parameter encryption. */
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
#  ifndef WOLFBOOT_TPM_PARMENC
#    define WOLFBOOT_TPM_PARMENC
#  endif
#endif

/* Any RSA SIGN flag (or WOLFCRYPT_SECURE_MODE without PKCS11_SMALL) means
 * the build links wolfCrypt's RSA code. sign_rsa.h handles the actual
 * configuration; the marker is set here so finalize.h can see it ahead
 * of finalize-time and skip NO_ASN. */
#if defined(WOLFBOOT_SIGN_RSA2048)            || \
    defined(WOLFBOOT_SIGN_RSA3072)            || \
    defined(WOLFBOOT_SIGN_RSA4096)            || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA2048)  || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA3072)  || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA4096)  || \
    defined(WOLFBOOT_SIGN_RSAPSS2048)         || \
    defined(WOLFBOOT_SIGN_RSAPSS3072)         || \
    defined(WOLFBOOT_SIGN_RSAPSS4096)         || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS2048) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS3072) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS4096) || \
    (defined(WOLFCRYPT_SECURE_MODE) && !defined(PKCS11_SMALL))
#  ifndef WOLFBOOT_NEEDS_RSA
#    define WOLFBOOT_NEEDS_RSA
#  endif
#endif

/* ------------------------------------------------------------------
 * WOLFBOOT_NEEDS_* declarations
 * ------------------------------------------------------------------
 * Positive intent markers. user_settings/finalize.h tests them and
 * applies the corresponding wolfCrypt negative flag (NO_*, WC_NO_*) to
 * builds that did NOT opt in. Fragments may also set additional markers
 * from their own headers. */

/* NEEDS_RNG: any feature that uses wolfCrypt's RNG.
 * Driven by: TPM parm-enc, secure-mode (TZ-PSA / TZ-FWTPM), test/bench,
 * wolfHSM server, and wolfHSM client + ML-DSA. */
#if defined(WOLFBOOT_TPM_PARMENC)            || \
    defined(WOLFCRYPT_SECURE_MODE)           || \
    defined(WOLFCRYPT_TEST)                  || \
    defined(WOLFCRYPT_BENCHMARK)             || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)  || \
    (defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
     defined(WOLFBOOT_SIGN_ML_DSA))
#  ifndef WOLFBOOT_NEEDS_RNG
#    define WOLFBOOT_NEEDS_RNG
#  endif
#endif

/* NEEDS_HASHDRBG: features that use wolfCrypt's HASHDRBG specifically.
 * Note: TEST/BENCH non-LPC55S69 builds use a custom RNG and do NOT
 * declare this marker; their explicit `#define WC_NO_HASHDRBG` lives
 * in test_bench.h. */
#if defined(WOLFBOOT_TPM_PARMENC)  || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    ((defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)) && \
     (defined(WOLFSSL_NXP_LPC55S69_WITH_HWACCEL) || \
      defined(WOLFSSL_NXP_LPC55S69_NO_HWACCEL)))
#  ifndef WOLFBOOT_NEEDS_HASHDRBG
#    define WOLFBOOT_NEEDS_HASHDRBG
#  endif
#endif

/* NEEDS_AES_CBC: features that use AES-CBC (entropy-using paths). */
#if defined(WOLFBOOT_TPM_PARMENC)  || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(WOLFCRYPT_TEST)        || \
    defined(WOLFCRYPT_BENCHMARK)
#  ifndef WOLFBOOT_NEEDS_AES_CBC
#    define WOLFBOOT_NEEDS_AES_CBC
#  endif
#endif

/* NEEDS_AES: features that use AES core. */
#if defined(ENCRYPT_WITH_AES128)   || \
    defined(ENCRYPT_WITH_AES256)   || \
    defined(WOLFBOOT_TPM_PARMENC)  || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(SECURE_PKCS11)         || \
    defined(WOLFCRYPT_TZ_PSA)      || \
    defined(WOLFCRYPT_TEST)        || \
    defined(WOLFCRYPT_BENCHMARK)
#  ifndef WOLFBOOT_NEEDS_AES
#    define WOLFBOOT_NEEDS_AES
#  endif
#endif

/* NEEDS_HMAC: features that use HMAC. */
#if defined(WOLFBOOT_TPM)          || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(WOLFCRYPT_TEST)        || \
    defined(WOLFCRYPT_BENCHMARK)
#  ifndef WOLFBOOT_NEEDS_HMAC
#    define WOLFBOOT_NEEDS_HMAC
#  endif
#endif

/* NEEDS_DEV_RANDOM: features that may want OS /dev/random as entropy. */
#if defined(WOLFBOOT_TPM)          || \
    defined(WOLFCRYPT_SECURE_MODE) || \
    defined(WOLFCRYPT_TEST)        || \
    defined(WOLFCRYPT_BENCHMARK)
#  ifndef WOLFBOOT_NEEDS_DEV_RANDOM
#    define WOLFBOOT_NEEDS_DEV_RANDOM
#  endif
#endif

/* NEEDS_ECC_KEY_EXPORT: features that need to export ECC keys. */
#if defined(WOLFBOOT_TPM)                    || \
    defined(WOLFCRYPT_SECURE_MODE)           || \
    defined(WOLFCRYPT_TEST)                  || \
    defined(WOLFCRYPT_BENCHMARK)             || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)  || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  ifndef WOLFBOOT_NEEDS_ECC_KEY_EXPORT
#    define WOLFBOOT_NEEDS_ECC_KEY_EXPORT
#  endif
#endif

/* NEEDS_ASN: features that need ASN.1 parsing. NEEDS_RSA also implies
 * this (RSA always parses ASN.1). */
#if defined(WOLFBOOT_NEEDS_RSA)              || \
    defined(WOLFBOOT_TPM)                    || \
    defined(WOLFCRYPT_SECURE_MODE)           || \
    defined(WOLFCRYPT_TEST)                  || \
    defined(WOLFCRYPT_BENCHMARK)             || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)  || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  ifndef WOLFBOOT_NEEDS_ASN
#    define WOLFBOOT_NEEDS_ASN
#  endif
#endif

/* NEEDS_BASE64: features that use base64 encoding. */
#if (defined(WOLFBOOT_TPM_SEAL) && defined(WOLFBOOT_ATA_DISK_LOCK)) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT)  || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  ifndef WOLFBOOT_NEEDS_BASE64
#    define WOLFBOOT_NEEDS_BASE64
#  endif
#endif

/* NEEDS_CMAC and NEEDS_KDF: TZ_PSA and TZ_FWTPM need both. */
#if defined(WOLFCRYPT_TZ_PSA) || defined(WOLFBOOT_TZ_FWTPM)
#  ifndef WOLFBOOT_NEEDS_CMAC
#    define WOLFBOOT_NEEDS_CMAC
#  endif
#  ifndef WOLFBOOT_NEEDS_KDF
#    define WOLFBOOT_NEEDS_KDF
#  endif
#endif

/* NEEDS_MALLOC: features whose code-paths use heap allocation.
 * SECURE_PKCS11, WOLFCRYPT_TZ_PSA, the wolfHSM server, and the
 * test/bench harnesses all expect a working malloc. Default builds
 * (no marker) get NO_WOLFSSL_MEMORY + WOLFSSL_NO_MALLOC instead. */
#if defined(SECURE_PKCS11)                  || \
    defined(WOLFCRYPT_TZ_PSA)               || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) || \
    defined(WOLFCRYPT_TEST)                 || \
    defined(WOLFCRYPT_BENCHMARK)
#  ifndef WOLFBOOT_NEEDS_MALLOC
#    define WOLFBOOT_NEEDS_MALLOC
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_CASCADE_H_ */
