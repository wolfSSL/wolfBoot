/* app_renesasRx01.c
 *
 * Test bare-metal application.
 *
 * Copyright (C) 2021 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

void main(void)
{
    uint8_t firmware_version = 0;

    hal_init();

    printf("| ------------------------------------------------------------------- |\n");
    printf("| Renesas RX User Application in BOOT partition started by wolfBoot   |\n");
    printf("| ------------------------------------------------------------------- |\n\n");

    /* The same as: wolfBoot_get_image_version(PART_BOOT); */
    firmware_version = wolfBoot_current_firmware_version();

    printf("Current Firmware Version: %d\n", firmware_version);

    if (firmware_version >= 1) {
        wolfBoot_success();
    } else {
        printf("Invalid Firmware Version\n");
        goto busy_idle;
    }

    printf("Hit any key to update the firmware.\n");
    getchar();

    wolfBoot_update_trigger();
    printf("Update Triggered\n");

    /* busy wait */
busy_idle:
    while (1)
        ;
}
