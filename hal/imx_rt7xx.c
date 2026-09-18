/* imx_rt7xx — wolfBoot HAL for the NXP i.MX RT700 (MIMXRT798S, Cortex-M33).
 *
 * Copyright (C) 2026 wolfSSL Inc.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include "image.h"
#include "hal.h"
#include "printf.h"
#include "imx_rt7xx.h"

/* The BootROM configures the compute-domain clocks and the XSPI0 XIP window
 * from the flash config block before handing control to this image, so no
 * clock or XSPI bring-up is required here for the initial boot path. Register
 * level XSPI0 program/erase for firmware update lands in a later slice; until
 * then the write side reports failure rather than faking success. */

#define RT7XX_REG(a) (*(volatile uint32_t *)(a))

#ifdef DEBUG_UART

#define CLKCTL0_PSCCTL1_SET  RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x44u)
#define CLKCTL0_FCCLK0SEL    RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x800u)
#define CLKCTL0_FCCLK0DIV    RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x804u)
#define CLKCTL0_FC0FCLKSEL   RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x808u)
#define RSTCTL0_PRSTCTL0_CLR RT7XX_REG(IMX_RT7XX_RSTCTL0_NS + 0x70u)
#define RSTCTL0_PRSTCTL2_CLR RT7XX_REG(IMX_RT7XX_RSTCTL0_NS + 0x78u)
#define CLKCTL0_PSCCTL5_SET  RT7XX_REG(IMX_RT7XX_CLKCTL0_NS + 0x54u)
#define IOPCTL0_PIO0_31      RT7XX_REG(IMX_RT7XX_IOPCTL0_NS + 0x7Cu)
#define IOPCTL0_PIO1_0       RT7XX_REG(IMX_RT7XX_IOPCTL0_NS + 0x80u)
#define LPFC0_PSELID         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0xFF8u)
#define LPUART0_BAUD         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x10u)
#define LPUART0_STAT         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x14u)
#define LPUART0_CTRL         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x18u)
#define LPUART0_DATA         RT7XX_REG(IMX_RT7XX_LPFC0_NS + 0x1Cu)

#define FCCLK0_DIV_REQFLAG   0x80000000u
#define FCCLK0_DIV_RESET     0x20000000u
#define LPFC0_CLK_RST_BIT    (1u << 30)
#define LPUART_STAT_TDRE     0x00800000u
#define LPUART_CTRL_TE_RE    0x000C0000u

/* FCCLK0 source is the compute base clock = FRO1 div1 after the BootROM. */
#define UART_FCCLK_HZ        192000000u
#ifndef UART_BAUD
#define UART_BAUD            115200u
#endif

void uart_init(void)
{
    uint32_t sbr;

    /* The BootROM leaves FCCLK0 halted and LP_FLEXCOMM0 gated + in reset */
    CLKCTL0_FCCLK0SEL = 0x4u;
    CLKCTL0_FCCLK0DIV |= FCCLK0_DIV_RESET;
    CLKCTL0_FCCLK0DIV = 0u;
    while ((CLKCTL0_FCCLK0DIV & FCCLK0_DIV_REQFLAG) != 0u) {
    }
    CLKCTL0_FC0FCLKSEL = 0x4u; /* FLEXCOMM0 functional clock = FCCLK0 */
    CLKCTL0_PSCCTL1_SET = LPFC0_CLK_RST_BIT;
    RSTCTL0_PRSTCTL2_CLR = LPFC0_CLK_RST_BIT;

    /* The BootROM also leaves IOPCTL0 in reset: release it before muxing */
    CLKCTL0_PSCCTL5_SET = (1u << 3);
    RSTCTL0_PRSTCTL0_CLR = (1u << 6);

    /* EVK MCU-LINK console: PIO1_0 = FC0 TX at 33 ohm drive (the VCOM net
     * drops 115200 edges at the default 100 ohm), PIO0_31 = FC0 RX */
    IOPCTL0_PIO1_0 = 0x3001u;
    IOPCTL0_PIO0_31 = 0x41u;

    LPFC0_PSELID = 0x1u; /* LP_FLEXCOMM0 function select: LPUART */

    sbr = UART_FCCLK_HZ / (16u * UART_BAUD);
    LPUART0_CTRL = 0u;
    LPUART0_BAUD = (15u << 24) | (sbr & 0x1FFFu);
    LPUART0_CTRL = LPUART_CTRL_TE_RE;
}

void uart_write(const char* buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        while ((LPUART0_STAT & LPUART_STAT_TDRE) == 0u) {
        }
        LPUART0_DATA = (uint32_t)(uint8_t)buf[i];
    }
}
#endif /* DEBUG_UART */

void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    wolfBoot_printf("wolfBoot HAL init: MIMXRT798S\n");
#endif
}

void RAMFUNCTION hal_flash_unlock(void)
{
}

void RAMFUNCTION hal_flash_lock(void)
{
}

int RAMFUNCTION hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return -1;
}

int RAMFUNCTION hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return -1;
}

int hal_flash_protect(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}

void hal_prepare_boot(void)
{
}
