/* arm_tee_crypto_api.c
 *
 * ARM TEE PSA Crypto NS API wrappers.
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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "psa/client.h"
#include "arm_tee_crypto_defs.h"

#define API_DISPATCH(in_vec, out_vec)          \
    psa_call(ARM_TEE_CRYPTO_HANDLE, PSA_IPC_CALL,  \
             in_vec, IOVEC_LEN(in_vec),        \
             out_vec, IOVEC_LEN(out_vec))

#define API_DISPATCH_NO_OUTVEC(in_vec)         \
    psa_call(ARM_TEE_CRYPTO_HANDLE, PSA_IPC_CALL,  \
             in_vec, IOVEC_LEN(in_vec),        \
             (psa_outvec *)NULL, 0)

psa_status_t psa_crypto_init(void)
{
    return PSA_SUCCESS;
}

psa_status_t psa_generate_random(uint8_t *output, size_t output_size)
{
    const struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_GENERATE_RANDOM_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = output, .len = output_size},
    };

    return API_DISPATCH(in_vec, out_vec);
}

psa_status_t psa_open_key(psa_key_id_t id, psa_key_id_t *key)
{
    const struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_OPEN_KEY_SID,
        .key_id = id,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = key, .len = sizeof(psa_key_id_t)},
    };

    return API_DISPATCH(in_vec, out_vec);
}

psa_status_t psa_close_key(psa_key_id_t key)
{
    const struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CLOSE_KEY_SID,
        .key_id = key,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };

    return API_DISPATCH_NO_OUTVEC(in_vec);
}

psa_status_t psa_import_key(const psa_key_attributes_t *attributes,
                            const uint8_t *data,
                            size_t data_length,
                            psa_key_id_t *key)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_IMPORT_KEY_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = attributes, .len = sizeof(psa_key_attributes_t)},
        {.base = data, .len = data_length},
    };
    psa_outvec out_vec[] = {
        {.base = key, .len = sizeof(psa_key_id_t)},
    };

    return API_DISPATCH(in_vec, out_vec);
}

psa_status_t psa_destroy_key(psa_key_id_t key)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_DESTROY_KEY_SID,
        .key_id = key,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };

    return API_DISPATCH_NO_OUTVEC(in_vec);
}

psa_status_t psa_get_key_attributes(psa_key_id_t key,
                                    psa_key_attributes_t *attributes)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_GET_KEY_ATTRIBUTES_SID,
        .key_id = key,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = attributes, .len = sizeof(psa_key_attributes_t)},
    };

    return API_DISPATCH(in_vec, out_vec);
}

void psa_reset_key_attributes(psa_key_attributes_t *attributes)
{
    if (attributes != NULL) {
        memset(attributes, 0, sizeof(*attributes));
    }
}

psa_status_t psa_export_key(psa_key_id_t key,
                            uint8_t *data,
                            size_t data_size,
                            size_t *data_length)
{
    psa_status_t status;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_EXPORT_KEY_SID,
        .key_id = key,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = data, .len = data_size},
    };

    status = API_DISPATCH(in_vec, out_vec);
    *data_length = out_vec[0].len;

    return status;
}

psa_status_t psa_export_public_key(psa_key_id_t key,
                                   uint8_t *data,
                                   size_t data_size,
                                   size_t *data_length)
{
    psa_status_t status;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_EXPORT_PUBLIC_KEY_SID,
        .key_id = key,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = data, .len = data_size},
    };

    status = API_DISPATCH(in_vec, out_vec);
    *data_length = out_vec[0].len;

    return status;
}

psa_status_t psa_generate_key(const psa_key_attributes_t *attributes,
                              psa_key_id_t *key)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_GENERATE_KEY_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = attributes, .len = sizeof(psa_key_attributes_t)},
    };
    psa_outvec out_vec[] = {
        {.base = key, .len = sizeof(psa_key_id_t)},
    };

    return API_DISPATCH(in_vec, out_vec);
}

psa_status_t psa_hash_compute(psa_algorithm_t alg,
                              const uint8_t *input,
                              size_t input_length,
                              uint8_t *hash,
                              size_t hash_size,
                              size_t *hash_length)
{
    psa_status_t status;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_COMPUTE_SID,
        .alg = alg,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = input, .len = input_length},
    };
    psa_outvec out_vec[] = {
        {.base = hash, .len = hash_size},
    };

    status = API_DISPATCH(in_vec, out_vec);
    *hash_length = out_vec[0].len;

    return status;
}

psa_status_t psa_hash_setup(psa_hash_operation_t *operation, psa_algorithm_t alg)
{
    uint32_t op_handle = (uint32_t)operation->opaque;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_SETUP_SID,
        .alg = alg,
        .op_handle = op_handle,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &op_handle, .len = sizeof(op_handle)},
    };

    psa_status_t status = API_DISPATCH(in_vec, out_vec);
    operation->opaque = op_handle;
    return status;
}

psa_status_t psa_hash_update(psa_hash_operation_t *operation,
                             const uint8_t *input,
                             size_t input_length)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_UPDATE_SID,
        .op_handle = (uint32_t)operation->opaque,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = input, .len = input_length},
    };

    return API_DISPATCH_NO_OUTVEC(in_vec);
}

psa_status_t psa_hash_finish(psa_hash_operation_t *operation,
                             uint8_t *hash,
                             size_t hash_size,
                             size_t *hash_length)
{
    psa_status_t status;
    uint32_t op_handle = (uint32_t)operation->opaque;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_FINISH_SID,
        .op_handle = op_handle,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &op_handle, .len = sizeof(op_handle)},
        {.base = hash, .len = hash_size},
    };

    status = API_DISPATCH(in_vec, out_vec);
    *hash_length = out_vec[1].len;
    operation->opaque = op_handle;

    return status;
}

psa_status_t psa_hash_abort(psa_hash_operation_t *operation)
{
    uint32_t op_handle = (uint32_t)operation->opaque;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_ABORT_SID,
        .op_handle = op_handle,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &op_handle, .len = sizeof(op_handle)},
    };

    psa_status_t status = API_DISPATCH(in_vec, out_vec);
    operation->opaque = op_handle;
    return status;
}

psa_status_t psa_hash_clone(const psa_hash_operation_t *source_operation,
                            psa_hash_operation_t *target_operation)
{
    uint32_t src_handle;
    uint32_t dst_handle = 0;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_HASH_CLONE_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &dst_handle, .len = sizeof(dst_handle)},
    };
    psa_status_t status;

    if (source_operation == NULL || target_operation == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    src_handle = (uint32_t)source_operation->opaque;
    iov.op_handle = src_handle;

    status = API_DISPATCH(in_vec, out_vec);
    if (status == PSA_SUCCESS) {
        target_operation->opaque = dst_handle;
    }
    return status;
}

psa_status_t psa_cipher_encrypt_setup(psa_cipher_operation_t *operation,
                                      psa_key_id_t key,
                                      psa_algorithm_t alg)
{
    uint32_t op_handle = 0;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_ENCRYPT_SETUP_SID,
        .key_id = key,
        .alg = alg,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &op_handle, .len = sizeof(op_handle)},
    };
    psa_status_t status;

    if (operation == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = API_DISPATCH(in_vec, out_vec);
    if (status == PSA_SUCCESS) {
        operation->opaque = op_handle;
    }
    return status;
}

psa_status_t psa_cipher_decrypt_setup(psa_cipher_operation_t *operation,
                                      psa_key_id_t key,
                                      psa_algorithm_t alg)
{
    uint32_t op_handle = 0;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_DECRYPT_SETUP_SID,
        .key_id = key,
        .alg = alg,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = &op_handle, .len = sizeof(op_handle)},
    };
    psa_status_t status;

    if (operation == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = API_DISPATCH(in_vec, out_vec);
    if (status == PSA_SUCCESS) {
        operation->opaque = op_handle;
    }
    return status;
}

psa_status_t psa_cipher_set_iv(psa_cipher_operation_t *operation,
                               const uint8_t *iv,
                               size_t iv_length)
{
    if (operation == NULL || (iv == NULL && iv_length > 0)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_SET_IV_SID,
        .op_handle = (uint32_t)operation->opaque,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = iv, .len = iv_length},
    };

    return API_DISPATCH_NO_OUTVEC(in_vec);
}

psa_status_t psa_cipher_update(psa_cipher_operation_t *operation,
                               const uint8_t *input,
                               size_t input_length,
                               uint8_t *output,
                               size_t output_size,
                               size_t *output_length)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_UPDATE_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = input, .len = input_length},
    };
    psa_outvec out_vec[] = {
        {.base = output, .len = output_size},
    };
    psa_status_t status;

    if (operation == NULL || output_length == NULL ||
        (input == NULL && input_length > 0) ||
        (output == NULL && output_size > 0)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    iov.op_handle = (uint32_t)operation->opaque;
    status = API_DISPATCH(in_vec, out_vec);
    if (status == PSA_SUCCESS) {
        *output_length = out_vec[0].len;
    }
    return status;
}

psa_status_t psa_cipher_finish(psa_cipher_operation_t *operation,
                               uint8_t *output,
                               size_t output_size,
                               size_t *output_length)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_FINISH_SID,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };
    psa_outvec out_vec[] = {
        {.base = output, .len = output_size},
    };
    psa_status_t status;

    if (operation == NULL || output_length == NULL ||
        (output == NULL && output_size > 0)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    iov.op_handle = (uint32_t)operation->opaque;
    status = API_DISPATCH(in_vec, out_vec);
    if (status == PSA_SUCCESS) {
        *output_length = out_vec[0].len;
        operation->opaque = 0;
    }
    return status;
}

psa_status_t psa_cipher_abort(psa_cipher_operation_t *operation)
{
    psa_status_t status;

    if (operation == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_CIPHER_ABORT_SID,
        .op_handle = (uint32_t)operation->opaque,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
    };

    status = API_DISPATCH_NO_OUTVEC(in_vec);
    if (status == PSA_SUCCESS) {
        operation->opaque = 0;
    }
    return status;
}

psa_status_t psa_sign_hash(psa_key_id_t key,
                           psa_algorithm_t alg,
                           const uint8_t *hash,
                           size_t hash_length,
                           uint8_t *signature,
                           size_t signature_size,
                           size_t *signature_length)
{
    psa_status_t status;
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_ASYMMETRIC_SIGN_HASH_SID,
        .key_id = key,
        .alg = alg,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = hash, .len = hash_length},
    };
    psa_outvec out_vec[] = {
        {.base = signature, .len = signature_size},
    };

    status = API_DISPATCH(in_vec, out_vec);
    *signature_length = out_vec[0].len;

    return status;
}

psa_status_t psa_verify_hash(psa_key_id_t key,
                             psa_algorithm_t alg,
                             const uint8_t *hash,
                             size_t hash_length,
                             const uint8_t *signature,
                             size_t signature_length)
{
    struct arm_tee_crypto_pack_iovec iov = {
        .function_id = ARM_TEE_CRYPTO_ASYMMETRIC_VERIFY_HASH_SID,
        .key_id = key,
        .alg = alg,
    };
    psa_invec in_vec[] = {
        {.base = &iov, .len = sizeof(struct arm_tee_crypto_pack_iovec)},
        {.base = hash, .len = hash_length},
        {.base = signature, .len = signature_length},
    };

    return API_DISPATCH_NO_OUTVEC(in_vec);
}
