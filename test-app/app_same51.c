/* same51.c
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
#include "hal.h"
#include "wolfboot/wolfboot.h"

#define PORT_BASE      0x41008000 
#define GPIOA_BASE      PORT_BASE
#define GPIOA_DIR      *((volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OUT      *((volatile uint32_t *)(GPIOA_BASE + 0x10))
#define GPIOA_WRCONFIG *((volatile uint32_t *)(GPIOA_BASE + 0x28))

#define WRCONFIG_INEN (1 << 1)
#define WRCONFIG_PULLEN (1 << 2)

void main(void) {
    GPIOA_WRCONFIG &= ~(WRCONFIG_PULLEN | WRCONFIG_INEN);
    GPIOA_DIR |= (1 << 2);
    GPIOA_OUT |= (1 << 2);
    asm volatile ("cpsie i");
    while(1)
        asm volatile("WFI");
}
