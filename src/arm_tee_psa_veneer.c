/* arm_tee_psa_veneer.c
 *
 * ARM TEE style PSA IPC CMSE veneers for Zephyr integration.
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <wolfboot/arm_tee_api.h>

uint32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_framework_version_veneer(void)
{
    return arm_tee_psa_framework_version();
}

uint32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_version_veneer(uint32_t sid)
{
    return arm_tee_psa_version(sid);
}

psa_handle_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_connect_veneer(uint32_t sid,
                                                          uint32_t version)
{
    return arm_tee_psa_connect(sid, version);
}

int32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_call_veneer(psa_handle_t handle,
    uint32_t ctrl_param,
    const psa_invec *in_vec, psa_outvec *out_vec)
{
    int32_t type = WOLFBOOT_ARM_TEE_PARAM_UNPACK_TYPE(ctrl_param);
    size_t in_len = WOLFBOOT_ARM_TEE_PARAM_UNPACK_IN_LEN(ctrl_param);
    size_t out_len = WOLFBOOT_ARM_TEE_PARAM_UNPACK_OUT_LEN(ctrl_param);

    return arm_tee_psa_call(handle, type, in_vec, in_len, out_vec, out_len);
}

void WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_close_veneer(psa_handle_t handle)
{
    arm_tee_psa_close(handle);
}
