/* arm_tee_ns_interface.c
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

#include <zephyr/kernel.h>
#include <zephyr/arch/arm/cortex_m/fpu.h>

#include "arm_tee_ns_interface.h"
#include "psa/client.h"

/* Global mutex used by the PSA dispatcher. */
K_MUTEX_DEFINE(arm_tee_mutex);

int32_t arm_tee_ns_interface_dispatch(arm_tee_veneer_fn fn,
				      uint32_t arg0, uint32_t arg1,
				      uint32_t arg2, uint32_t arg3)
{
	bool isr_mode = k_is_in_isr() || k_is_pre_kernel();
	int saved_prio = 0;
	int32_t result;

	if (!isr_mode) {
		if (k_mutex_lock(&arm_tee_mutex, K_FOREVER) != 0) {
			return (int32_t)PSA_ERROR_GENERIC_ERROR;
		}

#if !defined(CONFIG_ARM_NONSECURE_PREEMPTIBLE_SECURE_CALLS)
		saved_prio = k_thread_priority_get(k_current_get());
		k_thread_priority_set(k_current_get(), K_HIGHEST_THREAD_PRIO);
#endif
	}

#if defined(CONFIG_FPU_SHARING)
	struct fpu_ctx_full context_buffer;
	z_arm_save_fp_context(&context_buffer);
#endif

	result = fn(arg0, arg1, arg2, arg3);

#if defined(CONFIG_FPU_SHARING)
	z_arm_restore_fp_context(&context_buffer);
#endif

	if (!isr_mode) {
#if !defined(CONFIG_ARM_NONSECURE_PREEMPTIBLE_SECURE_CALLS)
		k_thread_priority_set(k_current_get(), saved_prio);
#endif
		k_mutex_unlock(&arm_tee_mutex);
	}

	return result;
}

uint32_t arm_tee_ns_interface_init(void)
{
	return PSA_SUCCESS;
}
