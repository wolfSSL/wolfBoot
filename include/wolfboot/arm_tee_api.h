/* arm_tee_api.h
 *
 * ARM TEE style PSA client veneers for Zephyr integration.
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

#ifndef WOLFBOOT_ARM_TEE_API_H
#define WOLFBOOT_ARM_TEE_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Provide minimal PSA client types if PSA client headers are unavailable. */
#ifndef __PSA_CLIENT_H__
typedef int32_t psa_handle_t;
typedef struct psa_invec {
    const void *base;
    size_t len;
} psa_invec;
typedef struct psa_outvec {
    void *base;
    size_t len;
} psa_outvec;
#endif

#ifndef PSA_ERROR_INVALID_ARGUMENT
#define PSA_ERROR_INVALID_ARGUMENT ((int32_t)-132)
#endif
#ifndef PSA_ERROR_NOT_SUPPORTED
#define PSA_ERROR_NOT_SUPPORTED ((int32_t)-138)
#endif

/* Pack extra args to keep veneers <= 4 args (ARM TEE style). */
#define WOLFBOOT_ARM_TEE_TYPE_MASK      0xFFFFUL
#define WOLFBOOT_ARM_TEE_IN_LEN_OFFSET  24
#define WOLFBOOT_ARM_TEE_IN_LEN_MASK    (0x7UL << WOLFBOOT_ARM_TEE_IN_LEN_OFFSET)
#define WOLFBOOT_ARM_TEE_OUT_LEN_OFFSET 16
#define WOLFBOOT_ARM_TEE_OUT_LEN_MASK   (0x7UL << WOLFBOOT_ARM_TEE_OUT_LEN_OFFSET)

#define WOLFBOOT_ARM_TEE_PARAM_PACK(type, in_len, out_len)                \
    ((((uint32_t)(type)) & WOLFBOOT_ARM_TEE_TYPE_MASK)                  | \
     ((((uint32_t)(in_len)) << WOLFBOOT_ARM_TEE_IN_LEN_OFFSET) &         \
      WOLFBOOT_ARM_TEE_IN_LEN_MASK)                                    | \
     ((((uint32_t)(out_len)) << WOLFBOOT_ARM_TEE_OUT_LEN_OFFSET) &       \
      WOLFBOOT_ARM_TEE_OUT_LEN_MASK))

#define WOLFBOOT_ARM_TEE_PARAM_UNPACK_TYPE(ctrl_param)                   \
    ((int32_t)(int16_t)((ctrl_param) & WOLFBOOT_ARM_TEE_TYPE_MASK))
#define WOLFBOOT_ARM_TEE_PARAM_UNPACK_IN_LEN(ctrl_param)                 \
    ((size_t)(((ctrl_param) & WOLFBOOT_ARM_TEE_IN_LEN_MASK) >>           \
              WOLFBOOT_ARM_TEE_IN_LEN_OFFSET))
#define WOLFBOOT_ARM_TEE_PARAM_UNPACK_OUT_LEN(ctrl_param)                \
    ((size_t)(((ctrl_param) & WOLFBOOT_ARM_TEE_OUT_LEN_MASK) >>          \
              WOLFBOOT_ARM_TEE_OUT_LEN_OFFSET))

#if defined(__ARM_FEATURE_CMSE) && defined(__GNUC__)
#define WOLFBOOT_CMSE_NS_ENTRY __attribute__((cmse_nonsecure_entry))
#else
#define WOLFBOOT_CMSE_NS_ENTRY
#endif

/* Secure-side NSC veneers expected by Zephyr ARM TEE client. */
uint32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_framework_version_veneer(void);
uint32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_version_veneer(uint32_t sid);
psa_handle_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_connect_veneer(uint32_t sid,
                                                          uint32_t version);
int32_t WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_call_veneer(psa_handle_t handle,
    uint32_t ctrl_param,
    const psa_invec *in_vec, psa_outvec *out_vec);
void WOLFBOOT_CMSE_NS_ENTRY arm_tee_psa_close_veneer(psa_handle_t handle);

/* Backing PSA IPC hooks (override in secure code). */
uint32_t arm_tee_psa_framework_version(void);
uint32_t arm_tee_psa_version(uint32_t sid);
psa_handle_t arm_tee_psa_connect(uint32_t sid, uint32_t version);
int32_t arm_tee_psa_call(psa_handle_t handle, int32_t type,
    const psa_invec *in_vec, size_t in_len,
    psa_outvec *out_vec, size_t out_len);
void arm_tee_psa_close(psa_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_API_H */
