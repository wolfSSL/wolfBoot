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

/* The non-secure world reaches UART0 through its alias; the secure world uses
 * the base address. wolfBoot hands UART0 to the NS world in hal_scu_init(). */
#define NS_OFFSET        0x10000000u
#define UART0_BASE_S     0x40070000u
#ifdef NONSECURE_APP
#  define UART0_BASE     (UART0_BASE_S + NS_OFFSET)
#else
#  define UART0_BASE     (UART0_BASE_S)
#endif

#define UART0_DAT        (*(volatile uint32_t *)(UART0_BASE + 0x000u))
#define UART0_FIFO       (*(volatile uint32_t *)(UART0_BASE + 0x008u))
#define UART0_LINE       (*(volatile uint32_t *)(UART0_BASE + 0x00Cu))
#define UART0_FIFOSTS    (*(volatile uint32_t *)(UART0_BASE + 0x018u))
#define UART0_BAUD       (*(volatile uint32_t *)(UART0_BASE + 0x024u))
#define UART0_FUNCSEL    (*(volatile uint32_t *)(UART0_BASE + 0x030u))

#define UART_FIFO_RXRST      (1u << 1)
#define UART_FIFO_TXRST      (1u << 2)
#define UART_LINE_WLS_8BIT   (0x3u << 0)
#define UART_FIFOSTS_RXEMPTY (1u << 14)
#define UART_FIFOSTS_TXFULL  (1u << 23)
#define UART_BAUD_BAUDM0     (1u << 28)
#define UART_BAUD_BAUDM1     (1u << 29)

/* Baud mode 2: BRD = ((src + baud/2) / baud) - 2, UART0 clocked from HIRC. */
#define CLK_HIRC_FREQ        12000000u
#define UART_BAUD_MODE2_DIVIDER(src, baud) \
    (((((src) + ((baud) / 2u)) / (baud)) - 2u))

void emu_uart_init(void)
{
    /* wolfBoot has already muxed PA6/PA7 and enabled the UART0 clock; the
     * non-secure world cannot reach SYS or CLK, so only the UART block
     * itself is touched here. */
    UART0_FUNCSEL = 0u;
    UART0_FIFO |= UART_FIFO_RXRST | UART_FIFO_TXRST;
    UART0_LINE = UART_LINE_WLS_8BIT;
    UART0_BAUD = UART_BAUD_BAUDM1 | UART_BAUD_BAUDM0 |
                 UART_BAUD_MODE2_DIVIDER(CLK_HIRC_FREQ, 115200u);
}

void emu_uart_write(uint8_t c)
{
    while ((UART0_FIFOSTS & UART_FIFOSTS_TXFULL) != 0u) {
    }
    UART0_DAT = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((UART0_FIFOSTS & UART_FIFOSTS_RXEMPTY) != 0u) {
        return 0;
    }
    *c = (uint8_t)(UART0_DAT & 0xFFu);
    return 1;
}
