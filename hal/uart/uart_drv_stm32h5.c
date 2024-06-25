/* uart_drv_stm32l5.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32L5 Nucleo
 * using LPUART1 (VCS port through USB).
 *
 *
 * Copyright (C) 2023 wolfSSL Inc.
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
#include "hal/stm32h5.h"

/* USE_UART1
 * Set to 0 for VCP over USB
 * Set to 1 for Arduino D0, D1 pins on nucleo
 * */
#define USE_UART1 0

#define RCC_AHB2ENR1_CLOCK_ER (*(volatile uint32_t *)(RCC_BASE + 0x8C ))
#define GPIOB_AHB2ENR1_CLOCK_ER (1 << 1)
#define GPIOD_AHB2ENR1_CLOCK_ER (1 << 3)


#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPE (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_ODR   (*(volatile uint32_t *)(GPIOB_BASE + 0x14))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OTYPE (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_OSPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_PUPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x0c))
#define GPIOD_ODR   (*(volatile uint32_t *)(GPIOD_BASE + 0x14))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))
#define GPIOD_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))

#define CLOCK_FREQ (64000000)

static void uart1_pins_setup(void)
{
    uint32_t reg;
    RCC_AHB2ENR1_CLOCK_ER|= GPIOB_AHB2ENR1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOB_MODE & ~ (0x03 << (UART1_RX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOB_MODE & ~ (0x03 << (UART1_TX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_TX_PIN * 2));

    /* Alternate function: use low pins (6 and 7) */
    reg = GPIOB_AFL & ~(0xf << (UART1_TX_PIN * 4));
    GPIOB_AFL = reg | (UART1_PIN_AF << (UART1_TX_PIN * 4));
    reg = GPIOB_AFL & ~(0xf << ((UART1_RX_PIN) * 4));
    GPIOB_AFH = reg | (UART1_PIN_AF << ((UART1_RX_PIN) * 4));

}

static void uart3_pins_setup(void)
{
    uint32_t reg;
    RCC_AHB2ENR1_CLOCK_ER|= GPIOD_AHB2ENR1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOD_MODE & ~ (0x03 << (UART3_RX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART3_RX_PIN * 2));
    reg = GPIOD_MODE & ~ (0x03 << (UART3_TX_PIN * 2));
    GPIOD_MODE = reg | (2 << (UART3_TX_PIN * 2));

    /* Alternate function: use hi pins (8 and 9) */
    reg = GPIOD_AFH & ~(0xf << ((UART3_TX_PIN - 8) * 4));
    GPIOD_AFH = reg | (UART3_PIN_AF << ((UART3_TX_PIN - 8) * 4));
    reg = GPIOD_AFH & ~(0xf << ((UART3_RX_PIN - 8) * 4));
    GPIOD_AFH = reg | (UART3_PIN_AF << ((UART3_RX_PIN - 8) * 4));

}

static int uart1_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF */
    uart1_pins_setup();

    reg = RCC_CCIPR3 & (~ (RCC_CCIPR3_LPUART1SEL_MASK << RCC_CCIPR3_LPUART1SEL_SHIFT));
    RCC_CCIPR3 = reg | (0 << RCC_CCIPR3_LPUART1SEL_SHIFT); /* PLL2 */

    /* Configure clock */
    UART1_BRR |= (uint16_t)(CLOCK_FREQ / bitrate) + 1;

    /* Configure data bits */
    if (data == 8)
        UART1_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART1_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART1_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART1_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART1_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART1_CR2 & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART1_CR2 = reg & (2 << 12);
    else
        UART1_CR2 = reg;

    /* Prescaler to DIV1 */
    UART1_PRE |= 2;

    /* Configure for RX+TX, turn on. */
    UART1_CR1 |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
}

static void uart1_clear_errors(void)
{
    UART1_ICR = UART1_ISR & (UART_ENE | UART_EPE | UART_ORE | UART_EFE);
}

static int uart1_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART1_ISR;
        if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
            uart1_clear_errors();
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART1_TDR = c;
    return 1;
}

static int uart1_rx(uint8_t *c)
{
    volatile uint32_t reg;
    int i = 0;
    reg = UART1_ISR;
    if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
        uart1_clear_errors();
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART1_RDR;
        return 1;
    }
    return 0;
}

static int uart3_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF */
    uart3_pins_setup();

    reg = RCC_CCIPR1 & (~ (RCC_CCIPR1_USART3SEL_MASK << RCC_CCIPR1_USART3SEL_SHIFT));
    RCC_CCIPR1 = reg | (0 << RCC_CCIPR1_USART3SEL_SHIFT); /* PLL2 */

    /* Configure clock */
    UART3_BRR = (uint16_t)(CLOCK_FREQ / bitrate) + 1;

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

    /* Prescaler to DIV1 */
    UART3_PRE |= 2;

    /* Configure for RX+TX, turn on. */
    UART3_CR1 |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
}

static void uart3_clear_errors(void)
{
    UART3_ICR = UART3_ISR & (UART_ENE | UART_EPE | UART_ORE | UART_EFE);
}

static int uart3_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART3_ISR;
        if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
            uart3_clear_errors();
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART3_TDR = c;
    return 1;
}

static int uart3_rx(uint8_t *c)
{
    volatile uint32_t reg;
    int i = 0;
    reg = UART3_ISR;
    if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
        uart3_clear_errors();
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART3_RDR;
        return 1;
    }
    return 0;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
#if USE_UART1
    return uart1_init(bitrate, data, parity, stop);
#else
    return uart3_init(bitrate, data, parity, stop);
#endif
}

int uart_tx(const uint8_t c)
{
#if USE_UART1
    return uart1_tx(c);
#else
    return uart3_tx(c);
#endif
}

int uart_rx(uint8_t *c)
{
#if USE_UART1
    return uart1_rx(c);
#else
    return uart3_rx(c);
#endif
}


