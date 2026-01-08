/* arm_tee_psa_call_pack.h
 *
 * Packing helper for PSA call parameters.
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

#ifndef WOLFBOOT_ARM_TEE_PSA_CALL_PACK_H_
#define WOLFBOOT_ARM_TEE_PSA_CALL_PACK_H_

#include <wolfboot/arm_tee_api.h>

#define PARAM_PACK(type, in_len, out_len) \
    WOLFBOOT_ARM_TEE_PARAM_PACK(type, in_len, out_len)

#endif /* WOLFBOOT_ARM_TEE_PSA_CALL_PACK_H_ */
