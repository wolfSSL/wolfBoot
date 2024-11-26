/* app_stm32l4.c
 *
 * Test bare-metal boot-led-on application
 *
 * Copyright (C) 2020 wolfSSL Inc.
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


#include "led.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#ifdef TARGET_stm32l4

void main(void)
{
    uint32_t boot_version;
    hal_init();
    boot_led_on();
    boot_version = wolfBoot_current_firmware_version();
    if (boot_version == 1) {
        /* Turn on Blue LED */
        boot_led_on();
        wolfBoot_update_trigger();
    } else if (boot_version >= 2) {
        /* Turn on Red LED */
        led_on();
        wolfBoot_success();
    }

    /* Wait for reboot */
    while(1) {
    }
}

#endif /* TARGET_stm32l4 */
