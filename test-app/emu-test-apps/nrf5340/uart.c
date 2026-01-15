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

#define UARTE0_BASE      0x40008000u
#define CLOCK_BASE       0x40005000u
#define CLOCK_TASKS_HFCLKSTART (*(volatile uint32_t *)((CLOCK_BASE) + 0x000u))
#define UARTE_TASKS_STARTRX(b) (*(volatile uint32_t *)((b) + 0x000u))
#define UARTE_TASKS_STOPRX(b)  (*(volatile uint32_t *)((b) + 0x004u))
#define UARTE_TASKS_STARTTX(b) (*(volatile uint32_t *)((b) + 0x008u))
#define UARTE_EVENTS_ENDRX(b)  (*(volatile uint32_t *)((b) + 0x110u))
#define UARTE_EVENTS_ENDTX(b)  (*(volatile uint32_t *)((b) + 0x120u))
#define UARTE_ENABLE(b)        (*(volatile uint32_t *)((b) + 0x500u))
#define UARTE_PSEL_TXD(b)      (*(volatile uint32_t *)((b) + 0x50Cu))
#define UARTE_PSEL_RXD(b)      (*(volatile uint32_t *)((b) + 0x514u))
#define UARTE_BAUDRATE(b)      (*(volatile uint32_t *)((b) + 0x524u))
#define UARTE_RXD_PTR(b)       (*(volatile uint32_t *)((b) + 0x534u))
#define UARTE_RXD_MAXCNT(b)    (*(volatile uint32_t *)((b) + 0x538u))
#define UARTE_TXD_PTR(b)       (*(volatile uint32_t *)((b) + 0x544u))
#define UARTE_TXD_MAXCNT(b)    (*(volatile uint32_t *)((b) + 0x548u))

static volatile uint8_t uart_tx_byte;
static volatile uint8_t uart_rx_byte;

static void uarte0_start_rx(void)
{
    UARTE_RXD_PTR(UARTE0_BASE) = (uint32_t)&uart_rx_byte;
    UARTE_RXD_MAXCNT(UARTE0_BASE) = 1u;
    UARTE_EVENTS_ENDRX(UARTE0_BASE) = 0u;
    UARTE_TASKS_STARTRX(UARTE0_BASE) = 1u;
}

void emu_uart_init(void)
{
    CLOCK_TASKS_HFCLKSTART = 1u;
    UARTE_ENABLE(UARTE0_BASE) = 0u;
    UARTE_PSEL_TXD(UARTE0_BASE) = 0u;
    UARTE_PSEL_RXD(UARTE0_BASE) = 0u;
    UARTE_BAUDRATE(UARTE0_BASE) = 0x01D7E000u; /* 115200 */
    UARTE_ENABLE(UARTE0_BASE) = 8u;
    uarte0_start_rx();
}

void emu_uart_write(uint8_t c)
{
    uart_tx_byte = c;
    UARTE_TXD_PTR(UARTE0_BASE) = (uint32_t)&uart_tx_byte;
    UARTE_TXD_MAXCNT(UARTE0_BASE) = 1u;
    UARTE_EVENTS_ENDTX(UARTE0_BASE) = 0u;
    UARTE_TASKS_STARTTX(UARTE0_BASE) = 1u;
    while (UARTE_EVENTS_ENDTX(UARTE0_BASE) == 0u) {
    }
}

int emu_uart_read(uint8_t *c)
{
    if (UARTE_EVENTS_ENDRX(UARTE0_BASE) == 0u) {
        return 0;
    }
    UARTE_EVENTS_ENDRX(UARTE0_BASE) = 0u;
    *c = uart_rx_byte;
    uarte0_start_rx();
    return 1;
}
