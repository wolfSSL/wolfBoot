/* user_settings/sign_xmss.h
 *
 * wolfCrypt configuration for XMSS (RFC 8391) hash-based stateful
 * signature verification. Active when WOLFBOOT_SIGN_XMSS is defined.
 *
 * Parameter values (XMSS_PARAMS, IMAGE_SIGNATURE_SIZE) are set by
 * options.mk from the .config file.
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
#ifndef _WOLFBOOT_USER_SETTINGS_SIGN_XMSS_H_
#define _WOLFBOOT_USER_SETTINGS_SIGN_XMSS_H_

#define WOLFSSL_HAVE_XMSS
#define WOLFSSL_WC_XMSS
#define WOLFSSL_WC_XMSS_SMALL
#define XMSS_IMAGE_SIGNATURE_SIZE IMAGE_SIGNATURE_SIZE
#define WOLFSSL_XMSS_VERIFY_ONLY
#define WOLFSSL_XMSS_MAX_HEIGHT 32

#endif /* _WOLFBOOT_USER_SETTINGS_SIGN_XMSS_H_ */
