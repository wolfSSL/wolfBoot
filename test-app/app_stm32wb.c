/* main.c
 *
 * Test bare-metal boot-led-on application
 *
 * Copyright (C) 2020 wolfSSL Inc.
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
#include "led.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"

#ifdef PLATFORM_stm32wb
char enc_key[33] = "01234566789abcdef0123456789abcdef";

volatile uint32_t time_elapsed = 0;
void main(void) {
    hal_init();
    boot_led_on();
    wolfBoot_success();
#if EXT_ENCRYPTED
    wolfBoot_set_encrypt_key((uint8_t *)enc_key, 32);
#endif


    /* Wait for reboot */
    while(1)
        ;
}
#endif /** PLATFROM_stm32wb **/
