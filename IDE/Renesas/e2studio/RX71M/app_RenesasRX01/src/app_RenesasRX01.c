/* app_RenesasRX01.c
 *
 * Test bare-metal application for the RX71M CC-RX project.
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
#include <stdint.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "r_smc_entry.h"

static const char* state2str(uint8_t s)
{
    switch (s) {
        case IMG_STATE_NEW: return "New";
        case IMG_STATE_UPDATING: return "Updating";
        case IMG_STATE_TESTING: return "Testing";
        case IMG_STATE_SUCCESS: return "Success";
        default: return "Unknown";
    }
}

static void printPart(uint8_t part_id, uint8_t *part)
{
    uint8_t *magic;
    uint32_t ver;
    uint8_t  state;
    int      has_trailer;

    magic = part;
    printf("Magic:    %c%c%c%c\n", magic[0], magic[1], magic[2], magic[3]);
    ver = wolfBoot_get_blob_version(part);
    printf("Version:  %02x\n", (uint8_t)ver);
    has_trailer = (wolfBoot_get_partition_state(part_id, &state) == 0);
    if (!has_trailer)
        state = IMG_STATE_NEW;
    printf("Status:   %02x (%s)\n", state, state2str(state));
    printf("Trailer Mgc: %s\n", has_trailer ? "BOOT" : "none");
}

static void printPartitions(void)
{
    printf("\n=== Boot Partition[%08x] ===\n", WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printPart(PART_BOOT, (uint8_t*)WOLFBOOT_PARTITION_BOOT_ADDRESS);
    printf("\n=== Update Partition[%08x] ===\n", WOLFBOOT_PARTITION_UPDATE_ADDRESS);
    printPart(PART_UPDATE, (uint8_t*)WOLFBOOT_PARTITION_UPDATE_ADDRESS);
}

void main(void)
{
    uint8_t firmware_version = 0;

    hal_init();

    printf("| ------------------------------------------------------------------- |\n");
    printf("| Renesas RX User Application in BOOT partition started by wolfBoot   |\n");
    printf("| ------------------------------------------------------------------- |\n\n");

    printPartitions();

    firmware_version = wolfBoot_current_firmware_version();

    printf("\nCurrent Firmware Version: %d\n", firmware_version);

    if (firmware_version == 1) {
        printf("Hit any key to call wolfBoot_success the firmware.\n");
        (void)getchar();

        wolfBoot_success();
        printPartitions();

        printf("\nHit any key to update the firmware.\n");
        (void)getchar();

        wolfBoot_update_trigger();
        printf("Firmware Update is triggered\n");
        printPartitions();
    }
    else if (firmware_version == 2) {
        printf("Hit any key to call wolfBoot_success the firmware.\n");
        (void)getchar();

        wolfBoot_success();
        printPartitions();
    }
    else if (firmware_version == 0) {
        printf("Invalid Firmware Version\n");
    }

    /* busy wait */
    while (1)
        ;
}
