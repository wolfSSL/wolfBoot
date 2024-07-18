/* app_RA.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include "hal_data.h"
#include "wolfboot/wolfboot.h"

extern bsp_leds_t g_bsp_leds;

void R_BSP_WarmStart(bsp_warm_start_event_t event);
int myprintf(const char * sFormat, ...);

static void blink(int interval)
{

    /* LED type structure */
    bsp_leds_t leds = g_bsp_leds;

#if BSP_TZ_SECURE_BUILD

    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif

    /* Define the units to be used with the software delay function */
    const bsp_delay_units_t bsp_delay_units = BSP_DELAY_UNITS_MILLISECONDS * interval;
    /* Calculate the delay in terms of bsp_delay_units */
    const uint32_t delay = bsp_delay_units / 2;

    /* Holds level to set for pins */
    bsp_io_level_t pin_level = BSP_IO_LEVEL_LOW;

    while (1)
    {
        /* Enable access to the PFS registers */
        R_BSP_PinAccessEnable();


        /* Update each LEDs*/
        for (uint32_t i = 0; i < leds.led_count; i++)
        {
            /* Get pin to toggle */
            uint32_t pin = leds.p_leds[i];

            /* Write to this pin */
            R_BSP_PinWrite((bsp_io_port_pin_t) pin, pin_level);

            /* Delay */
            R_BSP_SoftwareDelay(delay, bsp_delay_units);
        }

        /* Protect PFS registers */
        R_BSP_PinAccessDisable();

        /* Toggle level for next write */
        if (BSP_IO_LEVEL_LOW == pin_level) {
            pin_level = BSP_IO_LEVEL_HIGH;
        }
        else {
            pin_level = BSP_IO_LEVEL_LOW;
        }
    }
}

static void printPart(uint8_t *part)
{
#ifdef WOLFBOOT_DEBUG_PARTION
    uint32_t *v;
    int i;
#endif
    uint8_t  *magic;
    uint8_t  state;
    uint32_t ver;

    magic = part;
    myprintf("Magic:    %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    ver = wolfBoot_get_blob_version(part);
    myprintf("Version:  %02x\n", ver);
    state = *(part + WOLFBOOT_PARTITION_SIZE - sizeof(uint32_t) - 1);
    myprintf("Status:   %02x\n", state);
    magic = part + WOLFBOOT_PARTITION_SIZE - sizeof(uint32_t);
    myprintf("Trailer Magic: %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);

#ifdef WOLFBOOT_DEBUG_PARTION
    v = (uint32_t *)part;
    for(i = 0; i < 0x100/4; i++) {
        if(i % 4 == 0)
            myprintf("\n%08x: ", (uint32_t)v+i*4);
        myprintf("%08x ", v[i]);
    }
#endif

}

static void printPartitions(void)
{
    myprintf("\n=== Boot Partition[%08x] ===\n", WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printPart((uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS);
    myprintf("\n=== Update Partition[%08x] ===\n", WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printPart((uint8_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
}

void app_RA(void)
{
    uint8_t firmware_version = 0;

    R_BSP_WarmStart(BSP_WARM_START_POST_C);

    hal_init();

#ifndef WOLFBOOT_RENESAS_SCEPROTECT
    myprintf("| ------------------------------------------------------------------- |\n");
    myprintf("| Renesas RA User Application in BOOT partition started by wolfBoot   |\n");
    myprintf("| ------------------------------------------------------------------- |\n\n");
#else
    myprintf("| ----------------------------------------------------------------------- |\n");
    myprintf("| Renesas RA SCE User Application in BOOT partition started by wolfBoot   |\n");
    myprintf("| ----------------------------------------------------------------------- |\n\n");
#endif

    myprintf("\n");
    myprintf("WOLFBOOT_PARTITION_SIZE:           0x%08x\n",
                WOLFBOOT_PARTITION_SIZE);
    myprintf("WOLFBOOT_PARTITION_BOOT_ADDRESS:   0x%08x\n",
                WOLFBOOT_PARTITION_BOOT_ADDRESS);
    myprintf("WOLFBOOT_PARTITION_UPDATE_ADDRESS: 0x%08x\n",
                WOLFBOOT_PARTITION_UPDATE_ADDRESS);

    myprintf("\n");
    myprintf("Application Entry Address:         0x%08x\n",
                WOLFBOOT_PARTITION_BOOT_ADDRESS+IMAGE_HEADER_SIZE);

    printPartitions();

    /* The same as: wolfBoot_get_image_version(PART_BOOT); */
    firmware_version = wolfBoot_current_firmware_version();
    myprintf("Current Firmware Version : %d\n", firmware_version);

    if (firmware_version >= 1) {
        myprintf("\n");
        myprintf("Calling wolfBoot_success()");
        wolfBoot_success();
        myprintf("\n");
        myprintf("Called wolfBoot_success()");
        printPartitions();

    } else {
        goto busy_idle;
    }

    if (firmware_version == 1) {
        wolfBoot_update_trigger();
        blink(1);
    }
    else if (firmware_version == 2) {
        blink(5);
    }
    /* busy wait */
busy_idle:
    /* flashing LEDs in busy */
    blink(1);
}
