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

/* UART registers (same as in hal/versal.h) */
#define VERSAL_UART0_BASE       0xFF000000UL
#define UART_SR_OFFSET          0x2C
#define UART_FIFO_OFFSET        0x30
#define UART_SR_TXFULL          (1UL << 4)
#define UART_SR_TXEMPTY         (1UL << 3)

#define UART_SR     (*((volatile uint32_t*)(VERSAL_UART0_BASE + UART_SR_OFFSET)))
#define UART_FIFO   (*((volatile uint32_t*)(VERSAL_UART0_BASE + UART_FIFO_OFFSET)))

static void uart_tx(uint8_t c)
{
    while (UART_SR & UART_SR_TXFULL)
        ;
    UART_FIFO = c;
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
    uart_print("\n\n");
    uart_print("===========================================\n");
    uart_print(" wolfBoot Test Application - AMD Versal\n");
    uart_print("===========================================\n\n");

    uart_print("Application running successfully!\n");

    uart_print("\nEntering idle loop...\n");

    /* Wait for transmit to complete */
    while (!(UART_SR & UART_SR_TXEMPTY))
        ;

    /* Idle loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}


