/* app_RA.c
 *
 * Test bare-metal application.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "hal.h"
#include "hal_data.h"
#include "wolfboot/wolfboot.h"
#include "image.h"

extern bsp_leds_t g_bsp_leds;

void R_BSP_WarmStart(bsp_warm_start_event_t event);
int myprintf(const char * sFormat, ...);

static const char* state2str(uint8_t s)
{
    switch(s) {
        case IMG_STATE_NEW: return "New";
        case IMG_STATE_UPDATING: return "Updating";
        case IMG_STATE_TESTING: return "Testing";
        case IMG_STATE_SUCCESS: return "Success";
        default: return "Unknown";
    }
}

static const char* upFlag2str(uint8_t s)
{
    switch(s) {
        case SECT_FLAG_NEW: return "New";
        case SECT_FLAG_SWAPPING: return "Swapping";
        case SECT_FLAG_BACKUP: return "Backup";
        case SECT_FLAG_UPDATED: return "Updated";
        default: return "Unknown";
    }
}

static void blink(int order)
{

	int start, end, step;
    int i = 0;

    /* LED type structure */
    bsp_leds_t leds = g_bsp_leds;

#if BSP_TZ_SECURE_BUILD
    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif

    /* Define the units to be used with the software delay function */
    const bsp_delay_units_t bsp_delay_units = BSP_DELAY_UNITS_MILLISECONDS;
    const uint32_t freq = 1;
    /* Calculate the delay: 500ms * interval per LED write */
    const uint32_t delay = (uint32_t)(bsp_delay_units / (freq * 2));
    
    /* Holds level to set for pins */
    bsp_io_level_t pin_level = BSP_IO_LEVEL_LOW;
    start = 0;
    end = leds.led_count;
    step = (order == -1)? -1 : 1;

    if (order == -1) {
    	start = end - 1;
    	end = -1;
    }

    while (1)
    {
        /* Enable access to the PFS registers */
        R_BSP_PinAccessEnable();

        /* Update each LEDs*/
        i = start;
        for (;;)
        {
        	if (i == end) break;
            /* Get pin to toggle */
            uint32_t pin = leds.p_leds[i];
            /* Write to this pin */
            R_BSP_PinWrite((bsp_io_port_pin_t) pin, pin_level);

            R_BSP_SoftwareDelay(delay, bsp_delay_units);
            i += step;
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
        /* Delay */
        R_BSP_SoftwareDelay(delay, bsp_delay_units);
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
    uint8_t  upflag;

    magic = part;
    myprintf("Magic:    %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    ver = wolfBoot_get_blob_version(part);
    myprintf("Version:  %02x\n", ver);
    wolfBoot_get_partition_state(0, &state);
    myprintf("Status:   %02x(%s)\n", state, state2str(state));
    magic = part + WOLFBOOT_PARTITION_SIZE - sizeof(uint32_t);
    if (magic[0] != 0x42)
        magic = part + WOLFBOOT_PARTITION_SIZE - WOLFBOOT_SECTOR_SIZE - sizeof(uint32_t);
    myprintf("Trailer Mgc: %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    wolfBoot_get_update_sector_flag(0, &upflag);
    myprintf("Update flag: %02x (%s)\n", upflag, upFlag2str(upflag));

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
    SystemCoreClockUpdate();

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
        blink(-1);
    }
    else if (firmware_version == 2) {
        blink(1);
    }
    /* busy wait */
busy_idle:
    /* flashing LEDs in busy */
    blink(-1);
}
