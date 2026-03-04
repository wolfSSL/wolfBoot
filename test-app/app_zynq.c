/* app_zynq.c
 *
 * Test bare-metal boot application for AMD ZynqMP ZCU102
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

#include "wolfboot/wolfboot.h"
#include "printf.h"

#ifdef TARGET_zynq

/* Provide current_el() for hal/zynq.o (normally in boot_aarch64.c) */
__attribute__((weak)) unsigned int current_el(void)
{
    unsigned long el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (el) : : "cc");
    return (unsigned int)((el >> 2) & 0x3U);
}

/* Stub for QSPI DMA code in hal/zynq.o (not used with SD card boot) */
void flush_dcache_range(unsigned long start, unsigned long end)
{
    (void)start;
    (void)end;
}

void main(void)
{
#ifdef WOLFBOOT_FIXED_PARTITIONS
    uint32_t boot_version, update_version;
#endif

    wolfBoot_printf("\n\n");
    wolfBoot_printf("===========================================\n");
    wolfBoot_printf(" wolfBoot Test Application - AMD ZynqMP\n");
    wolfBoot_printf("===========================================\n\n");

    wolfBoot_printf("Current EL: %d\n", current_el());

#ifdef WOLFBOOT_FIXED_PARTITIONS
    boot_version = wolfBoot_get_image_version(PART_BOOT);
    update_version = wolfBoot_get_image_version(PART_UPDATE);
    wolfBoot_printf("BOOT: Version: %d (0x%08x)\n", boot_version, boot_version);
    wolfBoot_printf("UPDATE: Version: %d (0x%08x)\n", update_version, update_version);
#else
    wolfBoot_printf("Boot mode: Disk-based (MBR partitions)\n");
#endif

    wolfBoot_printf("Application running successfully!\n");
    wolfBoot_printf("\nEntering idle loop...\n");

    /* Idle loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}
#endif /** TARGET_zynq **/
