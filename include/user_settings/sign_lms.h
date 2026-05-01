/* user_settings/sign_lms.h
 *
 * wolfCrypt configuration for LMS (NIST SP 800-208) hash-based stateful
 * signature verification. Active when WOLFBOOT_SIGN_LMS is defined.
 *
 * The parameter values (LMS_LEVELS, LMS_HEIGHT, LMS_WINTERNITZ,
 * IMAGE_SIGNATURE_SIZE) are still set by options.mk from the .config
 * file, since they are user values per build. This fragment turns those
 * values into the wolfCrypt-side WOLFSSL_LMS_* defines.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_LMS_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_LMS_H_

#define WOLFSSL_HAVE_LMS
#define WOLFSSL_WC_LMS
#define WOLFSSL_WC_LMS_SMALL
#define WOLFSSL_LMS_VERIFY_ONLY
#define WOLFSSL_LMS_MAX_LEVELS LMS_LEVELS
#define WOLFSSL_LMS_MAX_HEIGHT LMS_HEIGHT
#define LMS_IMAGE_SIGNATURE_SIZE IMAGE_SIGNATURE_SIZE

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_LMS_H_ */
