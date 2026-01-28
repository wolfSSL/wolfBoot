/* arm_tee_ns_interface_stub.c
 *
 * Minimal non-Zephyr dispatcher for bare-metal test-app.
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

#include "arm_tee_ns_interface.h"
#include "psa/error.h"
#include <stddef.h>

int32_t arm_tee_ns_interface_dispatch(arm_tee_veneer_fn fn,
                                      uint32_t arg0, uint32_t arg1,
                                      uint32_t arg2, uint32_t arg3)
{
    if (fn == NULL) {
        return (int32_t)PSA_ERROR_INVALID_ARGUMENT;
    }

    return fn(arg0, arg1, arg2, arg3);
}

uint32_t arm_tee_ns_interface_init(void)
{
    return PSA_SUCCESS;
}
