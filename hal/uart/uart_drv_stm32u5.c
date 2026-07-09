/* uart_drv_stm32u5.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32U5
 * using USART1 (PA9/PA10, ST-LINK VCP on B-U585I-IOT02A).
 *
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

#ifdef TARGET_stm32u5

#include <stdint.h>
#include "hal/stm32u5.h"

static void uart_pins_setup(void)
{
    uint32_t reg;
    RCC_AHB2ENR1_CLOCK_ER |= GPIOA_AHB2ENR1_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOA_MODE & ~(0x03 << (UART1_RX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOA_MODE & ~(0x03 << (UART1_TX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART1_TX_PIN * 2));

    /* Alternate function: use hi pins (9 and 10) */
    reg = GPIOA_AFH & ~(0xf << ((UART1_TX_PIN - 8) * 4));
    GPIOA_AFH = reg | (UART1_PIN_AF << ((UART1_TX_PIN - 8) * 4));
    reg = GPIOA_AFH & ~(0xf << ((UART1_RX_PIN - 8) * 4));
    GPIOA_AFH = reg | (UART1_PIN_AF << ((UART1_RX_PIN - 8) * 4));
}

static void uart_clear_errors(uint32_t base)
{
    UART_ICR(base) = UART_ISR(base) & (UART_ENE | UART_EPE | UART_ORE | UART_EFE);
}

int uart_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART_ISR(UART1);
        if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
            uart_clear_errors(UART1);
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART_TDR(UART1) = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    volatile uint32_t reg;
    reg = UART_ISR(UART1);
    if (reg & (UART_ENE | UART_EPE | UART_ORE | UART_EFE))
        uart_clear_errors(UART1);
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART_RDR(UART1);
        return 1;
    }
    return 0;
}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;

    /* Enable USART1 peripheral clock */
    RCC_APB2ENR |= UART1_APB2_CLOCK_ER_VAL;

    /* If the UART is already running (e.g. left enabled by the secure
     * bootloader), let the last frame finish shifting out before
     * reconfiguring, or the character is cut mid-frame */
    if (UART_CR1(UART1) & UART_CR1_UART_ENABLE) {
        while ((UART_ISR(UART1) & UART_ISR_TX_COMPLETE) == 0) {};
    }

    uart_pins_setup();

    /* Use HSI16 as USART1 kernel clock, so the baud rate does not
     * depend on the current SYSCLK/PLL configuration */
    RCC_CR |= RCC_CR_HSION;
    while ((RCC_CR & RCC_CR_HSIRDY) == 0) {};
    reg = RCC_CCIPR1 & (~(RCC_CCIPR1_USART1SEL_MASK << RCC_CCIPR1_USART1SEL_SHIFT));
    RCC_CCIPR1 = reg | (RCC_CCIPR1_USART1SEL_HSI16 << RCC_CCIPR1_USART1SEL_SHIFT);

    /* Disable UART to configure BRR (only writable while disabled) */
    UART_CR1(UART1) &= ~UART_CR1_UART_ENABLE;

    /* Enable 16-bit oversampling */
    UART_CR1(UART1) &= ~UART_CR1_OVER8;

    /* Configure baud rate */
    UART_BRR(UART1) = (uint16_t)(HSI16_FREQ / bitrate);

    /* Configure data bits */
    if (data == 8)
        UART_CR1(UART1) &= ~UART_CR1_SYMBOL_LEN;
    else
        UART_CR1(UART1) |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART_CR1(UART1) |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART_CR1(UART1) |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART_CR1(UART1) &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART_CR2(UART1) & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART_CR2(UART1) = reg | (2 << 12);
    else
        UART_CR2(UART1) = reg;

    /* Clear flags for async mode */
    UART_CR2(UART1) &= ~(UART_CR2_LINEN | UART_CR2_CLKEN);
    UART_CR3(UART1) &= ~(UART_CR3_SCEN | UART_CR3_HDSEL | UART_CR3_IREN);

    /* Configure for RX+TX, turn on. */
    UART_CR1(UART1) |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
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

#endif /* TARGET_stm32u5 */
