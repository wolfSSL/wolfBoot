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

#ifndef HAL_HOST_SIM_STUB_H
#define HAL_HOST_SIM_STUB_H

#include <stdint.h>

#ifndef FLASH_OTP_BASE
    #define FLASH_OTP_BASE 0u
#endif

#ifndef OTP_SIZE
    /* Define a generic max OTP size to appease otp_keystore.h */
    #define OTP_SIZE 4096
#endif

/* See actual implementation in [WOLFBOOT_ROOT]/hal; Optionally define your own sim stubs: */

void hal_init(void);
int hal_flash_otp_write(uint32_t flashAddress, const void* data, uint16_t length);
int hal_flash_otp_set_readonly(uint32_t flashAddress, uint16_t length);

#endif /* HAL_HOST_SIM_STUB_H */
