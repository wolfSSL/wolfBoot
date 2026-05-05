/* user_settings/hash_sha3.h
 *
 * wolfCrypt configuration when WOLFBOOT_HASH_SHA3_384 selects SHA3 as the
 * image hash. Pulled in by user_settings/hash_dispatch.h.
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
#ifndef _WOLFBOOT_USER_SETTINGS_HASH_SHA3_H_
#define _WOLFBOOT_USER_SETTINGS_HASH_SHA3_H_

#define WOLFSSL_SHA3
/* Drop SHA-256 if no other consumer remains. In Phase 4 the negated
 * chain becomes a NEEDS_SHA256 marker test. */
#if defined(NO_RSA) && !defined(WOLFBOOT_TPM) && \
    !defined(WOLFCRYPT_SECURE_MODE) && \
    !defined(WOLFCRYPT_TEST) && !defined(WOLFCRYPT_BENCHMARK)
#  define NO_SHA256
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_HASH_SHA3_H_ */
