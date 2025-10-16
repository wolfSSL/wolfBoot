/* x86_uart.c
 *
 * Implementation of a very basic 8250 UART driver for x86
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
#include <uart_drv.h>

#include <x86/common.h>

#ifndef X86_UART_BASE
#define X86_UART_BASE 0x3f8
#undef X86_UART_MMIO
#define X86_UART_REG_WIDTH 1
#endif

#define X86_UART_REG(REG) (X86_UART_BASE + (REG*X86_UART_REG_WIDTH))
#define X86_UART_THR (X86_UART_REG(0))
#define X86_UART_RBR (X86_UART_REG(0))
#define X86_UART_DLL (X86_UART_REG(0))
#define X86_UART_IER (X86_UART_REG(1))
#define X86_UART_DLH (X86_UART_REG(1))
#define X86_UART_LCR (X86_UART_REG(3))
#define X86_UART_LSR (X86_UART_REG(5))

#define PARITY_ODD 0x01
#define PARITY_EVEN 0x03
#define PARITY_NONE 0x0

#define DATA_5_BIT 0x0
#define DATA_6_BIT 0x1
#define DATA_7_BIT 0x2
#define DATA_8_BIT 0x3

#define ENABLE_DLA (0x1 << 7)
#define EMPTY_THR_BIT (0x1 << 5)
#define LSR_DR_BIT (0x1 << 0)


#ifdef X86_UART_MMIO

#if X86_UART_REG_WIDTH != 4
#error "x86_uart: reg width not supported"
#endif /* X86_UART_REG_WIDTH != 4 */

static void write_reg(uint32_t address, uint8_t value)
{
    mmio_write32(address, value);
}
static uint8_t read_reg(uint32_t address)
{
    uint32_t reg;
    reg = mmio_read32(address);
    return (uint8_t)(reg & 0xff);
}
#else
static void write_reg(uint32_t port, uint8_t value)
{
    io_write8((uint16_t)port, value);
}

static uint8_t read_reg(uint32_t port)
{
    return io_read8((uint16_t)port);
}
#endif /* X86_UART_MMIO */

static void serial_wait_tx_ready()
{
    while ((read_reg(X86_UART_LSR) & EMPTY_THR_BIT) == 0)
        {
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

    write_reg(X86_UART_LCR, ENABLE_DLA);

    write_reg(X86_UART_DLL, (uint8_t)divisor);
    write_reg(X86_UART_DLH, (uint8_t)(divisor >> 8));

    mode = 0;
    mode |= data;
    mode |= (stops << 2);
    mode |= (parity_bits << 3);
    write_reg(X86_UART_LCR, mode);

    return 0;
}

int uart_tx(const uint8_t c)
{
    serial_wait_tx_ready();
    write_reg(X86_UART_THR, c);
    return 1;
}

int uart_rx(uint8_t *c)
{
    uint8_t lsr;

    lsr = read_reg(X86_UART_LSR);
    if ((lsr & LSR_DR_BIT) == 0)
        return -1;

    *c = read_reg(X86_UART_RBR) & 0xff;
    return 0;
}

void uart_write(const char *buf, unsigned int len)
{
    while (len--) {
        uart_tx(*buf);
        buf++;
    }
}
