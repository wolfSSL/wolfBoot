/* user_settings/sign_ml_dsa.h
 *
 * wolfCrypt configuration for ML-DSA (post-quantum, FIPS 204 / Dilithium)
 * signature verification.
 *
 * Active when WOLFBOOT_SIGN_ML_DSA (primary) or
 * WOLFBOOT_SIGN_SECONDARY_ML_DSA (hybrid) is defined.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_ML_DSA_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_ML_DSA_H_

#if defined(WOLFBOOT_SIGN_ML_DSA) || defined(WOLFBOOT_SIGN_SECONDARY_ML_DSA)
#  define HAVE_DILITHIUM
#  define WOLFSSL_WC_DILITHIUM
#  define WOLFSSL_EXPERIMENTAL_SETTINGS
   /* Wolfcrypt builds ML-DSA (dilithium) to the FIPS 204 final
    * standard by default. Uncomment this if you want the draft
    * version instead. */
#  if 0
#    define WOLFSSL_DILITHIUM_FIPS204_DRAFT
#  endif
#  define WOLFSSL_DILITHIUM_VERIFY_ONLY
#  define WOLFSSL_DILITHIUM_NO_LARGE_CODE
#  define WOLFSSL_DILITHIUM_SMALL
#  define WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM
#  define WOLFSSL_DILITHIUM_VERIFY_NO_MALLOC
/* wolfHSM client/server need ASN.1 for ML-DSA keys traveling over the
 * comm channel. In Phase 4 this becomes
 *   #ifndef WOLFBOOT_NEEDS_ASN
 *     #define WOLFSSL_DILITHIUM_NO_ASN1
 *   #endif
 * with wolfhsm.h declaring WOLFBOOT_NEEDS_ASN. */
#  if !defined(WOLFBOOT_ENABLE_WOLFHSM_CLIENT) && \
      !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define WOLFSSL_DILITHIUM_NO_ASN1
#  endif
   /* dilithium needs these sha functions. */
#  define WOLFSSL_SHA3
#  define WOLFSSL_SHAKE256
#  define WOLFSSL_SHAKE128
#  define WOLFSSL_SP_NO_DYN_STACK
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_ML_DSA_H_ */
