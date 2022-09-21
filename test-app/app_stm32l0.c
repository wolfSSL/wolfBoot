/* main.c
 *
 * Test bare-metal boot-led-on application
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "led.h"
#include "wolfboot/wolfboot.h"
#ifdef SPI_FLASH
#include "spi_flash.h"
#endif

#ifdef PLATFORM_stm32l0

/* Matches all keys:
 *    - chacha (32 + 12)
 *    - aes128 (16 + 16)
 *    - aes256 (32 + 16)
 */
/* Longest key possible: AES256 (32 key + 16 IV = 48) */
char enc_key[] = "0123456789abcdef0123456789abcdef"
		 "0123456789abcdef";

void main(void) {
    uint32_t version;
    volatile uint32_t i, j;
#ifdef SPI_FLASH
    spi_flash_probe();
#endif
    version = wolfBoot_current_firmware_version();

    if ((version % 2) == 1) {
        uint32_t sz;
#if EXT_ENCRYPTED
        wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
#endif
        wolfBoot_update_trigger();
    } else {
        wolfBoot_success();
    }

    for (i = 0; i < version; i++) {
        boot_led_on();
        for (j = 0; j < 200000; j++)
                ;
        boot_led_off();
        for (j = 0; j < 200000; j++)
                ;
    }
    boot_led_on();
    /* Wait for reboot */
    while(1)
        ;
}
#endif /** PLATFROM_stm32l0 **/

