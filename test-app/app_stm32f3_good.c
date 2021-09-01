/* stm32f4.c
 *
 * Test bare-metal blinking led application
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
#include "system.h"
#include "hal.h"
#include "wolfboot/wolfboot.h"




void main(void) {

	hal_init();

	volatile uint32_t reg;
	volatile uint32_t *AHB_ENABLE_REG = (uint32_t *)(0x40021000 + 0x14);
	volatile uint8_t  *GPIOC = (uint8_t *) 0x48000800;
	volatile uint32_t *GPIOC_MODER = (uint32_t *)GPIOC;
	volatile uint32_t *GPIOC_ODR = (uint32_t *)(GPIOC + 0x14);

	volatile uint8_t  *GPIOB = (uint8_t *) 0x48000400;
	volatile uint32_t *GPIOB_MODER = (uint32_t *)GPIOB;
	volatile uint32_t *GPIOB_ODR = (uint32_t *)(GPIOB + 0x14);
	volatile uint32_t *GPIOB_PUPDR = (uint32_t *)(GPIOB + 0x0C);
	volatile uint32_t *GPIOB_IDR = (uint32_t *)(GPIOB + 0x10);

	*AHB_ENABLE_REG |= (1 << 19) | (1 << 18);

	reg = *AHB_ENABLE_REG;
	reg = *AHB_ENABLE_REG;

	*GPIOB_PUPDR |= 0b10 << 24;

	*GPIOC_MODER |= (0b01) << 30;
	*GPIOC_ODR |= 1 << 15;

	while (!(*GPIOB_IDR & (1 << 12)));

	*GPIOC_ODR &= ~(1 << 15);
    wolfBoot_update_trigger();
	while(1);
}

