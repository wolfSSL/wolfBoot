/* user_settings/wolfhsm.h
 *
 * wolfCrypt configuration when wolfBoot is a wolfHSM client or server:
 * crypto-callback infrastructure, key-gen support, and the small set
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
    defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#  define WOLF_CRYPTO_CB
#  undef  HAVE_ANONYMOUS_INLINE_AGGREGATES
#  define HAVE_ANONYMOUS_INLINE_AGGREGATES 1
#  define WOLFSSL_KEY_GEN
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_WOLFHSM_H_ */
