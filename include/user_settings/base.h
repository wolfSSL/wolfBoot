/* user_settings/base.h
 *
 * Foundation defines that every wolfBoot build needs regardless of which
 * SIGN/HASH/feature flags are set: alignment, threading, stdlib types,
 * basic sizing.
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
#ifndef _WOLFBOOT_USER_SETTINGS_BASE_H_
#define _WOLFBOOT_USER_SETTINGS_BASE_H_

/* System */
#define WOLFSSL_GENERAL_ALIGNMENT 4
#define SINGLE_THREADED
#define WOLFSSL_USER_MUTEX /* avoid wc_port.c wc_InitAndAllocMutex */
/* WOLFCRYPT_ONLY: pure crypto, no TLS/SSL stack. The only configuration
 * that needs the SSL layer (cert manager) is wolfHSM server + cert-chain
 * verification, where the carve-out moves to user_settings/cert_chain.h
 * in a later phase. */
#if !(defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
        defined(WOLFBOOT_CERT_CHAIN_VERIFY))
#  define WOLFCRYPT_ONLY
#endif
#define SIZEOF_LONG_LONG 8
#define HAVE_EMPTY_AGGREGATES 0
#define HAVE_ANONYMOUS_INLINE_AGGREGATES 0

/* Stdlib Types */
#define CTYPE_USER /* don't let wolfCrypt types.h include ctype.h */

#ifndef WOLFSSL_ARMASM
#  ifndef toupper
extern int toupper(int c);
#  endif
#  ifndef tolower
extern int tolower(int c);
#  endif
#  define XTOUPPER(c)     toupper((c))
#  define XTOLOWER(c)     tolower((c))
#endif

#ifdef USE_FAST_MATH
    /* wolfBoot only does public asymmetric operations,
     * so timing resistance and hardening is not required */
#   define WC_NO_HARDEN
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_BASE_H_ */
