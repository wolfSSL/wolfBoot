/* uart_drv_stm32.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32F4, using UART3.
 *
 * Pinout: RX=PD9, TX=PD8
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

#ifdef TARGET_stm32f4

#include <stdint.h>

/* Common UART Config */
#if !defined(USE_UART1) && !defined(USE_UART3)
#define USE_UART3
#endif
#define UART_PIN_AF 7
#define UART_CR1_UART_ENABLE    (1 << 13)
#define UART_CR1_SYMBOL_LEN     (1 << 12)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_SR_TX_EMPTY        (1 << 7)
#define UART_SR_RX_NOTEMPTY     (1 << 5)

#ifndef CLOCK_SPEED
#define CLOCK_SPEED (168000000)
#endif

/* Common GPIO Config */
#define GPIO_MODE_AF (2)

/* UART1 Config */
#ifdef USE_UART1
#define UART_RX_PIN 7
#define UART_TX_PIN 6

#define UART1 (0x40011000)
#define UART_SR       (*(volatile uint32_t *)(UART1))
#define UART_DR       (*(volatile uint32_t *)(UART1 + 0x04))
#define UART_BRR      (*(volatile uint32_t *)(UART1 + 0x08))
#define UART_CR1      (*(volatile uint32_t *)(UART1 + 0x0c))
#define UART_CR2      (*(volatile uint32_t *)(UART1 + 0x10))

#define UART_CLOCK_ER           (*(volatile uint32_t *)(0x40023844))
#define UART_CLOCK_ER_VAL 	(1 << 4)

#define GPIO_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIO_CLOCK_ER_VAL (1 << 1)
#define GPIOB_BASE 0x40020400
#define GPIO_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIO_AFL    (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIO_AFH    (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#endif

/* UART3 Config */
#ifdef USE_UART3
#define UART_RX_PIN 9
#define UART_TX_PIN 8

#define UART3 (0x40004800)
#define UART_SR       (*(volatile uint32_t *)(UART3))
#define UART_DR       (*(volatile uint32_t *)(UART3 + 0x04))
#define UART_BRR      (*(volatile uint32_t *)(UART3 + 0x08))
#define UART_CR1      (*(volatile uint32_t *)(UART3 + 0x0c))
#define UART_CR2      (*(volatile uint32_t *)(UART3 + 0x10))

#define UART_CLOCK_ER           (*(volatile uint32_t *)(0x40023840))
#define UART_CLOCK_ER_VAL 	    (1 << 18)

#define GPIO_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIO_CLOCK_ER_VAL (1 << 3)
#define GPIOD_BASE 0x40020c00
#define GPIO_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIO_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIO_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))
#endif


static void uart_pins_setup(void)
{
    uint32_t reg;
    GPIO_CLOCK_ER |= GPIO_CLOCK_ER_VAL;
    /* Set mode = AF */
    reg = GPIO_MODE & ~ (0x03 << (UART_RX_PIN * 2));
    GPIO_MODE = reg | (2 << (UART_RX_PIN * 2));
    reg = GPIO_MODE & ~ (0x03 << (UART_TX_PIN * 2));
    GPIO_MODE = reg | (2 << (UART_TX_PIN * 2));

    /* The alternate function register is split across two 32bit
     * registers (AFL, AFH). AFL covers pins 0 through 7, and 
     * AFH covers pins 8 through 15. The code below determines 
     * which register to use at compile time based on the chosen
     * pin number
    */

#if UART_TX_PIN > 7
    reg = GPIO_AFH & ~(0xf << ((UART_TX_PIN - 8) * 4));
    GPIO_AFH = reg | (UART_PIN_AF << ((UART_TX_PIN - 8) * 4));
#else
    reg = GPIO_AFL & ~(0xf << (UART_TX_PIN * 4));
    GPIO_AFL = reg | (UART_PIN_AF << (UART_TX_PIN * 4));
#endif

#if UART_RX_PIN > 7
    reg = GPIO_AFH & ~(0xf << ((UART_RX_PIN - 8) * 4));
    GPIO_AFH = reg | (UART_PIN_AF << ((UART_RX_PIN - 8) * 4));
#else
    reg = GPIO_AFL & ~(0xf << (UART_RX_PIN * 4));
    GPIO_AFL = reg | (UART_PIN_AF << (UART_RX_PIN * 4));
#endif

}

int uart_tx(const uint8_t c)
{
    uint32_t reg;
    do {
        reg = UART_SR;
    } while ((reg & UART_SR_TX_EMPTY) == 0);
    UART_DR = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    volatile uint32_t reg = UART_SR;
    if ((reg & UART_SR_RX_NOTEMPTY) != 0) {
        reg = UART_DR;
        *c = (uint8_t)(reg & 0xff);
        return 1;
    }
    return 0;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF7 */
    uart_pins_setup();
    /* Turn on the device */
    UART_CLOCK_ER |= UART_CLOCK_ER_VAL;
    UART_CR1 &= ~(UART_CR1_UART_ENABLE);

    /* Configure for TX + RX */
    UART_CR1 |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE);

    /* Configure clock */
    UART_BRR =  CLOCK_SPEED / bitrate;

    /* Configure data bits */
    if (data == 8)
        UART_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART_CR2 & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART_CR2 = reg & (2 << 12);
    else
        UART_CR2 = reg;

    /* Turn on uart */
    UART_CR1 |= UART_CR1_UART_ENABLE;
    return 0;
}

#endif /* TARGET_stm32f4 */
