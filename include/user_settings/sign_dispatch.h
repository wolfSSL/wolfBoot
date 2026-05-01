/* user_settings/sign_dispatch.h
 *
 * Dispatches to one or more SIGN-family fragment headers based on which
 * WOLFBOOT_SIGN_<ALG> (or WOLFBOOT_SIGN_SECONDARY_<ALG>) flags are set.
 * Each sign_*.h fragment has its own #ifdef guard, so it is a no-op when
 * the matching flag isn't defined -- but we still gate the #include
 * itself to avoid pulling whole headers into builds that don't need them.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_DISPATCH_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_DISPATCH_H_

/* ED25519 (primary or secondary) */
#if defined(WOLFBOOT_SIGN_ED25519) || defined(WOLFBOOT_SIGN_SECONDARY_ED25519)
#  include "sign_ed25519.h"
#endif

/* ED448 (primary or secondary) */
#if defined(WOLFBOOT_SIGN_ED448) || defined(WOLFBOOT_SIGN_SECONDARY_ED448)
#  include "sign_ed448.h"
#endif

/* ML-DSA / Dilithium (primary or secondary) */
#if defined(WOLFBOOT_SIGN_ML_DSA) || defined(WOLFBOOT_SIGN_SECONDARY_ML_DSA)
#  include "sign_ml_dsa.h"
#endif

/* ECC (primary, secondary, or implicitly enabled by secure-mode/test/bench) */
#if defined(WOLFBOOT_SIGN_ECC256)           || \
    defined(WOLFBOOT_SIGN_ECC384)           || \
    defined(WOLFBOOT_SIGN_ECC521)           || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC256) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC384) || \
    defined(WOLFBOOT_SIGN_SECONDARY_ECC521) || \
    defined(WOLFCRYPT_SECURE_MODE)          || \
    defined(WOLFCRYPT_TEST) || defined(WOLFCRYPT_BENCHMARK)
#  include "sign_ecc.h"
#endif

/* RSA (PKCS#1 v1.5 or PSS, primary or secondary). Included unconditionally
 * so the #else branch in sign_rsa.h can `#define NO_RSA` -- downstream
 * blocks (e.g. the NO_ASN carve-out) read NO_RSA from this fragment. */
#include "sign_rsa.h"

/* LMS (hash-based stateful signature) */
#ifdef WOLFBOOT_SIGN_LMS
#  include "sign_lms.h"
#endif

/* XMSS (hash-based stateful signature) */
#ifdef WOLFBOOT_SIGN_XMSS
#  include "sign_xmss.h"
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_DISPATCH_H_ */
