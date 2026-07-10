/* user_settings/sign_ed25519.h
 *
 * wolfCrypt configuration for ED25519 signature verification.
 *
 * Active when WOLFBOOT_SIGN_ED25519 (primary) or
 * WOLFBOOT_SIGN_SECONDARY_ED25519 (hybrid) is defined. Pulled in by
 * user_settings/sign_dispatch.h.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_ED25519_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_ED25519_H_

#if defined(WOLFBOOT_SIGN_ED25519) || defined(WOLFBOOT_SIGN_SECONDARY_ED25519)
#  define HAVE_ED25519
#  define ED25519_SMALL
/* Verify-only by default. wolfHSM-server is the only build that needs to
 * sign too -- in Phase 4 this carve-out becomes
 *   #ifndef WOLFBOOT_NEEDS_ASYM_SIGN
 *     #define NO_ED25519_SIGN
 *     #define NO_ED25519_EXPORT
 *   #endif
 * with wolfhsm.h declaring WOLFBOOT_NEEDS_ASYM_SIGN when SERVER is set. */
#  if !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define NO_ED25519_SIGN
#    define NO_ED25519_EXPORT
#  endif
#  define USE_SLOW_SHA512
#  define WOLFSSL_SHA512
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_ED25519_H_ */
