/* wolfboot_startup.c
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
 /*
 wolfBoot-specific early startup code for RA6M4 application.
 *
 * When this application is booted by wolfBoot, wolfBoot's own RAM functions
 * occupy 0x20000000-0x20001188 with a DIFFERENT layout than this application
 * expects.  The C runtime startup (SystemRuntimeInit) tries to call memcpy()
 * at the address this application was linked to, but that address contains
 * wolfBoot's code, not the application's memcpy, causing an immediate crash.
 *
 * wolfboot_pre_init() must be called from R_BSP_WarmStart(BSP_WARM_START_RESET)
 * BEFORE SystemRuntimeInit runs.  It copies the __ram_from_flash$$ section
 * using a plain word loop (no library memcpy) so that the application's RAM
 * functions are placed at their correct VMA addresses.  SystemRuntimeInit will
 * then redundantly copy the same data, but by that point memcpy is valid.
 */
#include <stdint.h>

/**
 * Copy __ram_from_flash$$ section to its VMA before SystemRuntimeInit runs.
 *
 * Call this from R_BSP_WarmStart(BSP_WARM_START_RESET), which fires inside
 * SystemInit() BEFORE SystemRuntimeInit() is called.
 */
void wolfboot_pre_init(void)
{
    extern uint32_t __ram_from_flash$$Base;
    extern uint32_t __ram_from_flash$$Limit;
    extern uint32_t __ram_from_flash$$Load;

    volatile uint32_t       *dst = &__ram_from_flash$$Base;
    volatile const uint32_t *src = (const uint32_t *)(&__ram_from_flash$$Load);
    volatile const uint32_t *end = &__ram_from_flash$$Limit;

    while (dst < end)
    {
        *dst++ = *src++;
    }
}
