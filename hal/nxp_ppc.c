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
static uint32_t RAMFUNCTION hal_get_plat_clk(void)
{
    /* compute platform clock: system_input * (SYS_PLL_RAT / 2) */
    uint32_t plat_clk;
    uint32_t plat_ratio = get32(CLOCKING_PLLPGSR);
    plat_ratio = ((plat_ratio >> 1) & 0x1F);
    plat_clk = SYS_CLK * plat_ratio;
    return plat_clk;
}
static uint32_t hal_get_bus_clk(void)
{
    return hal_get_plat_clk() / 2;
}
#endif /* ENABLE_BUS_CLK_CALC */

#define TIMEBASE_CLK_DIV 16
#define TIMEBASE_HZ (hal_get_plat_clk() / TIMEBASE_CLK_DIV)
#define DELAY_US    (TIMEBASE_HZ / 1000000)
static void RAMFUNCTION udelay(uint32_t delay_us)
{
    wait_ticks((unsigned long long)delay_us * DELAY_US);
}
#endif /* CORE_E5500 || CORE_E6500 */

/* ---- Shared PC16552D-compatible DUART driver ----
 * Each target must define before including this file:
 *   UART_SEL, BAUD_RATE, UART_THR(n), UART_IER(n), UART_FCR(n),
 *   UART_LCR(n), UART_DLB(n), UART_DMB(n), UART_LSR(n),
 *   UART_FCR_TFR, UART_FCR_RFR, UART_FCR_FEN,
 *   UART_LCR_DLAB, UART_LCR_WLS, UART_LSR_TEMT, UART_LSR_THRE */
#ifdef DEBUG_UART
void uart_init(void)
{
    /* baud rate = bus_clk / (16 * div); round up */
    uint32_t div = (hal_get_bus_clk() + (8 * BAUD_RATE)) / (16 * BAUD_RATE);

    while (!(get8(UART_LSR(UART_SEL)) & UART_LSR_TEMT))
        ;

    set8(UART_IER(UART_SEL), 0);
    set8(UART_FCR(UART_SEL), (UART_FCR_TFR | UART_FCR_RFR | UART_FCR_FEN));

    /* enable baud rate access (DLAB=1) */
    set8(UART_LCR(UART_SEL), (UART_LCR_DLAB | UART_LCR_WLS));
    set8(UART_DLB(UART_SEL), (div & 0xff));
    set8(UART_DMB(UART_SEL), ((div >> 8) & 0xff));
    /* disable baud rate access (DLAB=0) */
    set8(UART_LCR(UART_SEL), (UART_LCR_WLS));
}

void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
            set8(UART_THR(UART_SEL), '\r');
        }
        while ((get8(UART_LSR(UART_SEL)) & UART_LSR_THRE) == 0);
        set8(UART_THR(UART_SEL), c);
    }
}
#endif /* DEBUG_UART */
