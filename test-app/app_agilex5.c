/* app_agilex5.c
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1335, USA
 */

#include <stdint.h>

#include "hal.h"
#include "hal/agilex5.h"
#include "printf.h"

/* boot_aarch64.c normally supplies this to the HAL. */
__attribute__((weak)) unsigned int current_el(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el) : : "cc");
    return (unsigned int)((el >> 2) & 0x3U);
}

void main(void)
{
    uint64_t start;
    uint64_t now;

    hal_init();
    wolfBoot_printf("\nAgilex 5 BL33 smoke test\n");
    wolfBoot_printf("Current EL: %d (expected 2)\n", current_el());

    start = hal_get_timer_us();
    do {
        now = hal_get_timer_us();
    } while ((now - start) < 1000U);
    wolfBoot_printf("Generic timer advanced by %d us\n",
        (uint32_t)(now - start));
    wolfBoot_printf("AGILEX5_BL33_SMOKE_PASS\n");

    while (1)
        __asm__ volatile("wfi");
}
