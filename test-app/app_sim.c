/* app_sim.c
 *
 * Test bare-metal boot-led-on application
 *
 * Copyright (C) 2022 wolfSSL Inc.
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "wolfboot/wolfboot.h"

#ifdef PLATFORM_sim

void hal_init(void);

int do_cmd(const char *cmd)
{
    if (strcmp(cmd, "get_version") == 0) {
        printf("%d", wolfBoot_current_firmware_version());
        return 0;
    }

    if (strcmp(cmd, "success") == 0) {
        wolfBoot_success();
        return 0;
    }

    if (strcmp(cmd, "update_trigger") == 0) {
        wolfBoot_update_trigger();
        return 0;
    }

    if (strcmp(cmd, "reset") == 0) {
        exit(0);
    }

    /* wrong command */
    return -1;
}

int main(int argc, char *argv[]) {

    int i;

    hal_init();

    for (i = 1; i < argc; ++i) {
        if (do_cmd(argv[i]) != 0)
            return -1;
    }

    return 0;

}
#endif /** PLATFROM_sim **/
