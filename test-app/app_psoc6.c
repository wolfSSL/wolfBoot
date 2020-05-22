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
#include "wolfboot/wolfboot.h"

#include "cy_gpio.h"

#define BOOT_LED P1_5
#define BOOT_D7 P5_7


void main(void) {
    Cy_PDL_Init(CY_DEVICE_CFG);
    Cy_GPIO_Pin_FastInit(P5_7_PORT, P5_7_NUM, CY_GPIO_DM_PULLUP, 0UL, P5_7_GPIO);
    Cy_GPIO_Pin_FastInit(P1_5_PORT, P1_5_NUM, CY_GPIO_DM_PULLUP, 0UL, P1_5_GPIO);
    while(1)
        ;
}

