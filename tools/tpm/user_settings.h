/* user_settings.h
 *
 * Configuration for wolfBoot TPM tools.
 * Enabled via WOLFSSL_USER_SETTINGS.
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#ifndef _WOLFBOOT_TPM_SETTING_H
#define _WOLFBOOT_TPM_SETTING_H

#include <stdint.h>

/* TPM Specific Options */
#define WOLFSSL_PUBLIC_MP
#define WOLFSSL_AES_CFB
#define DEBUG_WOLFTPM /* for TPM2_PrintBin */

/* System */
#define SINGLE_THREADED
#define WOLFCRYPT_ONLY

/* Math */
#define WOLFSSL_SP_MATH
#define TFM_TIMING_RESISTANT

/* ECC */
#define HAVE_ECC
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_384
#define WOLFSSL_SP_521
#define ECC_TIMING_RESISTANT
#define ECC_USER_CURVES
#define HAVE_ECC256
#define HAVE_ECC384
#define HAVE_ECC521

/* RSA */
#define HAVE_RSA
#define WOLFSSL_HAVE_SP_RSA
#define WOLFSSL_SP_4096
#define WC_RSA_BLINDING
#define WOLFSSL_KEY_GEN

/* Hashing */
#define WOLFSSL_SHA384
#undef  NO_SHA256

/* ASN */
#define WOLFSSL_ASN_TEMPLATE
#define WOLFSSL_PEM_TO_DER
#define WOLFSSL_PUB_PEM_TO_DER

/* Disables */
#define NO_CMAC
#define NO_RC4
#define NO_SHA
#define NO_DH
#define NO_DSA
#define NO_MD4
#define NO_RABBIT
#define NO_MD5
#define NO_SIG_WRAPPER
#define NO_CERT
#define NO_SESSION_CACHE
#define NO_HC128
#define NO_DES3
#define NO_PWDBASED
#define NO_WRITEV
#define NO_MAIN_DRIVER
#define NO_OLD_RNGNAME
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_SOCK
#define WOLFSSL_IGNORE_FILE_WARN

#define BENCH_EMBEDDED
#define NO_CRYPT_TEST
#define NO_CRYPT_BENCHMARK

#endif /* !_WOLFBOOT_TPM_SETTING_H */
