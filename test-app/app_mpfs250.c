/* app_mpfs250.c
 *
 * Test bare-metal application for PolarFire SoC MPFS250.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"
#include "printf.h"

#include "../hal/mpfs250.h"


void main(void)
{
    /* wolfBoot fully configured UART0 before jumping here.
     * Calling uart_init() again clears the TX FIFO (FCR write) while wolfBoot's
     * last output may still be draining, which can leave THRE stuck at 0.
     * Calling hal_init() writes to _main_hart_hls=0 (NULL ptr crash).
     * So use wolfBoot_printf directly — UART0 is already ready. */

    wolfBoot_printf("========================\r\n");
    wolfBoot_printf("PolarFire SoC MPFS250 wolfBoot demo Application\r\n");
    wolfBoot_printf("Copyright 2025 wolfSSL Inc\r\n");
    wolfBoot_printf("GPL v3\r\n");
    wolfBoot_printf("========================\r\n");

    /* TODO: Add application-specific code here */

    while(1) {
        /* Main application loop */
        /* TODO: Add watchdog feed if needed */
    }
}

