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

/* UARTE20, the console wolfBoot itself prints on (DEBUG_UART). */
#ifdef TZEN
#define UARTE20_BASE             0x400C6000u
#else
#define UARTE20_BASE             0x500C6000u
#endif
#define UARTE_TASKS_DMA_RX_START (*(volatile uint32_t *)(UARTE20_BASE + 0x028u))
#define UARTE_TASKS_DMA_TX_START (*(volatile uint32_t *)(UARTE20_BASE + 0x050u))
#define UARTE_EVENTS_DMA_RX_END  (*(volatile uint32_t *)(UARTE20_BASE + 0x14Cu))
#define UARTE_EVENTS_DMA_TX_END  (*(volatile uint32_t *)(UARTE20_BASE + 0x168u))
#define UARTE_ENABLE             (*(volatile uint32_t *)(UARTE20_BASE + 0x500u))
#define UARTE_BAUDRATE           (*(volatile uint32_t *)(UARTE20_BASE + 0x524u))
#define UARTE_DMA_RX_PTR         (*(volatile uint32_t *)(UARTE20_BASE + 0x704u))
#define UARTE_DMA_RX_MAXCNT      (*(volatile uint32_t *)(UARTE20_BASE + 0x708u))
#define UARTE_DMA_TX_PTR         (*(volatile uint32_t *)(UARTE20_BASE + 0x73Cu))
#define UARTE_DMA_TX_MAXCNT      (*(volatile uint32_t *)(UARTE20_BASE + 0x740u))
#define UARTE_ENABLE_ENABLED     8u
#define UARTE_BAUDRATE_115200    0x01D60000u

static volatile uint8_t uart_tx_byte;
static volatile uint8_t uart_rx_byte;

static void uarte20_start_rx(void)
{
    UARTE_DMA_RX_PTR = (uint32_t)&uart_rx_byte;
    UARTE_DMA_RX_MAXCNT = 1u;
    UARTE_EVENTS_DMA_RX_END = 0u;
    UARTE_TASKS_DMA_RX_START = 1u;
}

void emu_uart_init(void)
{
    /* Disabling UARTE20 would drop the emulator PTY wolfBoot opened on it */
    if (UARTE_ENABLE != UARTE_ENABLE_ENABLED) {
        UARTE_BAUDRATE = UARTE_BAUDRATE_115200;
        UARTE_ENABLE = UARTE_ENABLE_ENABLED;
    }
    uarte20_start_rx();
}

void emu_uart_write(uint8_t c)
{
    uart_tx_byte = c;
    UARTE_DMA_TX_PTR = (uint32_t)&uart_tx_byte;
    UARTE_DMA_TX_MAXCNT = 1u;
    UARTE_EVENTS_DMA_TX_END = 0u;
    UARTE_TASKS_DMA_TX_START = 1u;
    while (UARTE_EVENTS_DMA_TX_END == 0u) {
    }
}

int emu_uart_read(uint8_t *c)
{
    if (UARTE_EVENTS_DMA_RX_END == 0u) {
        return 0;
    }
    UARTE_EVENTS_DMA_RX_END = 0u;
    *c = uart_rx_byte;
    uarte20_start_rx();
    return 1;
}
