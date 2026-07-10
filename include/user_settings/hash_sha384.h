/* user_settings/hash_sha384.h
 *
 * wolfCrypt configuration when WOLFBOOT_HASH_SHA384 selects SHA-384 as
 * the image hash (which truncates SHA-512). Pulled in by hash_dispatch.h.
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
#ifndef _WOLFBOOT_USER_SETTINGS_HASH_SHA384_H_
#define _WOLFBOOT_USER_SETTINGS_HASH_SHA384_H_

#define WOLFSSL_SHA384
/* Drop SHA-256 if no other consumer remains. */
#if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
    !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define NO_SHA256
#endif
#ifndef WOLFSSL_SHA512
#  define WOLFSSL_SHA512
#  define WOLFSSL_NOSHA512_224
#  define WOLFSSL_NOSHA512_256
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_HASH_SHA384_H_ */
