/* tpm.h
 *
 * Copyright (C) 2023 wolfSSL Inc.
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

#ifndef _WOLFBOOT_TPM_H_
#define _WOLFBOOT_TPM_H_

#ifdef WOLFBOOT_TPM

#include <image.h>
#include "wolftpm/tpm2.h"
#include "wolftpm/tpm2_wrap.h"

extern WOLFTPM2_DEV     wolftpm_dev;
#if defined(WOLFBOOT_TPM_KEYSTORE) || defined(WOLFBOOT_TPM_SEAL)
extern WOLFTPM2_SESSION wolftpm_session;
extern WOLFTPM2_KEY     wolftpm_srk;
#endif

#ifndef WOLFBOOT_TPM_KEYSTORE_NV_BASE
    #define WOLFBOOT_TPM_KEYSTORE_NV_BASE 0x01400200
#endif
#ifndef WOLFBOOT_TPM_SEAL_NV_BASE
    #define WOLFBOOT_TPM_SEAL_NV_BASE     0x01400300
#endif
#ifndef WOLFBOOT_TPM_PCR_ALG
    /* Prefer SHA2-256 for PCR's, and all TPM 2.0 devices support it */
    #define WOLFBOOT_TPM_PCR_ALG          TPM_ALG_SHA256
    #define WOLFBOOT_TPM_PCR_DIG_SZ       32
#endif

#define WOLFBOOT_MAX_SEAL_SZ              MAX_SYM_DATA


int  wolfBoot_tpm2_init(void);
void wolfBoot_tpm2_deinit(void);

int wolfBoot_tpm2_clear(void);

#if defined(WOLFBOOT_TPM_VERIFY) || defined(WOLFBOOT_TPM_SEAL)
int wolfBoot_load_pubkey(const uint8_t* pubkey_hint, WOLFTPM2_KEY* pubKey,
    TPM_ALG_ID* pAlg);
#endif

#ifdef WOLFBOOT_TPM_KEYSTORE
int wolfBoot_check_rot(int key_slot, uint8_t* pubkey_hint);
#endif

#ifdef WOLFBOOT_TPM_SEAL
int wolfBoot_get_random(uint8_t* buf, int sz);
int wolfBoot_get_pcr_active(uint8_t pcrAlg, uint32_t* pcrMask, uint8_t pcrMax);
int wolfBoot_build_policy(uint8_t pcrAlg, uint32_t pcrMask,
    uint8_t* policy, uint32_t* policySz,
    uint8_t* policyRef, uint32_t policyRefSz);
int wolfBoot_get_policy(struct wolfBoot_image* img,
    uint8_t** policy, uint16_t* policySz);

int wolfBoot_seal(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, const uint8_t* secret, int secret_sz);
int wolfBoot_seal_auth(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, const uint8_t* secret, int secret_sz, const uint8_t* auth, int authSz);
int wolfBoot_seal_blob(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    WOLFTPM2_KEYBLOB* seal_blob, const uint8_t* secret, int secret_sz, const uint8_t* auth, int authSz);
int wolfBoot_unseal(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, uint8_t* secret, int* secret_sz);
int wolfBoot_unseal_auth(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    int index, uint8_t* secret, int* secret_sz, const uint8_t* auth, int authSz);
int wolfBoot_unseal_blob(const uint8_t* pubkey_hint, const uint8_t* policy, uint16_t policySz,
    WOLFTPM2_KEYBLOB* seal_blob, uint8_t* secret, int* secret_sz, const uint8_t* auth, int authSz);

int wolfBoot_delete_seal(int index);
int wolfBoot_read_blob(uint32_t nvIndex, WOLFTPM2_KEYBLOB* blob,
    const uint8_t* auth, uint32_t authSz);
int wolfBoot_store_blob(TPMI_RH_NV_AUTH authHandle, uint32_t nvIndex,
    word32 nvAttributes, WOLFTPM2_KEYBLOB* blob,
    const uint8_t* auth, uint32_t authSz);
int wolfBoot_delete_blob(TPMI_RH_NV_AUTH authHandle, uint32_t nvIndex,
    const uint8_t* auth, uint32_t authSz);

uint32_t wolfBoot_tpm_pcrmask_sel(uint32_t pcrMask, uint8_t* pcrArray,
    uint32_t pcrArraySz);
#endif

#ifdef WOLFBOOT_MEASURED_BOOT
int wolfBoot_tpm2_extend(uint8_t pcrIndex, uint8_t* hash, int line);

/* helper for measuring boot at line */
#define measure_boot(hash) \
    wolfBoot_tpm2_extend(WOLFBOOT_MEASURED_PCR_A, (hash), __LINE__)
#endif /* WOLFBOOT_MEASURED_BOOT */

/* debugging */
void wolfBoot_print_hexstr(const unsigned char* bin, unsigned long sz,
    unsigned long maxLine);
void wolfBoot_print_bin(const uint8_t* buffer, uint32_t length);


#else

/* stubs */
#define measure_boot(hash)

#endif /* WOLFBOOT_TPM */

#endif /* !_WOLFBOOT_TPM_H_ */
