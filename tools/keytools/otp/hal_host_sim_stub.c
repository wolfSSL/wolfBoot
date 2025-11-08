/* hal_host_sim_stub.c
 *
 * Helper for storing/retrieving Trust Anchor to/from OTP flash
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

/* hal_host_stub.c - host-only placeholders for HAL used by otp-keystore-primer */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
//
//#ifndef HAL_H
//#define HAL_H
///* For host builds, redirect hal.h to our sim stub */
//#include "hal_host_sim_stub.h"
//#endif

/* Minimal mirror of what primer expects. If these normally come from hal.h/target.h,
   define the bare minimum here so the host build can link. */
#ifndef FLASH_OTP_BASE
#define FLASH_OTP_BASE 0u
#endif

void hal_init(void)
{
    /* No hardware on host. */
    fprintf(stderr, "[hal_host_stub] hal_init() called\n");
}

/* Return 0 on success like many wolfBoot HAL funcs. Adjust signature to match your hal.h. */
int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length)
{
    (void)flashAddress;
    (void)data;
    (void)length;
    fprintf(stderr, "[hal_host_stub] hal_flash_otp_write(addr=%lu, len=%lu)\n",
        (unsigned long)flashAddress, length);
    return 0;
}

int hal_flash_otp_set_readonly(uint32_t flashAddress, uint16_t length)
{
    (void)flashAddress;
    (void)length;
    fprintf(stderr, "[hal_host_stub] hal_flash_otp_set_readonly(addr=%lu, len=%lu)\n",
        (unsigned long)flashAddress, length);
    return 0;
}
