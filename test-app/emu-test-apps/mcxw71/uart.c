/* uart.c
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

#include <stdint.h>
#include "fsl_device_registers.h"
#include "emu_app.h"

/* MRCC */
#define MRCC_BASE        0x4001C000u
#define MRCC_LPUART0     0xE0u

static inline void mrcc_enable(uint32_t off)
{
    volatile uint32_t *reg = (volatile uint32_t *)(MRCC_BASE + off);
    *reg = (1u << 31) | (1u << 30) | 1u;
}

/* LPUART0 */
#define LPUART0_BASE     0x40038000u
#define LPUART_STAT(b)   (*(volatile uint32_t *)((b) + 0x14u))
#define LPUART_CTRL(b)   (*(volatile uint32_t *)((b) + 0x18u))
#define LPUART_DATA(b)   (*(volatile uint32_t *)((b) + 0x1Cu))

#define LPUART_STAT_TDRE (1u << 23)
#define LPUART_STAT_RDRF (1u << 21)
#define LPUART_CTRL_RE   (1u << 18)
#define LPUART_CTRL_TE   (1u << 19)

void emu_uart_init(void)
{
    mrcc_enable(MRCC_LPUART0);
    LPUART_CTRL(LPUART0_BASE) = LPUART_CTRL_TE | LPUART_CTRL_RE;
}

void emu_uart_write(uint8_t c)
{
    while ((LPUART_STAT(LPUART0_BASE) & LPUART_STAT_TDRE) == 0u) {
    }
    LPUART_DATA(LPUART0_BASE) = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((LPUART_STAT(LPUART0_BASE) & LPUART_STAT_RDRF) == 0u) {
        return 0;
    }
    *c = (uint8_t)LPUART_DATA(LPUART0_BASE);
    return 1;
}
