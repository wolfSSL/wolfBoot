/* arm_tee_veneers.h
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

#ifndef WOLFBOOT_ARM_TEE_VENEERS_H_
#define WOLFBOOT_ARM_TEE_VENEERS_H_

#include <stdint.h>
#include "psa/client.h"
#include <wolfboot/arm_tee_api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* wolfBoot CMSE veneers (ARM TEE compatible names). */
uint32_t arm_tee_psa_framework_version_veneer(void);
uint32_t arm_tee_psa_version_veneer(uint32_t sid);
psa_handle_t arm_tee_psa_connect_veneer(uint32_t sid, uint32_t version);
psa_status_t arm_tee_psa_call_veneer(psa_handle_t handle,
				 uint32_t ctrl_param,
				 const psa_invec *in_vec,
				 psa_outvec *out_vec);
void arm_tee_psa_close_veneer(psa_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* WOLFBOOT_ARM_TEE_VENEERS_H_ */
