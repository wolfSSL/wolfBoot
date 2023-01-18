/* x86_uart.c
 *
 * Implementation of UART driver for x86
 *
 * Copyright (C) 2023 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <uart_drv.h>

#define COM1_BASE 0x3f8
#define COM1_THR (COM1_BASE + 0)
#define COM1_RBR (COM1_BASE + 0)
#define COM1_DLL (COM1_BASE + 0)
#define COM1_IER (COM1_BASE + 1)
#define COM1_DLH (COM1_BASE + 1)
#define COM1_LCR (COM1_BASE + 3)
#define COM1_LSR (COM1_BASE + 5)

#define PARITY_ODD 0x01
#define PARITY_EVEN 0x03
#define PARITY_NONE 0x0

#define DATA_5_BIT 0x0
#define DATA_6_BIT 0x1
#define DATA_7_BIT 0x2
#define DATA_8_BIT 0x3

#define ENABLE_DLA (0x1 << 7)
#define EMPTY_THR_BIT (0x1 << 5)

static void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_wait_tx_ready()
{
    while ((inb(COM1_LSR) & EMPTY_THR_BIT) == 0) {
    }
}

/* defaults to 115200 8-N-1 */
int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint8_t parity_bits, mode, stops;
    uint16_t divisor;

    switch (parity) {
    case 'O':
        parity_bits = PARITY_ODD;
        break;
    case 'E':
        parity_bits = PARITY_EVEN;
        break;
    case 'N':
        parity_bits = PARITY_NONE;
        break;
    default:
        return -1;
    }

    switch (data) {
    case 5:
        data = DATA_5_BIT;
        break;
    case 6:
        data = DATA_6_BIT;
        break;
    case 7:
        data = DATA_7_BIT;
        break;
    case 8:
        data = DATA_8_BIT;
        break;
    default:
        return -1;
    }

    stops = 0;
    if (stops > 1)
        stops = 0x01;

    if (bitrate == 0)
        return -1;
    divisor = 115200 / bitrate;

    outb(COM1_LCR, ENABLE_DLA);

    outb(COM1_DLL, (uint8_t)divisor);
    outb(COM1_DLH, (uint8_t)(divisor >> 8));

    mode = 0;
    mode |= data;
    mode |= (stop << 2);
    mode |= (parity_bits << 3);
    outb(COM1_LCR, mode);

    return 0;
}

int uart_tx(const uint8_t c)
{
    serial_wait_tx_ready();
    outb(COM1_THR, c);
    return 1;
}

int uart_rx(uint8_t *c)
{
    (void)c;
    /* unsupported */
    return -1;
}

void uart_write(const char *buf, unsigned int len)
{
    while (len--) {
        serial_wait_tx_ready();
        outb(COM1_THR, (uint8_t)*buf);
        buf++;
    }
}
