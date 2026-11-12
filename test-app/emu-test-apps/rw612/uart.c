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

/* FLEXCOMM3 USART, non-secure alias. wolfBoot's DEBUG_UART console already
 * clocked it and set 115200 8N1; both worlds reach this alias. */
#define USART3_BASE          0x40109000u
#define USART3_CFG           (*(volatile uint32_t *)(USART3_BASE + 0x000u))
#define USART3_FIFOCFG       (*(volatile uint32_t *)(USART3_BASE + 0xE00u))
#define USART3_FIFOSTAT      (*(volatile uint32_t *)(USART3_BASE + 0xE04u))
#define USART3_FIFOWR        (*(volatile uint32_t *)(USART3_BASE + 0xE20u))
#define USART3_FIFORD        (*(volatile uint32_t *)(USART3_BASE + 0xE30u))

#define USART_CFG_ENABLE     0x00000001u
#define FIFOCFG_ENABLETX     0x00000001u
#define FIFOCFG_ENABLERX     0x00000002u
#define FIFOSTAT_TXNOTFULL   0x00000020u
#define FIFOSTAT_RXNOTEMPTY  0x00000040u

#define UART_TX_POLL_LIMIT   100000u

void emu_uart_init(void)
{
    USART3_FIFOCFG |= FIFOCFG_ENABLETX | FIFOCFG_ENABLERX;
    USART3_CFG |= USART_CFG_ENABLE;
}

void emu_uart_write(uint8_t c)
{
    uint32_t t = UART_TX_POLL_LIMIT;

    while (((USART3_FIFOSTAT & FIFOSTAT_TXNOTFULL) == 0u) && (t > 0u)) {
        t--;
    }
    if (t != 0u) {
        USART3_FIFOWR = (uint32_t)c;
    }
}

int emu_uart_read(uint8_t *c)
{
    if ((USART3_FIFOSTAT & FIFOSTAT_RXNOTEMPTY) == 0u) {
        return 0;
    }
    *c = (uint8_t)(USART3_FIFORD & 0xFFu);
    return 1;
}
