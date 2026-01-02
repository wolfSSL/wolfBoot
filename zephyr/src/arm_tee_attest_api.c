/* arm_tee_attest_api.c
 *
 * ARM TEE attestation NS API wrappers.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "psa/initial_attestation.h"
#include "psa/client.h"
#include "psa_manifest/sid.h"
#include "arm_tee_attest_defs.h"

psa_status_t
psa_initial_attest_get_token(const uint8_t *auth_challenge,
                             size_t challenge_size,
                             uint8_t *token_buf,
                             size_t token_buf_size,
                             size_t *token_size)
{
    psa_status_t status;

    psa_invec in_vec[] = {
        { auth_challenge, challenge_size }
    };
    psa_outvec out_vec[] = {
        { token_buf, token_buf_size }
    };

    status = psa_call(ARM_TEE_ATTESTATION_SERVICE_HANDLE,
                      ARM_TEE_ATTEST_GET_TOKEN,
                      in_vec, IOVEC_LEN(in_vec),
                      out_vec, IOVEC_LEN(out_vec));

    if (status == PSA_SUCCESS && token_size != NULL) {
        *token_size = out_vec[0].len;
    }

    return status;
}

psa_status_t
psa_initial_attest_get_token_size(size_t challenge_size,
                                  size_t *token_size)
{
    psa_status_t status;
    rot_size_t challenge_size_param;
    rot_size_t token_size_param = 0;

    psa_invec in_vec[] = {
        { &challenge_size_param, sizeof(challenge_size_param) }
    };
    psa_outvec out_vec[] = {
        { &token_size_param, sizeof(token_size_param) }
    };

    if (challenge_size > ROT_SIZE_MAX) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    challenge_size_param = (rot_size_t)challenge_size;

    if (token_size == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = psa_call(ARM_TEE_ATTESTATION_SERVICE_HANDLE,
                      ARM_TEE_ATTEST_GET_TOKEN_SIZE,
                      in_vec, IOVEC_LEN(in_vec),
                      out_vec, IOVEC_LEN(out_vec));

    *token_size = token_size_param;

    return status;
}
