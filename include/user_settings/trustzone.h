/* user_settings/trustzone.h
 *
 * wolfCrypt configuration for ARMv8-M TrustZone secure-mode builds:
 * WOLFCRYPT_SECURE_MODE (the umbrella, set by options.mk for any TZ
 * sub-mode), WOLFCRYPT_TZ_PSA, and WOLFBOOT_TZ_FWTPM. The TZ_PKCS11
 * variant has its own cascade in encrypt.h via SECURE_PKCS11.
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
#ifndef _WOLFBOOT_USER_SETTINGS_TRUSTZONE_H_
#define _WOLFBOOT_USER_SETTINGS_TRUSTZONE_H_

#ifdef WOLFCRYPT_SECURE_MODE
   int hal_trng_get_entropy(unsigned char *out, unsigned len);
#  define CUSTOM_RAND_GENERATE_SEED hal_trng_get_entropy
   /* SECURE_MODE outside test/bench used to enable AES CFB explicitly
    * via the finalize.h else-else branch. Keep that behavior here so
    * the TZ-PSA, TZ-FWTPM, and any future SECURE_MODE sub-mode all
    * have AES CFB available. */
#  ifndef WOLFSSL_AES_CFB
#    define WOLFSSL_AES_CFB
#  endif
#endif

#if defined(WOLFCRYPT_TZ_PSA)
   /* WOLFSSL_AES_CFB is set by the SECURE_MODE block above (TZ_PSA implies
    * SECURE_MODE in options.mk). */
#  define WOLFSSL_AES_COUNTER
#  define WOLFSSL_AES_GCM
#  define HAVE_AESGCM
#  define HAVE_AESCCM
#  define HAVE_AES_ECB
#  define WOLFSSL_AES_OFB
#  ifndef NO_DES3
#    define NO_DES3
#  endif
#  ifndef NO_DES3_TLS_SUITES
#    define NO_DES3_TLS_SUITES
#  endif
#  define HAVE_CHACHA
#  define HAVE_POLY1305
#  define WOLFSSL_CMAC
#  define WOLFSSL_ECDSA_DETERMINISTIC_K
#  define WOLFSSL_HAVE_PRF
#  define HAVE_HKDF
#  define HAVE_PBKDF2
#  define HAVE_PWDBASED
#  define WOLFSSL_KEY_GEN
#  define WC_RSA_PSS
#  define WOLFSSL_PSS_SALT_LEN_DISCOVER
#  define WOLFSSL_RSA_OAEP
#  define HAVE_ECC_KEY_EXPORT
#  define HAVE_ECC_KEY_IMPORT
#endif

#if defined(WOLFBOOT_TZ_FWTPM)
   /* WOLFSSL_AES_CFB is set by the SECURE_MODE block above (TZ_FWTPM
    * implies SECURE_MODE in options.mk). */
#  define WOLFSSL_SHA384
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_TRUSTZONE_H_ */
