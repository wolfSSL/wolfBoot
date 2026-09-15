/* ns16550.c
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

#include <stdint.h>
#include "ns16550.h"
#include "wolfboot/wolfboot.h"   /* WEAKFUNCTION */

/* Bound on every busy-wait; only trips when the port is absent. */
#define NS16550_SPIN_MAX 1000000UL

static uint32_t ns16550_rd(const struct ns16550_dev* dev, uint32_t reg)
{
    uintptr_t addr = dev->base + dev->reg_off + ((uintptr_t)reg << dev->reg_shift);

    if (dev->io_width == 4) {
        return *((volatile uint32_t*)addr);
    }
    if (dev->io_width == 2) {
        return (uint32_t)(*((volatile uint16_t*)addr));
    }
    return (uint32_t)NS16550_RD8(addr);
}

static void ns16550_wr(const struct ns16550_dev* dev, uint32_t reg, uint32_t val)
{
    uintptr_t addr = dev->base + dev->reg_off + ((uintptr_t)reg << dev->reg_shift);

    if (dev->io_width == 4) {
        *((volatile uint32_t*)addr) = val;
    }
    else if (dev->io_width == 2) {
        *((volatile uint16_t*)addr) = (uint16_t)val;
    }
    else {
        NS16550_WR8(addr, val);
    }
}

/* Word-spaced registers imply word access unless the caller says otherwise. */
static void ns16550_fix_io_width(struct ns16550_dev* dev)
{
    if (dev->io_width == 1 || dev->io_width == 2 || dev->io_width == 4) {
        return;
    }
    dev->io_width = (dev->reg_shift >= 2) ? 4 : 1;
}

/* Weak default for ports that know their clock at build time. Returns 0,
 * which ns16550_init() rejects, so leaving clk_hz 0 without overriding this
 * is an error rather than a wrong baud rate. */
uint32_t WEAKFUNCTION ns16550_hal_clk_hz(void)
{
    return 0;
}

int ns16550_init(struct ns16550_dev* dev, uint32_t baud)
{
    uint32_t div;
    uint32_t lcr;
    uint32_t clk;

    /* The baud bound keeps baud * 16 from wrapping the divisor arithmetic
     * below; 64-bit intermediates would pull a libgcc divide into 32-bit
     * targets for a range no real port uses. */
    if (dev == NULL || dev->base == 0 || baud == 0 ||
            baud > (0xFFFFFFFFU / 16U)) {
        return NS16550_ERR_ARG;
    }
    if (dev->reg_shift > 3) {
        return NS16550_ERR_ARG;
    }
    ns16550_fix_io_width(dev);

    /* A port whose UART clock is only known at runtime leaves clk_hz 0. */
    clk = (dev->clk_hz != 0) ? dev->clk_hz : ns16550_hal_clk_hz();
    if (clk == 0) {
        return NS16550_ERR_CLK;
    }

    /* The latch is 16 bits and 0 means divide-by-65536, so out of range is a
     * bad clock/baud pairing rather than something to clamp. Rounded, not
     * truncated: 0.5% baud error instead of 1.4% at 115200 on 99.999 MHz. */
    div = (clk + (baud * 8U)) / (baud * 16U);
    if (div == 0U || div > 0xFFFFU) {
        return NS16550_ERR_CLK;
    }

    /* Mask interrupts, then set the format with DLAB up to reach the latches. */
    ns16550_wr(dev, NS16550_IER, 0);
    lcr = NS16550_LCR_8N1;
    ns16550_wr(dev, NS16550_LCR, lcr | NS16550_LCR_DLAB);
    ns16550_wr(dev, NS16550_DLL, div & 0xFFU);
    ns16550_wr(dev, NS16550_DLM, (div >> 8) & 0xFFU);
    ns16550_wr(dev, NS16550_LCR, lcr);

    /* Enable the FIFOs and drop anything a previous stage left. */
    ns16550_wr(dev, NS16550_FCR,
        NS16550_FCR_ENABLE | NS16550_FCR_RXRST | NS16550_FCR_TXRST);
    /* A receiver wired for hardware flow control needs DTR/RTS asserted. */
    ns16550_wr(dev, NS16550_MCR, NS16550_MCR_DTR | NS16550_MCR_RTS);

    return NS16550_OK;
}

/* Spin until every bit in `mask` is set in LSR, or the bound expires. */
static int ns16550_wait_lsr(const struct ns16550_dev* dev, uint32_t mask)
{
    uint32_t spin;

    for (spin = 0; spin < NS16550_SPIN_MAX; spin++) {
        if ((ns16550_rd(dev, NS16550_LSR) & mask) == mask) {
            return NS16550_OK;
        }
    }
    return NS16550_ERR_TMO;
}

int ns16550_write(struct ns16550_dev* dev, const char* buf, uint32_t len)
{
    uint32_t i;
    int ret;

    if (dev == NULL || dev->base == 0 || (buf == NULL && len != 0)) {
        return NS16550_ERR_ARG;
    }
    ns16550_fix_io_width(dev);

    for (i = 0; i < len; i++) {
        if (dev->crlf != 0 && buf[i] == '\n') {
            ret = ns16550_wait_lsr(dev, NS16550_LSR_THRE);
            if (ret != NS16550_OK) {
                return ret;
            }
            ns16550_wr(dev, NS16550_THR, (uint32_t)'\r');
        }
        ret = ns16550_wait_lsr(dev, NS16550_LSR_THRE);
        if (ret != NS16550_OK) {
            return ret;
        }
        ns16550_wr(dev, NS16550_THR, (uint32_t)(uint8_t)buf[i]);
    }
    /* Drain: the next image may reset the port. */
    return ns16550_wait_lsr(dev, NS16550_LSR_THRE | NS16550_LSR_TEMT);
}

int ns16550_can_read(struct ns16550_dev* dev)
{
    if (dev == NULL || dev->base == 0) {
        return 0;
    }
    ns16550_fix_io_width(dev);
    return (ns16550_rd(dev, NS16550_LSR) & NS16550_LSR_DR) ? 1 : 0;
}

int ns16550_read(struct ns16550_dev* dev, uint8_t* c)
{
    int ret;

    if (dev == NULL || dev->base == 0 || c == NULL) {
        return NS16550_ERR_ARG;
    }
    ns16550_fix_io_width(dev);
    ret = ns16550_wait_lsr(dev, NS16550_LSR_DR);
    if (ret != NS16550_OK) {
        return ret;
    }
    *c = (uint8_t)(ns16550_rd(dev, NS16550_RBR) & 0xFF);
    return NS16550_OK;
}
