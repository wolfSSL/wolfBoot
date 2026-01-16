/* app_versal.c
 *
 * Test application for AMD Versal VMK180
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

#include <stdint.h>
#include "wolfboot/wolfboot.h"

/* UART registers for PL011 UART (Versal uses PL011, NOT Cadence UART)
 * Register layout from hal/versal.h:
 * - Data Register (DR): offset 0x00
 * - Flag Register (FR): offset 0x18
 * - Control Register (CR): offset 0x30
 */
#define VERSAL_UART0_BASE       0xFF000000UL
#define UART_DR_OFFSET          0x00    /* Data Register (TX/RX) */
#define UART_FR_OFFSET          0x18    /* Flag Register */
#define UART_FR_TXFF            (1UL << 5)  /* TX FIFO full */
#define UART_FR_TXFE            (1UL << 7)  /* TX FIFO empty */

#define UART_DR     (*((volatile uint32_t*)(VERSAL_UART0_BASE + UART_DR_OFFSET)))
#define UART_FR     (*((volatile uint32_t*)(VERSAL_UART0_BASE + UART_FR_OFFSET)))

/* Get current exception level */
static uint32_t get_current_el(void)
{
    uint64_t current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r" (current_el));
    return (uint32_t)((current_el >> 2) & 0x3);
}

static void uart_tx(uint8_t c)
{
    /* Wait while TX FIFO is full */
    while (UART_FR & UART_FR_TXFF)
        ;
    UART_DR = c;
}

static void uart_print(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_tx('\r');
        uart_tx((uint8_t)*s++);
    }
}

void main(void)
{
    uint32_t el = get_current_el();

    uart_print("\n\n");
    uart_print("===========================================\n");
    uart_print(" wolfBoot Test Application - AMD Versal\n");
    uart_print("===========================================\n\n");

    /* Print current exception level */
    uart_print("Current EL: ");
    uart_tx('0' + el);
    uart_print("\n");

    uart_print("Application running successfully!\n");

    uart_print("\nEntering idle loop...\n");

    /* Wait for transmit to complete (TX FIFO empty) */
    while (!(UART_FR & UART_FR_TXFE))
        ;

    /* Idle loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}


