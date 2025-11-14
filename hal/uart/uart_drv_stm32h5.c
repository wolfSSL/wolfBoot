/* uart_drv_stm32l5.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32L5 Nucleo
 * using LPUART1 (VCS port through USB).
 *
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#ifdef TARGET_stm32h5

#include <stdint.h>
#include "hal/stm32h5.h"

#if defined(USE_UART1) && USE_UART == 1
static void uart_pins_setup(void)
{
    uint32_t reg;
    RCC_AHB2ENR_CLOCK_ER |= GPIOB_AHB2ENR1_CLOCK_ER;
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
#else
static void uart_pins_setup(void)
{
    uint32_t reg;
    RCC_AHB2ENR_CLOCK_ER |= GPIOD_AHB2ENR1_CLOCK_ER;
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
#endif

static int uart_base_init(uint32_t base, uint32_t bitrate, uint8_t data,
    char parity, uint8_t stop)
{
    uint32_t reg;

    /* Configure clock */
    UART_BRR(base) = (uint16_t)(PERIPH_CLOCK_FREQ / bitrate) + 1;

    /* Configure data bits */
    if (data == 8)
        UART_CR1(base) &= ~UART_CR1_SYMBOL_LEN;
    else
        UART_CR1(base) |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART_CR1(base) |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART_CR1(base) |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART_CR1(base) &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART_CR2(base) & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART_CR2(base) = reg & (2 << 12);
    else
        UART_CR2(base) = reg;

    /* Prescaler to DIV1 */
    UART_PRE(base) |= 2;

    /* Configure for RX+TX, turn on. */
    UART_CR1(base) |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
}

static void uart_clear_errors(uint32_t base)
{
    UART_ICR(base) = UART_ISR(base) & (UART_ENE | UART_EPE | UART_ORE | UART_EFE);
}

int uart_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART_ISR(USE_UART);
        if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
        uart_clear_errors(USE_UART);
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART_TDR(USE_UART) = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    volatile uint32_t reg;
    int i = 0;
    reg = UART_ISR(USE_UART);
    if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
        uart_clear_errors(USE_UART);
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART_RDR(USE_UART);
        return 1;
    }
    return 0;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    uart_pins_setup();
#if defined(USE_UART1) && USE_UART == 1
    reg = RCC_CCIPR3 & (~ (RCC_CCIPR3_LPUART1SEL_MASK << RCC_CCIPR3_LPUART1SEL_SHIFT));
    RCC_CCIPR3 = reg | (0 << RCC_CCIPR3_LPUART1SEL_SHIFT); /* PLL2 */
#else
    reg = RCC_CCIPR1 & (~ (RCC_CCIPR1_USART3SEL_MASK << RCC_CCIPR1_USART3SEL_SHIFT));
    RCC_CCIPR1 = reg | (0 << RCC_CCIPR1_USART3SEL_SHIFT); /* PLL2 */
#endif
    return uart_base_init(USE_UART, bitrate, data, parity, stop);
}

#ifdef DEBUG_UART
void uart_write(const char *buf, unsigned int len)
{
    while (len--) {
        uart_tx(*buf);
        buf++;
    }
}
#endif

#endif /* TARGET_stm32h5 */
