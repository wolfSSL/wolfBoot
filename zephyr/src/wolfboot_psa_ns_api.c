/* wolfboot_psa_ns_api.c
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

#include "psa/client.h"
#include "arm_tee_ns_interface.h"
#include "arm_tee_psa_call_pack.h"
#include <wolfboot/arm_tee_api.h>

uint32_t psa_framework_version(void)
{
	return arm_tee_ns_interface_dispatch(
		(arm_tee_veneer_fn)arm_tee_psa_framework_version_veneer, 0, 0, 0, 0);
}

uint32_t psa_version(uint32_t sid)
{
	return arm_tee_ns_interface_dispatch(
		(arm_tee_veneer_fn)arm_tee_psa_version_veneer, sid, 0, 0, 0);
}

psa_status_t psa_call(psa_handle_t handle, int32_t type,
		      const psa_invec *in_vec, size_t in_len,
		      psa_outvec *out_vec, size_t out_len)
{
	if ((in_len > PSA_MAX_IOVEC) || (out_len > PSA_MAX_IOVEC)) {
		return PSA_ERROR_PROGRAMMER_ERROR;
	}

	uint32_t ctrl_param = PARAM_PACK(type, in_len, out_len);

	return arm_tee_ns_interface_dispatch(
		(arm_tee_veneer_fn)arm_tee_psa_call_veneer,
		(uint32_t)handle,
		ctrl_param,
		(uint32_t)(uintptr_t)in_vec,
		(uint32_t)(uintptr_t)out_vec);
}

psa_handle_t psa_connect(uint32_t sid, uint32_t version)
{
	return arm_tee_ns_interface_dispatch(
		(arm_tee_veneer_fn)arm_tee_psa_connect_veneer, sid, version, 0, 0);
}

void psa_close(psa_handle_t handle)
{
	(void)arm_tee_ns_interface_dispatch(
		(arm_tee_veneer_fn)arm_tee_psa_close_veneer, (uint32_t)handle, 0, 0, 0);
}
