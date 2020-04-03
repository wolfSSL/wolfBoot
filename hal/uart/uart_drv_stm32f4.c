/* uart_drv_stm32.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32F4, using UART3.
 *
 * Pinout: RX=PD9, TX=PD8
 *
 * Copyright (C) 2020 wolfSSL Inc.
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

#include <stdint.h>

/* Driver hardcoded to work on UART3 (PD8/PD9) */
#define UART3 (0x40004800)
#define UART3_PIN_AF 7
#define UART3_RX_PIN 9
#define UART3_TX_PIN 8

#define UART3_SR       (*(volatile uint32_t *)(UART3))
#define UART3_DR       (*(volatile uint32_t *)(UART3 + 0x04))
#define UART3_BRR      (*(volatile uint32_t *)(UART3 + 0x08))
#define UART3_CR1      (*(volatile uint32_t *)(UART3 + 0x0c))
#define UART3_CR2      (*(volatile uint32_t *)(UART3 + 0x10))

#define UART_CR1_UART_ENABLE    (1 << 13)
#define UART_CR1_SYMBOL_LEN     (1 << 12)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_SR_TX_EMPTY        (1 << 7)
#define UART_SR_RX_NOTEMPTY     (1 << 5)


#define CLOCK_SPEED (168000000)

#define APB1_CLOCK_ER           (*(volatile uint32_t *)(0x40023840))
#define UART3_APB1_CLOCK_ER_VAL 	(1 << 18)

#define AHB1_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOD_AHB1_CLOCK_ER (1 << 3)
#define GPIOD_BASE 0x40020c00
#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))
#define GPIO_MODE_AF (2)

static void uart_pins_setup(void)
{
    uint32_t reg;
    AHB1_CLOCK_ER |= GPIOD_AHB1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOD_MODE & ~ (0x03 << (UART3_RX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART3_RX_PIN * 2));
    reg = GPIOD_MODE & ~ (0x03 << (UART3_TX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART3_TX_PIN * 2));

    /* Alternate function: use high pins (8 and 9) */
    reg = GPIOD_AFH & ~(0xf << ((UART3_TX_PIN - 8) * 4));
    GPIOD_AFH = reg | (UART3_PIN_AF << ((UART3_TX_PIN - 8) * 4));
    reg = GPIOD_AFH & ~(0xf << ((UART3_RX_PIN - 8) * 4));
    GPIOD_AFH = reg | (UART3_PIN_AF << ((UART3_RX_PIN - 8) * 4));
}

int uart_tx(const uint8_t c)
{
    uint32_t reg;
    do {
        reg = UART3_SR;
    } while ((reg & UART_SR_TX_EMPTY) == 0);
    UART3_DR = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    volatile uint32_t reg = UART3_SR;
    if ((reg & UART_SR_RX_NOTEMPTY) != 0) {
        reg = UART3_DR;
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
    APB1_CLOCK_ER |= UART3_APB1_CLOCK_ER_VAL;
    UART3_CR1 &= ~(UART_CR1_UART_ENABLE);

    /* Configure for TX + RX */
    UART3_CR1 |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE);

    /* Configure clock */
    UART3_BRR =  CLOCK_SPEED / bitrate;

    /* Configure data bits */
    if (data == 8)
        UART3_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART3_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART3_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART3_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART3_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART3_CR2 & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART3_CR2 = reg & (2 << 12);
    else
        UART3_CR2 = reg;

    /* Turn on uart */
    UART3_CR1 |= UART_CR1_UART_ENABLE;
    return 0;
}

