/* uart_drv_samr21.c
 *
 * Driver for the SERCOM USART on SAM D21 / SAM R21
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

#ifdef TARGET_samr21

#include <stdint.h>

#define CPU_FREQ (48000000)

/* SERCOM instance, pins and pad routing. The defaults are the SAM D21
 * Xplained Pro EDBG virtual COM port: SERCOM3 with TX on PA22 (pad 0) and RX
 * on PA23 (pad 1), both peripheral function C. A board that wires its console
 * elsewhere overrides these; UART_TXPO and UART_RXPO are the pad selections
 * from the CTRLA field tables, not pin numbers. */
#ifndef UART_SERCOM
#define UART_SERCOM 3
#endif
#ifndef UART_PORT_GROUP
#define UART_PORT_GROUP 0
#endif
#ifndef UART_TX_PIN
#define UART_TX_PIN 22
#endif
#ifndef UART_RX_PIN
#define UART_RX_PIN 23
#endif
#ifndef UART_PIN_MUX
#define UART_PIN_MUX 2
#endif
#ifndef UART_TXPO
#define UART_TXPO 0
#endif
#ifndef UART_RXPO
#define UART_RXPO 1
#endif

/* The generic clock generator feeding the SERCOM core. hal_init() sets
 * generator 0 to the 48MHz DPLL output. */
#ifndef UART_GCLK_GEN
#define UART_GCLK_GEN 0
#endif

#define PM_BASE                (0x40000400)
#define PM_APBCMASK            *((volatile uint32_t *)(PM_BASE + 0x20))
#define PM_APBCMASK_SERCOM(n)  (1UL << (2 + (n)))

#define GCLK_BASE              (0x40000C00)
#define GCLK_STATUS            *((volatile uint8_t  *)(GCLK_BASE + 0x01))
#define GCLK_CLKCTRL           *((volatile uint16_t *)(GCLK_BASE + 0x02))
#define GCLK_STATUS_SYNCBUSY   (1 << 7)
#define GCLK_CLKCTRL_CLKEN     (1 << 14)
#define GCLK_CLKCTRL_GEN_SHIFT (8)
/* GCLK_CLKCTRL.ID of SERCOMn_CORE: SERCOM0_CORE is 0x14 and the
 * instances are consecutive. */
#define GCLK_ID_SERCOM_CORE(n) (0x14 + (n))

#define PORT_BASE              (0x41004400)
#define PORT_GROUP(g)          (PORT_BASE + ((g) * 0x80))
#define PORT_PINCFG(g, p)      *((volatile uint8_t *)(PORT_GROUP(g) + 0x40 + (p)))
#define PORT_PMUX(g, p)        *((volatile uint8_t *)(PORT_GROUP(g) + 0x30 + ((p) / 2)))
#define PINCFG_PMUXEN          (1 << 0)

#define SERCOM_BASE            (0x42000800 + (UART_SERCOM * 0x400))
#define SERCOM_CTRLA           *((volatile uint32_t *)(SERCOM_BASE + 0x00))
#define SERCOM_CTRLB           *((volatile uint32_t *)(SERCOM_BASE + 0x04))
#define SERCOM_BAUD            *((volatile uint16_t *)(SERCOM_BASE + 0x0C))
#define SERCOM_INTFLAG         *((volatile uint8_t  *)(SERCOM_BASE + 0x18))
#define SERCOM_SYNCBUSY        *((volatile uint32_t *)(SERCOM_BASE + 0x1C))
#define SERCOM_DATA            *((volatile uint16_t *)(SERCOM_BASE + 0x28))

#define CTRLA_SWRST            (1UL << 0)
#define CTRLA_ENABLE           (1UL << 1)
#define CTRLA_MODE_USART_INT   (1UL << 2)   /* USART with internal clock */
#define CTRLA_FORM_PARITY      (1UL << 24)
#define CTRLA_DORD_LSB         (1UL << 30)
#define CTRLA_TXPO_SHIFT       (16)
#define CTRLA_RXPO_SHIFT       (20)

#define CTRLB_CHSIZE_MASK      (0x07UL)
#define CTRLB_SBMODE_2         (1UL << 6)
#define CTRLB_PMODE_ODD        (1UL << 13)
#define CTRLB_TXEN             (1UL << 16)
#define CTRLB_RXEN             (1UL << 17)

#define SYNCBUSY_SWRST         (1UL << 0)
#define SYNCBUSY_ENABLE        (1UL << 1)
#define SYNCBUSY_CTRLB         (1UL << 2)

