/* user_settings/renesas.h
 *
 * wolfCrypt configuration for Renesas TSIP / RSIP / SCEPROTECT hardware
 * crypto offload. Active when any WOLFBOOT_RENESAS_* flag is set.
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
#ifndef _WOLFBOOT_USER_SETTINGS_RENESAS_H_
#define _WOLFBOOT_USER_SETTINGS_RENESAS_H_

#if defined(WOLFBOOT_RENESAS_TSIP) || \
    defined(WOLFBOOT_RENESAS_RSIP) || \
    defined(WOLFBOOT_RENESAS_SCEPROTECT)

#  define WOLFBOOT_SMALL_STACK
#  define WOLF_CRYPTO_CB
#  define WOLF_CRYPTO_CB_ONLY_ECC
#  define WOLF_CRYPTO_CB_ONLY_RSA
#  define WOLFSSL_NO_SW_MATH
#  define MAX_CRYPTO_DEVID_CALLBACKS 2
#  define WC_NO_DEFAULT_DEVID
#  define WOLFSSL_AES_SMALL_TABLES

#  ifdef WOLFBOOT_RENESAS_TSIP
#    define WOLFSSL_RENESAS_TSIP
#    define WOLFSSL_RENESAS_TSIP_VER  117
#    define WOLFSSL_RENESAS_TSIP_CRYPT
#    define WOLFSSL_RENESAS_TSIP_CRYPTONLY
#    define NO_WOLFSSL_RENESAS_TSIP_CRYPT_HASH
#    define RENESAS_TSIP_INSTALLEDKEY_ADDR 0xFFFF0000
#    ifndef RENESAS_TSIP_INSTALLEDENCKEY_ADDR
#      define RENESAS_TSIP_INSTALLEDENCKEY_ADDR \
              (RENESAS_TSIP_INSTALLEDKEY_ADDR + 0x100)
#    endif
#    define ENCRYPTED_KEY_BYTE_SIZE ENC_PUB_KEY_SIZE
#    define RENESAS_DEVID 7890
#  endif
#  ifdef WOLFBOOT_RENESAS_SCEPROTECT
#    define WOLFSSL_RENESAS_SCEPROTECT_CRYPTONLY
#    define RENESAS_SCE_INSTALLEDKEY_ADDR 0x08001000U
#    define SCE_ID 7890
#  endif
#  ifdef WOLFBOOT_RENESAS_RSIP
#    define WOLFSSL_RENESAS_FSPSM
#    define WOLFSSL_RENESAS_FSPSM_CRYPTONLY
#    define WOLFSSL_RENESAS_RSIP_CRYPTONLY
#    undef  WOLFSSL_RENESAS_FSPSM_TLS
#    define RENESAS_RSIP_INSTALLEDKEY_FLASH_ADDR  0x60200000
#    define RENESAS_RSIP_INSTALLEDKEY_RAM_ADDR    0x10000100
#    define RENESAS_DEVID 7890
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_RENESAS_H_ */
