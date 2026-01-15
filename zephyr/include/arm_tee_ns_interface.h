/* arm_tee_ns_interface.h
 *
 * ARM TEE NS interface helpers for PSA client dispatch.
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

#ifndef WOLFBOOT_ARM_TEE_NS_INTERFACE_H_
#define WOLFBOOT_ARM_TEE_NS_INTERFACE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*arm_tee_veneer_fn)(uint32_t arg0, uint32_t arg1,
    uint32_t arg2, uint32_t arg3);

int32_t arm_tee_ns_interface_dispatch(arm_tee_veneer_fn fn,
    uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3);

uint32_t arm_tee_ns_interface_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_NS_INTERFACE_H_ */
