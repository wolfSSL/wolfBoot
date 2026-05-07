/* user_settings/sign_rsa.h
 *
 * wolfCrypt configuration for RSA (PKCS#1 v1.5 and PSS) signature
 * verification.
 *
 * Active when WOLFBOOT_RSA_ENABLED is set by cascade.h (any RSA SIGN
 * flag, or WOLFCRYPT_SECURE_MODE && !PKCS11_SMALL).
 *
 * The companion `NO_RSA` fallback (when RSA isn't enabled) is also in
 * this file, in the #else branch -- so the fragment is included
 * unconditionally and the trigger condition stays in one place.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_RSA_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_RSA_H_

/* This fragment is included unconditionally by user_settings.h. The opt-in
 * condition lives in cascade.h as WOLFBOOT_RSA_ENABLED (single source of
 * truth shared with the WOLFBOOT_NEEDS_RSA marker). The #else branch
 * defines NO_RSA so downstream blocks that test `#if defined(NO_RSA)`
 * (e.g. the hash_sha*.h NO_ASN carve-outs) keep seeing the same value. */

#ifdef WOLFBOOT_RSA_ENABLED

#define WC_RSA_BLINDING
#define WC_RSA_DIRECT
#define RSA_LOW_MEM
#define WC_ASN_HASH_SHA256

#if defined(WOLFBOOT_SIGN_RSAPSS2048)         || \
    defined(WOLFBOOT_SIGN_RSAPSS3072)         || \
    defined(WOLFBOOT_SIGN_RSAPSS4096)         || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS2048) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS3072) || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS4096)
#  define WC_RSA_PSS
#endif

#if !defined(WOLFBOOT_TPM) && !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK) && \
    !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
    !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  define WOLFSSL_RSA_VERIFY_INLINE
#  define WOLFSSL_RSA_VERIFY_ONLY
#  define WOLFSSL_RSA_PUBLIC_ONLY
#  if !defined(WC_RSA_PSS)
#    define WC_NO_RSA_OAEP
#  endif
#  define NO_RSA_BOUNDS_CHECK
#endif

#if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#  define WOLFSSL_HAVE_SP_RSA
#  define WOLFSSL_SP
#  define WOLFSSL_SP_SMALL
#  define WOLFSSL_SP_MATH
#endif

#if defined(WOLFBOOT_SIGN_RSA2048)            || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA2048)  || \
    defined(WOLFBOOT_SIGN_RSAPSS2048)         || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS2048)
#  define FP_MAX_BITS (2048 * 2)
#  define SP_INT_BITS 2048
#  define WOLFSSL_SP_NO_3072
#  define WOLFSSL_SP_NO_4096
#  define WOLFSSL_SP_2048
#  define RSA_MIN_SIZE 2048
#  define RSA_MAX_SIZE 2048
#endif
#if defined(WOLFBOOT_SIGN_RSA3072)            || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA3072)  || \
    defined(WOLFBOOT_SIGN_RSAPSS3072)         || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS3072)
#  define FP_MAX_BITS (3072 * 2)
#  define SP_INT_BITS 3072
#  define WOLFSSL_SP_NO_2048
#  define WOLFSSL_SP_NO_4096
#  define WOLFSSL_SP_3072
#  define RSA_MIN_SIZE 3072
#  define RSA_MAX_SIZE 3072
#endif
#if defined(WOLFBOOT_SIGN_RSA4096)            || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSA4096)  || \
    defined(WOLFBOOT_SIGN_RSAPSS4096)         || \
    defined(WOLFBOOT_SIGN_SECONDARY_RSAPSS4096)
#  define FP_MAX_BITS (4096 * 2)
#  define SP_INT_BITS 4096
#  define WOLFSSL_SP_NO_2048
#  define WOLFSSL_SP_NO_3072
#  define WOLFSSL_SP_4096
#  define RSA_MIN_SIZE 4096
#  define RSA_MAX_SIZE 4096
#endif
#ifdef WOLFCRYPT_SECURE_MODE
#  undef FP_MAX_BITS
#  define FP_MAX_BITS (4096 * 2)
#  define SP_INT_BITS 4096
#  define WOLFSSL_SP_2048
#  define WOLFSSL_SP_3072
#  define WOLFSSL_SP_4096
#  define RSA_MIN_SIZE 2048
#  define RSA_MAX_SIZE 4096
#endif

#else /* !WOLFBOOT_RSA_ENABLED */
#  define NO_RSA
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_RSA_H_ */
