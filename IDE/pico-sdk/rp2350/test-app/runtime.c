/* runtime.c
 *
 * Custom pre-init for non-secure application, staged by wolfBoot.
 * Wolfboot test application for raspberry-pi pico2 (rp2350)
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
#include <stdint.h>
typedef void (*preinit_fn_t)(void);

void runtime_init_cpasr(void)
{
    volatile uint32_t *cpasr_ns = (volatile uint32_t*) 0xE000ED88;
    *cpasr_ns |= 0xFF;
}

preinit_fn_t __attribute__((section(".nonsecure_preinit_array"))) nonsecure_preinit[] = 
             { &runtime_init_cpasr };
