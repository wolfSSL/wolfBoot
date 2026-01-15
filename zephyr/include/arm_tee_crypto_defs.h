/* arm_tee_crypto_defs.h
 *
 * ARM TEE crypto pack definitions for PSA IPC dispatch.
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

#ifndef WOLFBOOT_ARM_TEE_CRYPTO_DEFS_H_
#define WOLFBOOT_ARM_TEE_CRYPTO_DEFS_H_

#include <stdint.h>
#include "psa/crypto.h"
#include "psa_manifest/sid.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PLATFORM_DEFAULT_CRYPTO_KEYS
#include "crypto_keys/arm_tee_builtin_key_ids.h"
#else
#include "arm_tee_builtin_key_ids.h"
#endif

#define ARM_TEE_CRYPTO_MAX_NONCE_LENGTH (16u)

struct arm_tee_crypto_aead_pack_input {
    uint8_t nonce[ARM_TEE_CRYPTO_MAX_NONCE_LENGTH];
    uint32_t nonce_length;
};

struct arm_tee_crypto_pack_iovec {
    psa_key_id_t key_id;
    psa_algorithm_t alg;
    uint32_t op_handle;
    uint32_t ad_length;
    uint32_t plaintext_length;
    struct arm_tee_crypto_aead_pack_input aead_in;
    uint16_t function_id;
    uint16_t step;
    union {
        uint32_t capacity;
        uint64_t value;
    };
};

/* Function ID values aligned with ARM TEE service IDs. */
#define ARM_TEE_CRYPTO_GENERATE_RANDOM_SID          (0x0100U)
#define ARM_TEE_CRYPTO_GET_KEY_ATTRIBUTES_SID       (0x0200U)
#define ARM_TEE_CRYPTO_OPEN_KEY_SID                 (0x0201U)
#define ARM_TEE_CRYPTO_CLOSE_KEY_SID                (0x0202U)
#define ARM_TEE_CRYPTO_IMPORT_KEY_SID               (0x0203U)
#define ARM_TEE_CRYPTO_DESTROY_KEY_SID              (0x0204U)
#define ARM_TEE_CRYPTO_EXPORT_KEY_SID               (0x0205U)
#define ARM_TEE_CRYPTO_EXPORT_PUBLIC_KEY_SID        (0x0206U)
#define ARM_TEE_CRYPTO_GENERATE_KEY_SID             (0x0209U)
#define ARM_TEE_CRYPTO_HASH_COMPUTE_SID             (0x0300U)
#define ARM_TEE_CRYPTO_HASH_SETUP_SID               (0x0302U)
#define ARM_TEE_CRYPTO_HASH_UPDATE_SID              (0x0303U)
#define ARM_TEE_CRYPTO_HASH_CLONE_SID               (0x0304U)
#define ARM_TEE_CRYPTO_HASH_FINISH_SID              (0x0305U)
#define ARM_TEE_CRYPTO_HASH_ABORT_SID               (0x0307U)
#define ARM_TEE_CRYPTO_CIPHER_ENCRYPT_SETUP_SID     (0x0400U)
#define ARM_TEE_CRYPTO_CIPHER_DECRYPT_SETUP_SID     (0x0401U)
#define ARM_TEE_CRYPTO_CIPHER_SET_IV_SID            (0x0402U)
#define ARM_TEE_CRYPTO_CIPHER_UPDATE_SID            (0x0403U)
#define ARM_TEE_CRYPTO_CIPHER_FINISH_SID            (0x0404U)
#define ARM_TEE_CRYPTO_CIPHER_ABORT_SID             (0x0405U)
#define ARM_TEE_CRYPTO_ASYMMETRIC_SIGN_HASH_SID     (0x0702U)
#define ARM_TEE_CRYPTO_ASYMMETRIC_VERIFY_HASH_SID   (0x0703U)

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_CRYPTO_DEFS_H_ */
