/* app_max32666.c
 *
 * Test application for wolfBoot on MAX32666FTHR
 *
 * Copyright (C) 2025 wolfSSL Inc.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "printf.h"

void main(void)
{
    uint32_t version;

    hal_init();

    version = wolfBoot_current_firmware_version();

    wolfBoot_printf("MAX32666 Test App v%lu\n", (unsigned long)version);

    /* Mark boot successful to prevent rollback */
    wolfBoot_success();

    wolfBoot_printf("Boot success marked. Version: %lu\n",
        (unsigned long)version);

    /* Main loop */
    while (1) {
        __asm__ volatile ("wfi");
    }
}
