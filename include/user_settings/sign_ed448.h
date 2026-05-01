/* user_settings/sign_ed448.h
 *
 * wolfCrypt configuration for ED448 signature verification.
 *
 * Active when WOLFBOOT_SIGN_ED448 (primary) or
 * WOLFBOOT_SIGN_SECONDARY_ED448 (hybrid) is defined.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_ED448_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_ED448_H_

#if defined(WOLFBOOT_SIGN_ED448) || defined(WOLFBOOT_SIGN_SECONDARY_ED448)
#  define HAVE_ED448
#  define HAVE_ED448_VERIFY
#  define ED448_SMALL
#  if !defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER)
#    define NO_ED448_SIGN
#    define NO_ED448_EXPORT
#  endif
#  define WOLFSSL_SHA3
#  define WOLFSSL_SHAKE256
#  define WOLFSSL_SHA512
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_ED448_H_ */
