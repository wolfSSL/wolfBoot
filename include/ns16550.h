/* ns16550.h
 *
 * Instance-based driver for NS16550-compatible UARTs.
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

#ifndef WOLFBOOT_NS16550_H
#define WOLFBOOT_NS16550_H

#include <stdint.h>
#include <stddef.h>

/* MMIO access. A port whose bus needs more than a volatile access supplies
 * these via -DNS16550_IO_H='"myport_io.h"' - PowerPC QorIQ must keep its
 * sync/twi/isync and sync/eieio sequences or MMIO ordering is lost.
 * 8-bit only: every port needing ordering so far is byte-wide. The 16- and
 * 32-bit paths use plain volatile accesses. */
#ifdef NS16550_IO_H
#include NS16550_IO_H
#endif
#ifndef NS16550_RD8
#define NS16550_RD8(a)     (*(volatile uint8_t*)(a))
#endif
#ifndef NS16550_WR8
#define NS16550_WR8(a, v) (*(volatile uint8_t*)(a) = (uint8_t)(v))
#endif

/* Register indices, before reg_shift is applied. */
#define NS16550_RBR  0x00 /* read:  receive buffer */
#define NS16550_THR  0x00 /* write: transmit holding */
#define NS16550_DLL  0x00 /* divisor low  (LCR.DLAB set) */
#define NS16550_IER  0x01
#define NS16550_DLM  0x01 /* divisor high (LCR.DLAB set) */
#define NS16550_FCR  0x02 /* write only */
#define NS16550_LCR  0x03
#define NS16550_MCR  0x04
#define NS16550_LSR  0x05

#define NS16550_LCR_8N1   0x03 /* 8 data bits, no parity, 1 stop bit */
#define NS16550_LCR_DLAB  0x80

#define NS16550_FCR_ENABLE 0x01
#define NS16550_FCR_RXRST  0x02
#define NS16550_FCR_TXRST  0x04

#define NS16550_MCR_DTR    0x01
#define NS16550_MCR_RTS    0x02

#define NS16550_LSR_DR     0x01 /* receive data ready */
#define NS16550_LSR_THRE   0x20 /* transmit holding register empty */
#define NS16550_LSR_TEMT   0x40 /* transmitter fully empty (shift reg too) */

/* One NS16550-compatible port. Each field is the devicetree property of the
 * same name; io_width is "reg-io-width", 0 derives it from reg_shift. */
struct ns16550_dev {
    uintptr_t base;
    uint32_t  reg_shift;
    uint32_t  reg_off;
    uint32_t  clk_hz;   /* 0 = ask the HAL via ns16550_hal_clk_hz() */
    uint8_t   io_width;
    uint8_t   crlf;     /* console duty: expand '\n' to '\r\n' */
};

/* Return codes. */
#define NS16550_OK        0
#define NS16550_ERR_ARG (-1)
#define NS16550_ERR_CLK (-2)  /* clock/baud combination has no usable divisor */
#define NS16550_ERR_TMO (-3)  /* transmitter never drained; port likely absent */

/* 8N1 at `baud`, FIFOs enabled and reset. Returns a NS16550_* code. */
int ns16550_init(struct ns16550_dev* dev, uint32_t baud);

/* Write `len` bytes, expanding '\n' to '\r\n' when dev->crlf is set. Bounded
 * waits, so an absent port returns NS16550_ERR_TMO rather than hanging. */
int ns16550_write(struct ns16550_dev* dev, const char* buf, uint32_t len);

/* Read one byte. Returns NS16550_OK with *c set, or NS16550_ERR_TMO when
 * nothing arrived within the bound. */
int ns16550_read(struct ns16550_dev* dev, uint8_t* c);

/* Non-blocking: nonzero if a byte is waiting. */
int ns16550_can_read(struct ns16550_dev* dev);

/* Supplied by a port that leaves clk_hz 0 because its UART clock is only
 * known at runtime (QorIQ derives it from the bus clock). */
uint32_t ns16550_hal_clk_hz(void);

#endif /* WOLFBOOT_NS16550_H */