#define INTFLAG_DRE            (1 << 0)
#define INTFLAG_RXC            (1 << 2)

#define GCLK_WAITBUSY() { while (GCLK_STATUS & GCLK_STATUS_SYNCBUSY) {} }

static void uart_pins_setup(void)
{
    uint8_t reg;

    PORT_PINCFG(UART_PORT_GROUP, UART_TX_PIN) |= PINCFG_PMUXEN;
    PORT_PINCFG(UART_PORT_GROUP, UART_RX_PIN) |= PINCFG_PMUXEN;

    /* One PMUX register per pin pair: the even pin takes the low nibble and
     * the odd pin the high one. */
    reg = PORT_PMUX(UART_PORT_GROUP, UART_TX_PIN);
    if ((UART_TX_PIN & 1) == 0)
        reg = (reg & 0xF0) | UART_PIN_MUX;
    else
        reg = (reg & 0x0F) | (UART_PIN_MUX << 4);
    PORT_PMUX(UART_PORT_GROUP, UART_TX_PIN) = reg;

    reg = PORT_PMUX(UART_PORT_GROUP, UART_RX_PIN);
    if ((UART_RX_PIN & 1) == 0)
        reg = (reg & 0xF0) | UART_PIN_MUX;
    else
        reg = (reg & 0x0F) | (UART_PIN_MUX << 4);
    PORT_PMUX(UART_PORT_GROUP, UART_RX_PIN) = reg;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;

    /* Turn on the APB clock, then give the SERCOM core a generic clock.
     * hal_init() leaves every peripheral channel pointed at generator 7,
     * which it never configures, so the channel has to be set here. */
    PM_APBCMASK |= PM_APBCMASK_SERCOM(UART_SERCOM);
    GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |
        (UART_GCLK_GEN << GCLK_CLKCTRL_GEN_SHIFT) |
        GCLK_ID_SERCOM_CORE(UART_SERCOM);
    GCLK_WAITBUSY();

    uart_pins_setup();

    /* Reset the peripheral so the register state is known */
    SERCOM_CTRLA = CTRLA_SWRST;
    while ((SERCOM_SYNCBUSY & SYNCBUSY_SWRST) ||
           (SERCOM_CTRLA & CTRLA_SWRST))
        ;

    /* 16x oversampling in arithmetic mode:
     *   BAUD = 65536 * (1 - 16 * bitrate / CPU_FREQ)
     * Scaling numerator and denominator by 1024 keeps the intermediate
     * inside 32 bits, which matters on a core with no 64-bit divide. */
    SERCOM_BAUD = (uint16_t)(65536U -
            ((bitrate * 1024U) / (CPU_FREQ / 1024U)));

    reg = CTRLA_MODE_USART_INT | CTRLA_DORD_LSB |
        ((uint32_t)UART_TXPO << CTRLA_TXPO_SHIFT) |
        ((uint32_t)UART_RXPO << CTRLA_RXPO_SHIFT);
    if ((parity == 'O') || (parity == 'E'))
        reg |= CTRLA_FORM_PARITY;
    SERCOM_CTRLA = reg;

    reg = CTRLB_TXEN | CTRLB_RXEN;
    /* CHSIZE encodes 8 bits as 0 and 9 as 1; 5 to 7 are their own values */
    if (data < 8)
        reg |= (data & CTRLB_CHSIZE_MASK);
    else if (data == 9)
        reg |= 1;
    if (parity == 'O')
        reg |= CTRLB_PMODE_ODD;
    if (stop > 1)
        reg |= CTRLB_SBMODE_2;
    SERCOM_CTRLB = reg;
    while (SERCOM_SYNCBUSY & SYNCBUSY_CTRLB)
        ;

    SERCOM_CTRLA |= CTRLA_ENABLE;
    while (SERCOM_SYNCBUSY & SYNCBUSY_ENABLE)
        ;

    return 0;
}

int uart_tx(const uint8_t c)
{
    while ((SERCOM_INTFLAG & INTFLAG_DRE) == 0)
        ;
    SERCOM_DATA = c;
    return 1;
}

int uart_rx(uint8_t *c, int len)
{
    (void)len;
    if (SERCOM_INTFLAG & INTFLAG_RXC) {
        *c = (uint8_t)SERCOM_DATA;
        return 1;
    }
    return 0;
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    while (len--) {
        uart_tx((const uint8_t)*buf);
        buf++;
    }
}
#endif

#endif /* TARGET_samr21 */
