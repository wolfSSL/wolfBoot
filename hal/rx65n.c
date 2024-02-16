/* rx65n.c
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

/* HAL for Renesas RX65N */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "user_settings.h"

#include <target.h>
#include "hal.h"
#include "hal/renesas-rx.h"

#define PCLK (96000000) /* 96MHz */

/* Register Write Protection Function */
#define PRCR       (*(volatile uint16_t *)(0x803FE))
#define PRCR_PRKEY (0xA500)
#define PRCR_PRC0  ENDIAN_BIT(0) /* Enables writing to the registers related to the clock generation circuit */
#define PRCR_PRC1  ENDIAN_BIT(1) /* Enables writing to the registers related to operating modes, clock R/W generation circuit, low power consumption, and software reset */
#define PRCR_PRC3  ENDIAN_BIT(3) /* Enables writing to the registers related to the LVD */

/* Serial Communication Interface */
#define SCI_BASE(n) (0x8A000 + ((n) * 0x20))
#define SCI_SMR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x00))
#define SCI_BRR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x01)) /* Bit Rate Reg < 255 */
#define SCI_SCR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x02))
#define SCI_TDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x03)) /* Transmit Data Register */
#define SCI_SSR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x04))
#define SCI_RDR(n)  (*(volatile uint8_t *)(SCI_BASE(n) + 0x05)) /* Receive Data Register */
#define SCI_SCMR(n) (*(volatile uint8_t *)(SCI_BASE(n) + 0x06))

#define SCI_SMR_CKS(clk) (clk & 0x3) /* 0=PCLK, 1=PCLK/4, 2=PCLK/16, 3=PCLK/64 */
#define SCI_SMR_STOP  ENDIAN_BIT(3) /* 0=1 stop bit */
#define SCI_SMR_CHR   ENDIAN_BIT(6) /* 0=8-bit */
#define SCI_SCMR_CHR1 ENDIAN_BIT(4) /* 1=8-bit */
#define SCI_SCR_RE    ENDIAN_BIT(4)
#define SCI_SCR_TE    ENDIAN_BIT(5)
#define SCI_SSR_TEND  ENDIAN_BIT(2) /* Transmit End Flag */
#define SCI_SSR_RDRF  ENDIAN_BIT(6) /* Receive Data Full Flag */
#define SCI_SSR_TDRE  ENDIAN_BIT(7) /* Transmit Data Empty Flag */

/* MPC (Multi-Function Pin Controller) */
#define MPC_PWPR   (*(volatile uint8_t *)(0x8C11F))
#define MPC_PWPR_B0WI  ENDIAN_BIT(7)
#define MPC_PWPR_PFSWE ENDIAN_BIT(6)

#define MPC_PFS(n) (*(volatile uint8_t *)(0x8C0E0 + (n)))

/* Ports */
#define PORT_BASE(n)  (0x8C000 + (n))
#define PORT_PDR(n)   (*(volatile uint8_t*)(0x8C000 + (n)))
#define PORT_PMR(n)   (*(volatile uint8_t*)(0x8C060 + (n))) /* 0=General, 1=Peripheral */

#ifdef DEBUG_UART

#ifndef DEBUG_UART_SCI
#define DEBUG_UART_SCI 5
#endif
#ifndef DEBUG_BAUD_RATE
#define DEBUG_BAUD_RATE 115200
#endif

void uart_init(void)
{
    /* Disable MPC Write Protect for PFS */
    MPC_PWPR &= ~MPC_PWPR_B0WI;
    MPC_PWPR |= MPC_PWPR_PFSWE;

    /* Configure PC3 for UART (TXD5) and PC2 UART (RXD5) */
    MPC_PFS(0xC2) = 0xA; /* RXD5 */
    MPC_PFS(0xC3) = 0xA; /* TXD5 */
    PORT_PMR(0xC) |= (ENDIAN_BIT(2) | ENDIAN_BIT(3)); /* Enable TXD5/RXD5 */

    /* Enable MPC Write Protect for PFS */
    MPC_PWPR &= ~(MPC_PWPR_PFSWE | MPC_PWPR_B0WI);
    MPC_PWPR |= MPC_PWPR_PFSWE;

    /* 8-bit, 1-stop, no parity, cks=3 (/64) */
    SCI_SMR(DEBUG_UART_SCI) = SCI_SMR_CKS(3);
    /* baud rate */
    SCI_BRR(DEBUG_UART_SCI) = PCLK / 64 / DEBUG_BAUD_RATE;
}
void uart_write(const char* buf, uint32_t sz)
{
    uint32_t pos = 0;
    SCI_SCR(DEBUG_UART_SCI) |= SCI_SCR_TE;
    while (sz-- > 0) {
        char c = buf[pos++];
        if (c == '\n') { /* handle CRLF */
            while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
            SCI_TDR(DEBUG_UART_SCI) = '\r';
        }
        while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
        SCI_TDR(DEBUG_UART_SCI) = c;
    }
    while ((SCI_SSR(DEBUG_UART_SCI) & SCI_SSR_TEND) == 0);
    SCI_SCR(DEBUG_UART_SCI) &= ~SCI_SCR_TE;
}
#endif /* DEBUG_UART */

/* HAL Stubs */
void hal_init(void)
{
#ifdef DEBUG_UART
    uart_init();
    uart_write("wolfBoot HAL Init\n", 18);
#endif
    return;
}
int hal_flash_write(uint32_t address, const uint8_t *data, int len)
{
    (void)address;
    (void)data;
    (void)len;
    return 0;
}
int hal_flash_erase(uint32_t address, int len)
{
    (void)address;
    (void)len;
    return 0;
}
void hal_flash_unlock(void)
{
    return;
}
void hal_flash_lock(void)
{
    return;
}
void hal_prepare_boot(void)
{
    return;
}
