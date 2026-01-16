/* app_versal.c
 *
 * Test application for AMD Versal VMK180
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

#include "hal.h"
#include "hal/versal.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

void main(void)
{
    uint32_t boot_version, update_version;

    /* Initialize HAL (UART, etc.) */
    hal_init();

    /* Get versions from both partitions */
    boot_version = wolfBoot_get_image_version(PART_BOOT);
    update_version = wolfBoot_get_image_version(PART_UPDATE);

    wolfBoot_printf("\n\n");
    wolfBoot_printf("===========================================\n");
    wolfBoot_printf(" wolfBoot Test Application - AMD Versal\n");
    wolfBoot_printf("===========================================\n\n");

    /* Print firmware versions */
    wolfBoot_printf("Boot Partition Version: %d (0x%08x)\n", boot_version, boot_version);
    wolfBoot_printf("Update Partition Version: %d (0x%08x)\n", update_version, update_version);

    wolfBoot_printf("Application running successfully!\n");
    wolfBoot_printf("\nEntering idle loop...\n");

    /* Idle loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}
