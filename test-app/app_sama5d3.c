/* app_sama5d3.c
 *
 * Test bare-metal boot application
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
#include <hal/sama5d3.h>

#include "wolfboot/wolfboot.h"

#ifdef TARGET_sama5d3
/* Blue LED is PE23, Red LED is PE24 */

#define BLUE_LED_PIN 23
#define RED_LED_PIN 24

void led_init(uint32_t pin)
{
    uint32_t mask = 1U << pin;
    GPIOE_MDDR |= mask;
    GPIOE_PER |= mask;
    GPIOE_IDR |= mask;
    GPIOE_PPUDR |= mask;
    GPIOE_CODR |= mask;
}

void led_put(uint32_t pin, int val)
{
    uint32_t mask = 1U << pin;
    if (val)
        GPIOE_SODR |= mask;
    else
        GPIOE_CODR |= mask;
}

volatile uint32_t time_elapsed = 0;
void main(void) {

    /* Wait for reboot */
    led_init(RED_LED_PIN);
    led_put(RED_LED_PIN, 1);

    while(1)
        ;
}
#endif /** TARGET_sama5d3 **/
