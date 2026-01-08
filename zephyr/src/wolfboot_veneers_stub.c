/* wolfboot_veneers_stub.c
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
#include <wolfboot/arm_tee_api.h>

#if defined(__GNUC__)
#define WOLFBOOT_WEAK __attribute__((weak))
#else
#define WOLFBOOT_WEAK
#endif

WOLFBOOT_WEAK uint32_t arm_tee_psa_framework_version_veneer(void)
{
	return 0;
}

WOLFBOOT_WEAK uint32_t arm_tee_psa_version_veneer(uint32_t sid)
{
	(void)sid;
	return 0;
}

WOLFBOOT_WEAK psa_handle_t arm_tee_psa_connect_veneer(uint32_t sid, uint32_t version)
{
	(void)sid;
	(void)version;
	return (psa_handle_t)PSA_ERROR_CONNECTION_REFUSED;
}

WOLFBOOT_WEAK psa_status_t arm_tee_psa_call_veneer(psa_handle_t handle,
					      uint32_t ctrl_param,
					      const psa_invec *in_vec,
					      psa_outvec *out_vec)
{
	(void)handle;
	(void)ctrl_param;
	(void)in_vec;
	(void)out_vec;
	return PSA_ERROR_NOT_SUPPORTED;
}

WOLFBOOT_WEAK void arm_tee_psa_close_veneer(psa_handle_t handle)
{
	(void)handle;
}
