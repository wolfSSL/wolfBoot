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

/* ------------------------------------------------------------------
 * NEEDS_* reconciliation
 * ------------------------------------------------------------------
 * Each negative wolfCrypt flag (NO_*, WC_NO_*) is gated by the absence
 * of its matching WOLFBOOT_NEEDS_* marker. Markers are declared in
 * cascade.h (from feature flags) or in fragment headers. */

/* HASHDRBG: positive when needed, WC_NO_HASHDRBG otherwise.
 * Note: test_bench.h's non-LPC55S69 path explicitly defines
 * WC_NO_HASHDRBG itself; this `#ifndef` won't redefine it. */
#ifdef WOLFBOOT_NEEDS_HASHDRBG
#  ifndef HAVE_HASHDRBG
#    define HAVE_HASHDRBG
#  endif
#else
#  ifndef WC_NO_HASHDRBG
#    define WC_NO_HASHDRBG
#  endif
#endif

#ifndef WOLFBOOT_NEEDS_RNG
#  define WC_NO_RNG
#endif

#ifndef WOLFBOOT_NEEDS_AES
#  define NO_AES
#endif
#ifndef WOLFBOOT_NEEDS_AES_CBC
#  define NO_AES_CBC
#endif
#ifndef WOLFBOOT_NEEDS_HMAC
#  define NO_HMAC
#endif
#ifndef WOLFBOOT_NEEDS_DEV_RANDOM
#  define NO_DEV_RANDOM
#endif
#ifndef WOLFBOOT_NEEDS_ECC_KEY_EXPORT
#  define NO_ECC_KEY_EXPORT
#endif
#ifndef WOLFBOOT_NEEDS_ASN
#  define NO_ASN
#endif
#ifndef WOLFBOOT_NEEDS_CMAC
#  define NO_CMAC
#endif
#ifndef WOLFBOOT_NEEDS_KDF
#  define NO_KDF
#endif

/* RSA: skip NO_RSA when NEEDS_RSA is set. */
#ifndef WOLFBOOT_NEEDS_RSA
#  define NO_RSA
#endif

/* HAVE_PWDBASED is opted into by EXT_ENCRYPTED, SECURE_PKCS11, and
 * WOLFCRYPT_TZ_PSA. If none of them set it, default to NO_PWDBASED. */
#ifndef HAVE_PWDBASED
#  define NO_PWDBASED
#endif

/* BASE64 / NO_CODING. */
#ifdef WOLFBOOT_NEEDS_BASE64
#  define WOLFSSL_BASE64_ENCODE
#else
#  define NO_CODING
#endif

/* ------------------------------------------------------------------
 * Always-on disables (no fragment opts out today).
 * ------------------------------------------------------------------
 * Each entry asserts that no fragment has opted in via the matching
 * positive flag, then defines the disable. If one of these assertions
 * fires, the right fix is to introduce a WOLFBOOT_NEEDS_* marker and
 * gate the disable on its absence (see docs/wolfssl-config.md, Section
 * 8 Step 5).
 *
 * Entries without an assertion either have no canonical positive form
 * (NO_ASN_TIME, NO_SIG_WRAPPER) or describe wolfBoot's environment
 * rather than a wolfCrypt feature a fragment would plausibly want to
 * enable (NO_WRITEV, NO_MAIN_DRIVER, NO_WOLFSSL_DIR, WOLFSSL_NO_SOCK,
 * WOLFSSL_IGNORE_FILE_WARN, NO_ERROR_STRINGS, NO_OLD_RNGNAME). */

#if defined(HAVE_DH)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_DH

#if defined(WOLFSSL_PEM)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define WOLFSSL_NO_PEM

#define NO_ASN_TIME

#if defined(HAVE_RC4)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_RC4

#if defined(WOLFSSL_SHA1)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_SHA

#if defined(HAVE_DSA)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_DSA

#if defined(WOLFSSL_MD4)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_MD4

#if defined(HAVE_RABBIT)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_RABBIT

#if defined(WOLFSSL_MD5)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_MD5

#define NO_SIG_WRAPPER

#if defined(WOLFSSL_CERT_GEN)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_CERT

#if defined(HAVE_SESSION_CACHE)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_SESSION_CACHE

#if defined(HAVE_HC128)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_HC128

#if defined(HAVE_DES3)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
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

#if defined(HAVE_PKCS12)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_PKCS12

/* NO_PKCS8: no assertion. encrypt.h's SECURE_PKCS11 path defines
 * HAVE_PKCS8 vestigially; wolfSSL gates PKCS8 on `#ifndef NO_PKCS8`,
 * so the HAVE_PKCS8 define is a no-op. Until that vestigial define is
 * cleaned up, we cannot assert here without false-positive on the
 * SECURE_PKCS11 build. */
#define NO_PKCS8

#if defined(WOLFSSL_CHECK_PRIVATE_KEY)
#  error "user_settings: NEEDS_* marker required; see docs/wolfssl-config.md"
#endif
#define NO_CHECK_PRIVATE_KEY

/* BENCH_EMBEDDED is the default outside explicit test/benchmark mode. */
#if !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define BENCH_EMBEDDED
#endif

/* ------------------------------------------------------------------
 * Memory model.
 * ------------------------------------------------------------------ */
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
#  ifndef WOLFBOOT_NEEDS_MALLOC
#    define NO_WOLFSSL_MEMORY
#    define WOLFSSL_NO_MALLOC
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_FINALIZE_H_ */
