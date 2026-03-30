/* hal_otp.h
 *
 * OTP helper definitions.
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

#ifndef WOLFBOOT_HAL_OTP_H
#define WOLFBOOT_HAL_OTP_H

#include <stdint.h>

static inline uint32_t hal_otp_blocks_for_length(uint32_t length,
    uint32_t block_size)
{
    return (length + block_size - 1U) / block_size;
}

#endif /* WOLFBOOT_HAL_OTP_H */
