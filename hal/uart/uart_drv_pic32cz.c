/* uart_drv_pic32cz.c
 *
 * Driver for the PIC32CZ SERCOM UART, used for wolfBoot debug output.
 *
 * Uses SERCOM1 on the host (Cortex-M7) core: TX on PC4 (EXT1 pin 14),
 * RX on PC7. 115200 8N1, transmit only.
 *
 * Derived from the reference driver in the wolfHSM PIC32CZ port
 * (czhsm-client/uart.c), which is in turn based on the plib driver generated
 * by MPLAB.
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

#ifdef TARGET_pic32cz

#include <stdint.h>
#include <stddef.h>

#define GCLK_BASE   (0x44050000U)
#define SERCOM_BASE (0x46000000U)
#define PORT_BASE   (0x44840000U)

/* SERCOM1 */
#define SERCOM_OFF     (0x2000U)
#define SERCOM1        (SERCOM_BASE + SERCOM_OFF)
#define SERCOM_PCHCTRL (22) /* GCLK peripheral channel for SERCOM1 */

#define SERCOM_CTRLA   (*(volatile uint32_t*)(SERCOM1 + 0x00U))
#define SERCOM_CTRLB   (*(volatile uint32_t*)(SERCOM1 + 0x04U))
#define SERCOM_BAUD    (*(volatile uint16_t*)(SERCOM1 + 0x0CU))
#define SERCOM_INTFLAG (*(volatile uint8_t*)(SERCOM1 + 0x18U))
#define SERCOM_SYNCBUSY (*(volatile uint32_t*)(SERCOM1 + 0x1CU))
#define SERCOM_DATA    (*(volatile uint32_t*)(SERCOM1 + 0x28U))

#define CTRLA_MODE_USART_INT (0x1U << 2)  /* USART with internal clock */
#define CTRLA_ENABLE         (0x1U << 1)
#define CTRLA_IBON           (0x1U << 8)
#define CTRLA_DORD           (0x1U << 30) /* LSB first */

#define CTRLB_CHSIZE_8BIT    (0x0U << 0)
#define CTRLB_SBMODE_1BIT    (0x0U << 6)
#define CTRLB_TXEN           (0x1U << 16)

#define INTFLAG_DRE          (0x1U << 0)  /* Data Register Empty */

/* GCLK: generator 1 sources SERCOM1 */
#define GCLK_GENCTRL1 (*(volatile uint32_t*)(GCLK_BASE + 0x20U + (1 * 4)))
#define GCLK_SYNCBUSY (*(volatile uint32_t*)(GCLK_BASE + 0x04U))
#define GCLK_PCHCTRL(n) (*(volatile uint32_t*)(GCLK_BASE + 0x80U + ((n) * 4)))

#define GCLK_GENCTRL_SRC_PLL0 (6U << 0)
#define GCLK_GENCTRL_DIV(x)   (((uint32_t)(x) & 0xFFFFU) << 16)
#define GCLK_GENCTRL_GENEN    (0x1U << 8)
#define GCLK_SYNCBUSY_GENCTRL1 (0x1U << 3)
#define GCLK_PCHCTRL_GEN1     (0x1U << 0)
#define GCLK_PCHCTRL_CHEN     (0x1U << 6)

/* PORT group 2 == port C */
#define PORT_GROUP_SIZE (0x80U)
#define PORTC           (PORT_BASE + (2 * PORT_GROUP_SIZE))
#define PORTC_PMUX(n)   (*(volatile uint8_t*)(PORTC + 0x30U + (n)))
#define PORTC_PINCFG(n) (*(volatile uint8_t*)(PORTC + 0x40U + (n)))

#define PINCFG_PMUXEN   (0x1U)

/* Baud register value for 115200 with GCLK1 = PLL0/2 (150 MHz), 16x sampling.
 *
 * hal_init() programs PLL0 to 300 MHz before uart_init() runs, and GCLK
 * generator 1 below divides it by 2, so the source clock is fixed regardless of
 * what the caller passes. wolfBoot only ever asks for 115200, so rather than
 * divide at runtime we use the same constant the reference driver does. */
#define BAUD_115200 (64730U)

static void pins_setup(void)
{
    /* PC4 = SERCOM1 TX, PC7 = SERCOM1 RX; both on peripheral function D. */
    PORTC_PINCFG(4) = PINCFG_PMUXEN;
    PORTC_PINCFG(7) = PINCFG_PMUXEN;
    PORTC_PMUX(2)   = 0x3U;  /* PC4 -> even nibble */
    PORTC_PMUX(3)   = 0x30U; /* PC7 -> odd nibble */
}

static void clock_setup(void)
{
    GCLK_GENCTRL1 =
        GCLK_GENCTRL_DIV(2U) | GCLK_GENCTRL_SRC_PLL0 | GCLK_GENCTRL_GENEN;
    while ((GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL1) != 0U) {
        /* wait for generator 1 to synchronise */
    }

    GCLK_PCHCTRL(SERCOM_PCHCTRL) = GCLK_PCHCTRL_GEN1 | GCLK_PCHCTRL_CHEN;
    while ((GCLK_PCHCTRL(SERCOM_PCHCTRL) & GCLK_PCHCTRL_CHEN) == 0U) {
        /* wait for the peripheral channel to be enabled */
    }
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    /* Fixed 115200 8N1: see BAUD_115200. */
    (void)bitrate;
    (void)data;
    (void)parity;
    (void)stop;

    pins_setup();
    clock_setup();

    SERCOM_CTRLA = CTRLA_MODE_USART_INT | CTRLA_DORD | CTRLA_IBON;
    SERCOM_BAUD  = (uint16_t)BAUD_115200;

    /* Transmit only: nothing on this port ever reads from the console. */
    SERCOM_CTRLB = CTRLB_CHSIZE_8BIT | CTRLB_SBMODE_1BIT | CTRLB_TXEN;
    while (SERCOM_SYNCBUSY != 0U) {
        /* wait */
    }

    SERCOM_CTRLA |= CTRLA_ENABLE;
    while (SERCOM_SYNCBUSY != 0U) {
        /* wait */
    }

    return 0;
}

int uart_tx(const uint8_t c)
{
    while ((SERCOM_INTFLAG & INTFLAG_DRE) == 0U) {
        /* wait for the data register to drain */
    }
    SERCOM_DATA = c;
    return 1;
}

int uart_rx(uint8_t* c)
{
    /* RX is not enabled on this port: CTRLB sets TXEN only. */
    (void)c;
    return 0;
}

#ifdef DEBUG_UART
void uart_write(const char* buf, unsigned int len)
{
    while (len--) {
        uart_tx((uint8_t)*buf);
        buf++;
    }
}
#endif

#endif /* TARGET_pic32cz */
