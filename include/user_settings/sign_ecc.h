/* user_settings/sign_ecc.h
 *
 * wolfCrypt configuration for ECC (P-256, P-384, P-521) signature
 * verification.
 *
 * Active when any WOLFBOOT_SIGN_ECC{256,384,521} (or
 * WOLFBOOT_SIGN_SECONDARY_ECC*) is defined, or when
 * WOLFCRYPT_SECURE_MODE / WOLFCRYPT_TEST / WOLFCRYPT_BENCHMARK is on.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_ECC_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_ECC_H_

/* sign_dispatch.h has already gated on the ECC trigger conditions before
 * including this fragment, so we don't repeat the outer #if chain. The
 * inner branches (ECC carve-outs for TPM/HSM/SECURE_MODE/TEST/BENCHMARK)
 * still need to fire here -- those collapse into NEEDS_ASYM_SIGN /
 * NEEDS_ASYM_KEYEXPORT in Phase 4. */

#define HAVE_ECC
#define ECC_TIMING_RESISTANT
#define ECC_USER_CURVES /* enables only 256-bit by default */

/* Kinetis LTC support */
#ifdef FREESCALE_USE_LTC
#  define FREESCALE_COMMON
#  define FSL_HW_CRYPTO_MANUAL_SELECTION
#  define FREESCALE_LTC_ECC
#  define FREESCALE_LTC_TFM
#endif

/* Some ECC options are disabled to reduce size */
#if !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK) && \
    !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
    !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  if !defined(WOLFBOOT_TPM)
#    define NO_ECC_SIGN
#    define NO_ECC_DHE
     /* For Renesas RX do not enable the misc.c constant time code
      * due to issue with 64-bit types */
#    if defined(__RX__)
#      define WOLFSSL_NO_CT_OPS /* don't use constant time ops in misc.c */
#    endif
#    if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
        !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#      define NO_ECC_EXPORT
#      define NO_ECC_KEY_EXPORT
#    endif
#  else
#    define HAVE_ECC_KEY_EXPORT
#  endif
#else
#  define HAVE_ECC_SIGN
#  define HAVE_ECC_VERIFY
#  if !defined(PKCS11_SMALL) && !defined(WOLFCRYPT_TEST) && \
      !defined(WOLFCRYPT_BENCHMARK)
#    define HAVE_ECC_CDH
#  endif
#  define WOLFSSL_SP_MATH
#  define WOLFSSL_SP_SMALL
#  define WOLFSSL_HAVE_SP_ECC
#  define WOLFSSL_KEY_GEN
#  define HAVE_ECC_KEY_EXPORT
#  define HAVE_ECC_KEY_IMPORT
#endif

/* SP MATH default for builds that did not go through the secure-mode/
 * test/bench/wolfHSM #else branch above (which already sets these). */
#if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL) && \
    !defined(WOLFSSL_SP_MATH)
#  define WOLFSSL_SP_MATH
#  define WOLFSSL_SP_SMALL
#  define WOLFSSL_HAVE_SP_ECC
#endif

#define WOLFSSL_PUBLIC_MP

/* Curve */
#if defined(WOLFBOOT_SIGN_ECC256)           || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC256) || \
    defined(WOLFCRYPT_SECURE_MODE)          || \
    defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#  define HAVE_ECC256
#endif
#if defined(WOLFBOOT_SIGN_ECC384)           || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC384) || \
    defined(WOLFCRYPT_SECURE_MODE)          || \
    defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#  define HAVE_ECC384
#  define WOLFSSL_SP_384
#endif
/* ECC521 only enabled if specifically requested (not for tests - too large) */
#if defined(WOLFBOOT_SIGN_ECC521)           || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC521) || \
    defined(WOLFCRYPT_SECURE_MODE)
#  define HAVE_ECC521
#  define WOLFSSL_SP_521
#endif

/* FP MAX BITS */
#if defined(HAVE_ECC521)
#  define FP_MAX_BITS ((528 * 2))
#elif defined(HAVE_ECC384)
#  define FP_MAX_BITS ((384 * 2))
#elif defined(HAVE_ECC256)
#  define FP_MAX_BITS ((256 + 32))
#endif

#if !defined(HAVE_ECC256) && !defined(WOLFBOOT_TPM_PARMENC)
#  define NO_ECC256
#endif

#if !defined(USE_FAST_MATH) && !defined(WOLFSSL_SP_MATH_ALL)
#  if !defined(HAVE_ECC521)
#    define WOLFSSL_SP_NO_521
#  endif
#  if !defined(HAVE_ECC384)
#    define WOLFSSL_SP_NO_384
#  endif
#  if !defined(HAVE_ECC256)
#    define WOLFSSL_SP_NO_256
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_ECC_H_ */
