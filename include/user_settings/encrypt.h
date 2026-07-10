/* user_settings/encrypt.h
 *
 * wolfCrypt configuration for image encryption (EXT_ENCRYPTED) and the
 * SECURE_PKCS11 store. The cipher selection (ChaCha20 vs AES-128 vs
 * AES-256 vs PKCS#11-backed) lives in options.mk; this fragment owns the
 * wolfCrypt-side gates that follow from those choices.
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
#ifndef _WOLFBOOT_USER_SETTINGS_ENCRYPT_H_
#define _WOLFBOOT_USER_SETTINGS_ENCRYPT_H_

#if defined(EXT_ENCRYPTED)
#  define HAVE_PWDBASED
#endif

#if defined(SECURE_PKCS11)
#  include <time.h>
#  define HAVE_PWDBASED
#  define HAVE_PBKDF2
#  define WOLFPKCS11_CUSTOM_STORE
#  define WOLFBOOT_SECURE_PKCS11
#  ifndef WOLFPKCS11_USER_SETTINGS
#    define WOLFPKCS11_USER_SETTINGS
#  endif
#  define WOLFPKCS11_NO_TIME
#  ifndef WOLFSSL_AES_COUNTER
#    define WOLFSSL_AES_COUNTER
#  endif
#  define HAVE_AESCTR
#  ifndef WOLFSSL_AES_DIRECT
#    define WOLFSSL_AES_DIRECT
#  endif
#  define WOLFSSL_AES_GCM
#  define GCM_TABLE_4BIT
#  define WOLFSSL_AES_128
#  define HAVE_SCRYPT
#  define HAVE_AESGCM
#  define HAVE_PKCS8
#endif

/* PKCS11 for wolfBoot is always static. */
#define HAVE_PKCS11_STATIC

/* The NO_PWDBASED fallback (when no fragment opted in) lives in
 * user_settings/finalize.h so it runs after trustzone.h / tpm.h /
 * test_bench.h have had a chance to set HAVE_PWDBASED. */

#endif /* _WOLFBOOT_USER_SETTINGS_ENCRYPT_H_ */
