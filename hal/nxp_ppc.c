/* nxp_ppc.c
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

/* This file gets directly included from nxp_ targets.
 * This file contains shared driver code for all NXP QorIQ platforms */

/* RAMFUNCTION is defined by image.h (included by targets that need RAM_CODE).
 * Provide an empty fallback for targets that do not use RAM_CODE (e.g. T1024/P1021). */
#ifndef RAMFUNCTION
#define RAMFUNCTION
#endif

/* ---- E5500/E6500 clock helpers and udelay ----
 * CLOCKING_PLLCNGSR and CLOCKING_PLLPGSR must be defined by the including
 * target before this file is reached (e.g. via nxp_t2080.h / nxp_t1024.c).
 * SYS_CLK must be the oscillator input frequency (e.g. 100 MHz). */
#if defined(CORE_E5500) || defined(CORE_E6500)
#ifdef ENABLE_BUS_CLK_CALC
static uint32_t hal_get_core_clk(void)
{
    /* compute core clock: system_input * (CGA_PLL1_RAT / 2) */
    uint32_t core_clk;
    uint32_t core_ratio = get32(CLOCKING_PLLCNGSR(0));
    core_ratio = ((core_ratio >> 1) & 0x3F);
    core_clk = SYS_CLK * core_ratio;
    return core_clk;
}
/* Non-static: prototyped in nxp_ppc.h so drivers built as standalone
 * objects (e.g. hal/nxp_esdhc.c) can use the clock helpers. */
uint32_t RAMFUNCTION hal_get_plat_clk(void)
{
    /* compute platform clock: system_input * (SYS_PLL_RAT / 2) */
    uint32_t plat_clk;
    uint32_t plat_ratio = get32(CLOCKING_PLLPGSR);
    plat_ratio = ((plat_ratio >> 1) & 0x1F);
    plat_clk = SYS_CLK * plat_ratio;
    return plat_clk;
}
uint32_t hal_get_bus_clk(void)
{
    return hal_get_plat_clk() / 2;
}
#endif /* ENABLE_BUS_CLK_CALC */

#define DELAY_US    (TIMEBASE_HZ / 1000000)
static void RAMFUNCTION udelay(uint32_t delay_us)
{
    wait_ticks((unsigned long long)delay_us * DELAY_US);
}
#endif /* CORE_E5500 || CORE_E6500 */

/* ---- PC16552D DUART console ----
 * Each target defines UART_BASE(n), UART_SEL and BAUD_RATE. The full loader
 * uses hal/uart/ns16550.c with get8()/set8() kept as the accessors via
 * NS16550_IO_H, so MMIO ordering is unchanged. stage1 keeps a specialised
 * copy: P1021 gives it 4KB of flash in total and the generic driver does
 * not fit. */
#ifdef DEBUG_UART
#ifndef BUILD_LOADER_STAGE1

#include "ns16550.h"

static struct ns16550_dev uart_console;

/* The bus clock is only known once the PLLs have been read. */
uint32_t ns16550_hal_clk_hz(void)
{
    return (uint32_t)hal_get_bus_clk();
}

void uart_init(void)
{
    /* Every field is set explicitly rather than memset first: this file is
     * included as source by four HALs and not all of them pull in string.h. */
    uart_console.base = (uintptr_t)UART_BASE(UART_SEL);
    uart_console.reg_shift = 0; /* byte-spaced registers */
    uart_console.reg_off = 0;
    uart_console.clk_hz = 0;    /* ask ns16550_hal_clk_hz() */
    uart_console.io_width = 1;
    uart_console.crlf = 1;      /* console duty */
    (void)ns16550_init(&uart_console, BAUD_RATE);
}

void uart_write(const char* buf, uint32_t sz)
{
    (void)ns16550_write(&uart_console, buf, sz);
}

#else /* BUILD_LOADER_STAGE1 */

/* Minimal driver for the size-constrained first stage. */
#define S1_UART(off)  ((volatile unsigned char*)(UART_BASE(UART_SEL) + (off)))
#define S1_THR        S1_UART(0)
#define S1_IER        S1_UART(1)
#define S1_FCR        S1_UART(2)
#define S1_LCR        S1_UART(3)
#define S1_LSR        S1_UART(5)
#define S1_DLL        S1_UART(0)
#define S1_DLM        S1_UART(1)

void uart_init(void)
{
    uint32_t div = (hal_get_bus_clk() + (8 * BAUD_RATE)) / (16 * BAUD_RATE);

    while (!(get8(S1_LSR) & 0x40))
        ;
    set8(S1_IER, 0);
    set8(S1_FCR, 0x07);          /* FIFO enable + RX/TX reset */
    set8(S1_LCR, 0x83);          /* DLAB | 8 data bits */
    set8(S1_DLL, (div & 0xff));
    set8(S1_DLM, ((div >> 8) & 0xff));
    set8(S1_LCR, 0x03);          /* 8 data bits, DLAB clear */
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') {
            while ((get8(S1_LSR) & 0x20) == 0);
            set8(S1_THR, '\r');
        }
        while ((get8(S1_LSR) & 0x20) == 0);
        set8(S1_THR, c);
    }
}

#endif /* !BUILD_LOADER_STAGE1 */
#endif /* DEBUG_UART */
