/* nrf52.c
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "target.h"
#include "wolfboot/wolfboot.h"
#include "hal/nrf52.h"
#include "printf.h"

static const char extradata[1024 * 16] = "hi!";

static void gpiotoggle(uint32_t pin)
{
    uint32_t reg_val = GPIO_OUT;
    GPIO_OUTCLR = reg_val & (1 << pin);
    GPIO_OUTSET = (~reg_val) & (1 << pin);
}

static const char* START="*";
void main(void)
{
    //uint32_t pin = 19;
    uint32_t pin = 6;
    int i;
    uint32_t version = 0;
    uint8_t *v_array = (uint8_t *)&version;
    (void)extradata;
    GPIO_PIN_CNF[pin] = 1; /* Output */

    version = wolfBoot_current_firmware_version();

    uart_init();
    uart_write(START, 1);
    for (i = 3; i >= 0; i--) {
        uart_write((const char*)&v_array[i], 1);
    }
    while (1) {
        gpiotoggle(pin);
        for (i = 0; i < 800000; i++)  /* Wait a bit. */
              asm volatile ("nop");
    }
}
