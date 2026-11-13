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

/* LPUART0 (FRDM-MCXA153 console), clocked from FRO12M through MRCC0 */
#define LPUART0_BASE     0x4009F000u
#define LPUART_BAUD      (*(volatile uint32_t *)(LPUART0_BASE + 0x10u))
#define LPUART_STAT      (*(volatile uint32_t *)(LPUART0_BASE + 0x14u))
#define LPUART_CTRL      (*(volatile uint32_t *)(LPUART0_BASE + 0x18u))
#define LPUART_DATA      (*(volatile uint32_t *)(LPUART0_BASE + 0x1Cu))

#define LPUART_STAT_TDRE (1u << 23)
#define LPUART_STAT_RDRF (1u << 21)
#define LPUART_CTRL_RE   (1u << 18)
#define LPUART_CTRL_TE   (1u << 19)

#define MRCC0_BASE       0x40091000u
#define MRCC_GLB_RST0_SET (*(volatile uint32_t *)(MRCC0_BASE + 0x004u))
#define MRCC_GLB_RST0_CLR (*(volatile uint32_t *)(MRCC0_BASE + 0x008u))
#define MRCC_GLB_CC0_SET (*(volatile uint32_t *)(MRCC0_BASE + 0x044u))
#define MRCC_LPUART0_CLKSEL (*(volatile uint32_t *)(MRCC0_BASE + 0x0E0u))
#define MRCC_LPUART0_CLKDIV (*(volatile uint32_t *)(MRCC0_BASE + 0x0E4u))
#define SYSCON_CLKUNLOCK (*(volatile uint32_t *)(MRCC0_BASE + 0x3FCu))
#define MRCC_LPUART0_BIT (1u << 19)

/* OSR=15, SBR=7: 115200 baud from 12 MHz */
#define LPUART_BAUD_115200 (0x0F000000u | 7u)

void emu_uart_init(void)
{
    SYSCON_CLKUNLOCK &= ~1u;
    MRCC_GLB_CC0_SET = MRCC_LPUART0_BIT;
    MRCC_LPUART0_CLKSEL = 0u;
    MRCC_LPUART0_CLKDIV = 0u;
    MRCC_GLB_RST0_CLR = MRCC_LPUART0_BIT;
    MRCC_GLB_RST0_SET = MRCC_LPUART0_BIT;
    SYSCON_CLKUNLOCK |= 1u;
    LPUART_BAUD = LPUART_BAUD_115200;
    LPUART_CTRL = LPUART_CTRL_TE | LPUART_CTRL_RE;
}

void emu_uart_write(uint8_t c)
{
    while ((LPUART_STAT & LPUART_STAT_TDRE) == 0u) {
    }
    LPUART_DATA = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((LPUART_STAT & LPUART_STAT_RDRF) == 0u) {
        return 0;
    }
    *c = (uint8_t)LPUART_DATA;
    return 1;
}
