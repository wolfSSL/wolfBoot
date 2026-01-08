/* arm_tee_ps_defs.h
 *
 * ARM TEE protected storage message IDs.
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

#ifndef WOLFBOOT_ARM_TEE_PS_DEFS_H_
#define WOLFBOOT_ARM_TEE_PS_DEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Protected Storage message types that distinguish PS services. */
#define ARM_TEE_PS_SET                1001
#define ARM_TEE_PS_GET                1002
#define ARM_TEE_PS_GET_INFO           1003
#define ARM_TEE_PS_REMOVE             1004
#define ARM_TEE_PS_GET_SUPPORT        1005

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_PS_DEFS_H_ */
