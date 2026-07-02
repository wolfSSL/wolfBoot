/* user_settings/wolfhsm.h
 *
 * wolfCrypt configuration when wolfBoot is a wolfHSM client or server
 * (including the WOLFCRYPT_TZ_WOLFHSM TrustZone engine): crypto-callback
 * infrastructure, key-gen support, server-side sizing, and the small set
 * of carve-outs that opt out of strict verify-only defaults.
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
#ifndef _WOLFBOOT_USER_SETTINGS_WOLFHSM_H_
#define _WOLFBOOT_USER_SETTINGS_WOLFHSM_H_

#if defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) || \
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) || \
    defined(WOLFCRYPT_TZ_WOLFHSM)
#  define WOLF_CRYPTO_CB
#  undef  HAVE_ANONYMOUS_INLINE_AGGREGATES
#  define HAVE_ANONYMOUS_INLINE_AGGREGATES 1
#  define WOLFSSL_KEY_GEN
#endif

/* Secure-side sizing common to any wolfHSM server build (TZ engine and
 * the verify-only cert-chain server). Gated out of host unit tests. */
#if (defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) || \
     defined(WOLFCRYPT_TZ_WOLFHSM)) && !defined(UNIT_TEST)
   /* Match NS-side WC_MAX_DIGEST_SIZE. NS test-app/wcs/user_settings.h
    * enables WOLFSSL_SHA3 which sets WC_MAX_DIGEST_SIZE = 64. Without
    * SHA384/SHA512 on the secure side, WC_MAX_DIGEST_SIZE caps at
    * SHA256's 32 and wc_ecc_sign_hash (ecc.c:7281) rejects legitimately
    * oversized hashes (e.g. ECDSA truncation tests) with BAD_LENGTH_E. */
#  ifndef WOLFSSL_SHA384
#    define WOLFSSL_SHA384
#  endif
#  ifndef WOLFSSL_SHA512
#    define WOLFSSL_SHA512
#  endif
   /* Match the keycache sizing the wolfHSM test suite is validated
    * against (test/config/wolfhsm_cfg.h: 9 regular + 3 big). The
    * library defaults (8 + 1) are one regular slot short of the
    * suite's peak working set and leave too few big slots for the
    * RSA/ML-DSA cases. */
#  ifndef WOLFHSM_CFG_SERVER_KEYCACHE_COUNT
#    define WOLFHSM_CFG_SERVER_KEYCACHE_COUNT 9
#  endif
#  ifndef WOLFHSM_CFG_SERVER_KEYCACHE_BIG_COUNT
#    define WOLFHSM_CFG_SERVER_KEYCACHE_BIG_COUNT 3
#  endif
#endif

/* AES/HMAC/HKDF are only needed by the STM32H5 TrustZone engine, whose
 * non-secure client routes general crypto (AES/HMAC/HKDF) through the
 * secure server. The AES/HMAC cores themselves stay enabled via the
 * WOLFBOOT_NEEDS_AES / WOLFBOOT_NEEDS_HMAC markers in cascade.h; the
 * verify-only cert-chain server does not service these requests, so it
 * keeps the NO_AES/NO_HMAC defaults from finalize.h. */
#if defined(WOLFCRYPT_TZ_WOLFHSM) && !defined(UNIT_TEST)
#  ifndef WOLFSSL_AES_DIRECT
#    define WOLFSSL_AES_DIRECT
#  endif
#  ifndef HAVE_HKDF
#    define HAVE_HKDF
#  endif
#  ifndef WOLFSSL_AES_COUNTER
#    define WOLFSSL_AES_COUNTER
#  endif
#  ifndef HAVE_AESCTR
#    define HAVE_AESCTR
#  endif
#  ifndef WOLFSSL_AES_GCM
#    define WOLFSSL_AES_GCM
#  endif
#  ifndef HAVE_AESGCM
#    define HAVE_AESGCM
#  endif
#  ifndef GCM_TABLE_4BIT
#    define GCM_TABLE_4BIT
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_WOLFHSM_H_ */
