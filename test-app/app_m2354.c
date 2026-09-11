/* app_m2354.c
 *
 * Test bare-metal application for the Nuvoton M2354 (NuMaker-M2354).
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

/* Console only: the NuMaker-M2354 user LED pins were not confirmed against a
 * schematic, so this app does not drive GPIO. */

#include <stdint.h>
#include "hal.h"
#include "hal/m2354.h"
#include "wolfboot/wolfboot.h"
#include "target.h"
#include "printf.h"

extern void uart_init(void);

static void busy_delay(uint32_t count)
{
    volatile uint32_t i;
    for (i = 0; i < count; i++)
        __asm__ volatile ("nop");
}

static void system_reset(void)
{
    AIRCR = AIRCR_VKEY | AIRCR_SYSRESETREQ;
    while (1)
        ;
}

void main(void)
{
    uint32_t version;
    uint32_t update_version;

    hal_init();
    uart_init();
    wolfBoot_printf("TEST APP\n");

#ifdef TZEN
    /* Non-secure: reach wolfBoot through the secure gateway veneers. */
    version = wolfBoot_nsc_current_firmware_version();
    update_version = wolfBoot_nsc_update_firmware_version();
#else
    version = wolfBoot_current_firmware_version();
    update_version = wolfBoot_update_firmware_version();
#endif
    wolfBoot_printf("App version: %d\n", (int)version);

    /* Confirm a completed update so the swap sticks. Without this wolfBoot
     * would roll back to the previous image on the next boot. */
    if (version > 1) {
#ifdef TZEN
        wolfBoot_nsc_success();
#else
        wolfBoot_success();
#endif
        wolfBoot_printf("update OK -- success confirmed\n");
    }

    /* Only ask for an update when the update partition actually holds a
     * newer image. Triggering unconditionally makes an empty or unusable
     * update slot look like a boot loop.
     */
    if (update_version > version) {
        wolfBoot_printf("update v%d available -> trigger, reset\n",
                (int)update_version);
        busy_delay(2000000);
#ifdef TZEN
        wolfBoot_nsc_update_trigger();
#else
        wolfBoot_update_trigger();
#endif
        system_reset();
    }

    wolfBoot_printf("no newer image (update slot v%d); idle\n",
            (int)update_version);
    while (1)
        ;
}
