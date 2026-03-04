/* app_wolfhal.c
 *
 * Generic test bare-metal application using wolfHAL
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#include "board.h"

#ifdef TARGET_wolfhal

/* Board-provided device instances */
extern whal_Gpio g_wbGpio;
extern whal_Uart g_wbUart;

/* Matches all keys:
 *    - chacha (32 + 12)
 *    - aes128 (16 + 16)
 *    - aes256 (32 + 16)
 */
char enc_key[] = "0123456789abcdef0123456789abcdef"
                 "0123456789abcdef";

volatile uint32_t time_elapsed = 0;

void main(void)
{
    uint32_t version;
    uint32_t updv;
    uint8_t ver_buf[5];

    hal_init();

    /* LED on */
    whal_Gpio_Set(&g_wbGpio, BOARD_LED_PIN, 1);

    version = wolfBoot_current_firmware_version();
    updv = wolfBoot_update_firmware_version();

    ver_buf[0] = '*';
    ver_buf[1] = (version >> 24) & 0xFF;
    ver_buf[2] = (version >> 16) & 0xFF;
    ver_buf[3] = (version >> 8) & 0xFF;
    ver_buf[4] = version & 0xFF;
    whal_Uart_Send(&g_wbUart, ver_buf, sizeof(ver_buf));

    if ((version == 1) && (updv != 8)) {
        /* LED off */
        whal_Gpio_Set(&g_wbGpio, BOARD_LED_PIN, 0);
#if EXT_ENCRYPTED
        wolfBoot_set_encrypt_key((uint8_t *)enc_key,
                                 (uint8_t *)(enc_key + 32));
#endif
        wolfBoot_update_trigger();
        /* LED on */
        whal_Gpio_Set(&g_wbGpio, BOARD_LED_PIN, 1);
    } else {
        if (version != 7)
            wolfBoot_success();
    }

    /* Wait for reboot */
    while (1)
        __asm__ volatile("wfi");
}

#endif /* TARGET_wolfhal */
