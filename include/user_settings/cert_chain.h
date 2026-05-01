/* user_settings/cert_chain.h
 *
 * wolfCrypt configuration for WOLFBOOT_CERT_CHAIN_VERIFY. This is the
 * only build mode that links the wolfSSL TLS-layer cert manager (server
 * side). Client side just uses wolfHSM's cert manager and needs no
 * extra wolfCrypt config beyond what wolfhsm.h already supplies.
 *
 * The companion `WOLFCRYPT_ONLY` carve-out (when the server cert-chain
 * mode is active) lives in user_settings/base.h.
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
#ifndef _WOLFBOOT_USER_SETTINGS_CERT_CHAIN_H_
#define _WOLFBOOT_USER_SETTINGS_CERT_CHAIN_H_

#if defined(WOLFBOOT_ENABLE_WOLFHSM_SERVER) && \
    defined(WOLFBOOT_CERT_CHAIN_VERIFY)
#  define NO_TLS
#  define NO_OLD_TLS
#  define WOLFSSL_NO_TLS12
#  define WOLFSSL_USER_IO
#  define WOLFSSL_SP_MUL_D
#  define WOLFSSL_PEM_TO_DER
#  define WOLFSSL_ALLOW_NO_SUITES
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_CERT_CHAIN_H_ */
