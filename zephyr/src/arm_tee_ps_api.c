/* arm_tee_ps_api.c
 *
 * ARM TEE Protected Storage NS API wrappers.
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

#include "psa/client.h"
#include "psa/protected_storage.h"
#include "psa_manifest/sid.h"
#include "arm_tee_ps_defs.h"

struct rot_psa_ps_storage_info_t {
    rot_size_t capacity;
    rot_size_t size;
    psa_storage_create_flags_t flags;
};

psa_status_t psa_ps_set(psa_storage_uid_t uid,
                        size_t data_length,
                        const void *p_data,
                        psa_storage_create_flags_t create_flags)
{
    psa_invec in_vec[] = {
        { .base = &uid,   .len = sizeof(uid) },
        { .base = p_data, .len = data_length },
        { .base = &create_flags, .len = sizeof(create_flags) }
    };

    return psa_call(ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE,
                    ARM_TEE_PS_SET, in_vec, IOVEC_LEN(in_vec), NULL, 0);
}

psa_status_t psa_ps_get(psa_storage_uid_t uid,
                        size_t data_offset,
                        size_t data_size,
                        void *p_data,
                        size_t *p_data_length)
{
    psa_status_t status;
    rot_size_t data_offset_param;

    psa_invec in_vec[] = {
        { .base = &uid, .len = sizeof(uid) },
        { .base = &data_offset_param, .len = sizeof(data_offset_param) }
    };

    psa_outvec out_vec[] = {
        { .base = p_data, .len = data_size }
    };

    if (data_offset > ROT_SIZE_MAX) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    data_offset_param = (rot_size_t)data_offset;

    if (p_data_length == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = psa_call(ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE,
                      ARM_TEE_PS_GET, in_vec, IOVEC_LEN(in_vec),
                      out_vec, IOVEC_LEN(out_vec));

    *p_data_length = out_vec[0].len;

    return status;
}

psa_status_t psa_ps_get_info(psa_storage_uid_t uid,
                             struct psa_storage_info_t *p_info)
{
    psa_status_t status;
    struct rot_psa_ps_storage_info_t info_param = {0};

    psa_invec in_vec[] = {
        { .base = &uid, .len = sizeof(uid) }
    };

    psa_outvec out_vec[] = {
        { .base = &info_param, .len = sizeof(info_param) }
    };

    if (p_info == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = psa_call(ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE,
                      ARM_TEE_PS_GET_INFO, in_vec, IOVEC_LEN(in_vec),
                      out_vec, IOVEC_LEN(out_vec));

    p_info->capacity = info_param.capacity;
    p_info->size = info_param.size;
    p_info->flags = info_param.flags;

    return status;
}

psa_status_t psa_ps_remove(psa_storage_uid_t uid)
{
    psa_invec in_vec[] = {
        { .base = &uid, .len = sizeof(uid) }
    };

    return psa_call(ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE,
                    ARM_TEE_PS_REMOVE, in_vec, IOVEC_LEN(in_vec), NULL, 0);
}

uint32_t psa_ps_get_support(void)
{
    uint32_t support_flags = 0;
    psa_outvec out_vec[] = {
        { .base = &support_flags, .len = sizeof(support_flags) }
    };

    (void)psa_call(ARM_TEE_PROTECTED_STORAGE_SERVICE_HANDLE,
                   ARM_TEE_PS_GET_SUPPORT, NULL, 0,
                   out_vec, IOVEC_LEN(out_vec));

    return support_flags;
}
