/* main.c
 *
 * Test bare-metal boot-led-on application
 *
 * Copyright (C) 2025 wolfSSL Inc.
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
#define WOLFBOOT_FIXED_PARTITIONS

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "wolfboot/wolfboot.h"
#include <wolfHAL/wolfHAL.h>
#include "target.h"
#include "hal.h"

#ifdef TARGET_wolfhal

#define BOOT_LED_PIN 0

extern whal_Clock wbClock;
#ifndef WOLFHAL_NO_GPIO
extern whal_Gpio wbGpio;
#endif
extern whal_Flash wbFlash;
#if defined(DEBUG_UART) || defined(UART_FLASH)
extern whal_Uart wbUart;
#endif /* DEBUG_UART || UART_FLASH */

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

#ifndef WOLFHAL_NO_GPIO
    whal_Gpio_Set(&wbGpio, BOOT_LED_PIN, 1);
#endif

    version = wolfBoot_current_firmware_version();
    updv = wolfBoot_update_firmware_version();
    
#if defined(DEBUG_UART) || defined(UART_FLASH)
    whal_Uart_Send(&wbUart, "*", 1);
    whal_Uart_Send(&wbUart, (uint8_t *)&version, 4);
#endif /* DEBUG_UART || UART_FLASH */
    
    if ((version == 1) && (updv != 8)) {
        uint32_t sz;
#ifndef WOLFHAL_NO_GPIO
        whal_Gpio_Set(&wbGpio, BOOT_LED_PIN, 0);
#endif
#if EXT_ENCRYPTED
        wolfBoot_set_encrypt_key((uint8_t *)enc_key,(uint8_t *)(enc_key +  32));
#endif
        wolfBoot_update_trigger();
#ifndef WOLFHAL_NO_GPIO
        whal_Gpio_Set(&wbGpio, BOOT_LED_PIN, 1);
#endif
    } else {
        if (version != 7)
            wolfBoot_success();
    }
    /* Wait for reboot */
    while(1)
        asm volatile("wfi");
}
#endif /** PLATFROM_wolfhal **/
