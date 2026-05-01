/* user_settings/tpm.h
 *
 * wolfCrypt + wolfTPM configuration when WOLFBOOT_TPM is active.
 * WOLFBOOT_TPM is set explicitly or implied via cascade.h from
 * WOLFBOOT_TPM_VERIFY / MEASURED_BOOT / WOLFBOOT_TPM_KEYSTORE / SEAL.
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
#ifndef _WOLFBOOT_USER_SETTINGS_TPM_H_
#define _WOLFBOOT_USER_SETTINGS_TPM_H_

/* TPM Parameter Encryption: implied by KEYSTORE / SEAL. */
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
#  define WOLFBOOT_TPM_PARMENC /* used in this file to gate features */
#endif

#if defined(WOLFBOOT_TPM) && !defined(WOLFBOOT_TZ_FWTPM)
   /* Do not use heap */
#  define WOLFTPM2_NO_HEAP
   /* small stack options */
#  ifdef WOLFTPM_SMALL_STACK
#    define MAX_COMMAND_SIZE 1024
#    define MAX_RESPONSE_SIZE 1350
#    define WOLFTPM2_MAX_BUFFER 1500
#    define MAX_SESSION_NUM 2
#    define MAX_DIGEST_BUFFER 973
#  endif

#  ifdef WOLFBOOT_TPM_PARMENC
     /* Enable AES CFB (parameter encryption) and HMAC (for KDF) */
#    define WOLFSSL_AES_CFB
     /* Get access to mp_* math API's for ECC encrypt */
#    define WOLFSSL_PUBLIC_MP
     /* Configure RNG seed */
#    include "../loader.h"
#    define CUSTOM_RAND_GENERATE_SEED(buf, sz) \
         ({(void)buf; (void)sz; wolfBoot_panic(); 0;}) /* stub, not used */
#    define WC_RNG_SEED_CB
#  endif

#  ifdef WOLFTPM_MMIO
     /* IO callback above TIS that includes Address and read/write flag */
#    define WOLFTPM_ADV_IO
#  endif

   /* add delay */
#  if !defined(XTPM_WAIT) && defined(WOLFTPM_MMIO)
     void delay(int msec);
#    define XTPM_WAIT() delay(1);
#  endif
#  ifndef XTPM_WAIT
#    define XTPM_WAIT() /* no delay */
#  endif
#  define HASH_COUNT 3 /* enable more PCR hash types */

   /* TPM remap printf */
#  if defined(DEBUG_WOLFTPM) && !defined(ARCH_SIM)
#    include "../printf.h"
#    define printf wolfBoot_printf
#  endif
#endif

#endif /* _WOLFBOOT_USER_SETTINGS_TPM_H_ */
