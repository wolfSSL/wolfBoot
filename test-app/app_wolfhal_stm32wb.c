/* app_wolfhal_stm32wb.c
 *
 * Test bare-metal application using wolfHAL for the STM32WB55 Nucleo
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

#include <wolfHAL/gpio/stm32wb_gpio.h>
#include <wolfHAL/uart/stm32wb_uart.h>
#include <wolfHAL/clock/stm32wb_rcc.h>
#include <wolfHAL/platform/st/stm32wb55xx.h>

#ifdef TARGET_wolfhal_stm32wb

/* Re-use the clock instance from the board file */
extern whal_Clock g_wbClock;

/* GPIO pin indices */
enum {
    LED_PIN,
    UART_TX_PIN,
    UART_RX_PIN,
};

/* GPIO — LED on PB5, UART1 TX/RX on PB6/PB7 */
static whal_Gpio wbGpio = {
    .regmap = { .base = 0x48000000, .size = 0x400 },

    .cfg = &(whal_Stm32wbGpio_Cfg) {
        .clkCtrl = &g_wbClock,
        .clk = (const void *[1]) {
            &(whal_Stm32wbRcc_Clk){WHAL_STM32WB55_GPIOB_CLOCK},
        },
        .clkCount = 1,

        .pinCfg = (whal_Stm32wbGpio_PinCfg[3]) {
            [LED_PIN] = {
                .port = WHAL_STM32WB_GPIO_PORT_B,
                .pin = 5,
                .mode = WHAL_STM32WB_GPIO_MODE_OUT,
                .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
                .speed = WHAL_STM32WB_GPIO_SPEED_LOW,
                .pull = WHAL_STM32WB_GPIO_PULL_UP,
            },
            [UART_TX_PIN] = {
                .port = WHAL_STM32WB_GPIO_PORT_B,
                .pin = 6,
                .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
                .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
                .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
                .pull = WHAL_STM32WB_GPIO_PULL_UP,
                .altFn = 7,
            },
            [UART_RX_PIN] = {
                .port = WHAL_STM32WB_GPIO_PORT_B,
                .pin = 7,
                .mode = WHAL_STM32WB_GPIO_MODE_ALTFN,
                .outType = WHAL_STM32WB_GPIO_OUTTYPE_PUSHPULL,
                .speed = WHAL_STM32WB_GPIO_SPEED_FAST,
                .pull = WHAL_STM32WB_GPIO_PULL_UP,
                .altFn = 7,
            },
        },
        .pinCount = 3,
    },
};

/* UART1 at 115200 baud */
static whal_Uart wbUart = {
    .regmap = { .base = 0x40013800, .size = 0x400 },

    .cfg = &(whal_Stm32wbUart_Cfg) {
        .clkCtrl = &g_wbClock,
        .clk = &(whal_Stm32wbRcc_Clk) {WHAL_STM32WB55_UART1_CLOCK},
        .baud = 115200,
    },
};

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

    /* Initialize GPIO and UART via wolfHAL */
    whal_Stm32wbGpio_Init(&wbGpio);
    whal_Stm32wbUart_Init(&wbUart);

    /* LED on */
    whal_Stm32wbGpio_Set(&wbGpio, LED_PIN, 1);

    version = wolfBoot_current_firmware_version();
    updv = wolfBoot_update_firmware_version();

    ver_buf[0] = '*';
    ver_buf[1] = (version >> 24) & 0xFF;
    ver_buf[2] = (version >> 16) & 0xFF;
    ver_buf[3] = (version >> 8) & 0xFF;
    ver_buf[4] = version & 0xFF;
    whal_Stm32wbUart_Send(&wbUart, ver_buf, sizeof(ver_buf));

    if ((version == 1) && (updv != 8)) {
        /* LED off */
        whal_Stm32wbGpio_Set(&wbGpio, LED_PIN, 0);
#if EXT_ENCRYPTED
        wolfBoot_set_encrypt_key((uint8_t *)enc_key,
                                 (uint8_t *)(enc_key + 32));
#endif
        wolfBoot_update_trigger();
        /* LED on */
        whal_Stm32wbGpio_Set(&wbGpio, LED_PIN, 1);
    } else {
        if (version != 7)
            wolfBoot_success();
    }

    /* Wait for reboot */
    while (1)
        __asm__ volatile("wfi");
}

#endif /* TARGET_wolfhal_stm32wb */
