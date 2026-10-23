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
#include "emu_app.h"

/* LP_FLEXCOMM4 / LPUART4, Secure alias. wolfBoot (DEBUG_UART) set up the
 * clocks, pins and baud rate before the jump, so only TX/RX are enabled. */
#define LPUART4_BASE     0x500B4000u
#define LPUART4_STAT     (*(volatile uint32_t *)(LPUART4_BASE + 0x14u))
#define LPUART4_CTRL     (*(volatile uint32_t *)(LPUART4_BASE + 0x18u))
#define LPUART4_DATA     (*(volatile uint32_t *)(LPUART4_BASE + 0x1Cu))

#define UART_STAT_TDRE   0x00800000u
#define UART_STAT_RDRF   0x00200000u
#define UART_CTRL_TE_RE  0x000C0000u
#define UART_TX_POLL_LIMIT 100000u

void emu_uart_init(void)
{
    LPUART4_CTRL |= UART_CTRL_TE_RE;
}

void emu_uart_write(uint8_t c)
{
    uint32_t t = UART_TX_POLL_LIMIT;

    while (((LPUART4_STAT & UART_STAT_TDRE) == 0u) && (t > 0u)) {
        t--;
    }
    if (t != 0u) {
        LPUART4_DATA = (uint32_t)c;
    }
}

int emu_uart_read(uint8_t *c)
{
    if ((LPUART4_STAT & UART_STAT_RDRF) == 0u) {
        return 0;
    }
    *c = (uint8_t)(LPUART4_DATA & 0xFFu);
    return 1;
}
