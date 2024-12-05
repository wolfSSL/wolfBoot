/* nrf5340.c
 *
 * Copyright (C) 2024 wolfSSL Inc.
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
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "hal/nrf5340.h"
#include "printf.h"

void gpiotoggle(uint32_t port, uint32_t pin)
{
    uint32_t reg_val = GPIO_OUT(port);
    GPIO_OUTCLR(port) = reg_val & (1 << pin);
    GPIO_OUTSET(port) = (~reg_val) & (1 << pin);
}

void main(void)
{
    int i;
    /* nRF5340-DK LEDs:
     *  LED1 P0.28
     *  LED2 P0.29
     *  LED3 P0.30
     *  LED4 P0.31 */
    uint32_t port = 0;
    uint32_t pin = 28;
    uint32_t app_version;

    GPIO_PIN_CNF(port, pin) = GPIO_CNF_OUT;
    /* Allow network core access to P0.29 GPIO */
    GPIO_PIN_CNF(0, 29) = (GPIO_CNF_OUT | GPIO_CNF_MCUSEL(1));

    app_version = wolfBoot_current_firmware_version();

    uart_init();

    wolfBoot_printf("========================\n");
    wolfBoot_printf("nRF5340 wolfBoot (app core)\n");
    wolfBoot_printf("Copyright 2024 wolfSSL Inc\n");
    wolfBoot_printf("GPL v3\n");
    wolfBoot_printf("Version : 0x%lx\n", app_version);
    wolfBoot_printf("Compiled: " __DATE__ ":" __TIME__ "\n");
    wolfBoot_printf("========================\n");

    /* mark boot successful */
    wolfBoot_success();

    /* Toggle LED loop */
    while (1) {
        gpiotoggle(port, pin);

        sleep_us(500 * 1000);
    }
}
