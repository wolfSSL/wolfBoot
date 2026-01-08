/* arm_tee_builtin_key_ids.h
 *
 * ARM TEE builtin key identifiers.
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

#ifndef WOLFBOOT_ARM_TEE_BUILTIN_KEY_IDS_H_
#define WOLFBOOT_ARM_TEE_BUILTIN_KEY_IDS_H_

/**
 * \brief The persistent key identifiers for builtin keys.
 */
enum arm_tee_builtin_key_id_t {
    ARM_TEE_BUILTIN_KEY_ID_MIN = 0x7FFF815Bu,
    ARM_TEE_BUILTIN_KEY_ID_HUK,
    ARM_TEE_BUILTIN_KEY_ID_IAK,
#ifdef ARM_TEE_PARTITION_DELEGATED_ATTESTATION
    ARM_TEE_BUILTIN_KEY_ID_DAK_SEED,
#endif
    ARM_TEE_BUILTIN_KEY_ID_PLAT_SPECIFIC_MIN = 0x7FFF816Bu,
    ARM_TEE_BUILTIN_KEY_ID_MAX = 0x7FFF817Bu,
};

#endif /* WOLFBOOT_ARM_TEE_BUILTIN_KEY_IDS_H_ */
