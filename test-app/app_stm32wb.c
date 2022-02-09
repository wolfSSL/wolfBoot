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
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "uart_drv.h"

#ifdef PLATFORM_stm32wb

/* Matches all keys:
 *    - chacha (32 + 12)
 *    - aes128 (16 + 16)
 *    - aes256 (32 + 16)
 */
/* Longest key possible: AES256 (32 key + 16 IV = 48) */
char enc_key[] = "0123456789abcdef0123456789abcdef"
		 "0123456789abcdef";

volatile uint32_t time_elapsed = 0;
void main(void) {
    uint32_t version;
    uint32_t l = 0;
    uint32_t updv;
    hal_init();
    boot_led_on();
    uart_init(115200, 8, 'N', 1);
#ifdef SPI_FLASH
    spi_flash_probe();
#endif
    version = wolfBoot_current_firmware_version();
    updv = wolfBoot_update_firmware_version();
    uart_tx('*');
    uart_tx((version >> 24) & 0xFF);
    uart_tx((version >> 16) & 0xFF);
    uart_tx((version >> 8) & 0xFF);
    uart_tx(version & 0xFF);
    if ((version == 1) && (updv != 8)) {
        uint32_t sz;
        boot_led_off();
#if EXT_ENCRYPTED
        wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
#endif
        wolfBoot_update_trigger();
        boot_led_on();
    } else {
        if (version != 7)
            wolfBoot_success();
    }
    /* Wait for reboot */
    while(1)
        asm volatile("wfi");
}
#endif /** PLATFROM_stm32wb **/
