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

/* LPUART1, the FRDM-MCXW71 console wolfBoot also logs on. Non-secure alias:
 * with TZEN=1 the wolfBoot SAU opens only this UART to the non-secure app. */
#define LPUART1_BASE     0x40039000u
#define LPUART1_STAT     (*(volatile uint32_t *)(LPUART1_BASE + 0x14u))
#define LPUART1_CTRL     (*(volatile uint32_t *)(LPUART1_BASE + 0x18u))
#define LPUART1_DATA     (*(volatile uint32_t *)(LPUART1_BASE + 0x1Cu))

#define UART_STAT_TDRE   0x00800000u
#define UART_STAT_RDRF   0x00200000u
#define UART_CTRL_TE_RE  0x000C0000u

#ifndef TZEN
/* MRCC gate: clock on (CC=1), reset released (RSTB) */
#define MRCC_LPUART1     (*(volatile uint32_t *)(0x4001C000u + 0xE4u))
#define MRCC_CC_MASK     0x00000003u
#define MRCC_RSTB        0x40000000u
#endif

void emu_uart_init(void)
{
#ifndef TZEN
    if ((MRCC_LPUART1 & MRCC_CC_MASK) == 0u) {
        MRCC_LPUART1 = MRCC_RSTB | 1u;
    }
#endif
    LPUART1_CTRL |= UART_CTRL_TE_RE;
}

void emu_uart_write(uint8_t c)
{
    while ((LPUART1_STAT & UART_STAT_TDRE) == 0u) {
    }
    LPUART1_DATA = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((LPUART1_STAT & UART_STAT_RDRF) == 0u) {
        return 0;
    }
    *c = (uint8_t)(LPUART1_DATA & 0xFFu);
    return 1;
}
