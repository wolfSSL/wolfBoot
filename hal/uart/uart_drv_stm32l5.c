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
#include "hal/stm32l5.h"

#define UART1 (0x50008000) /* Using LPUART1 */
#define UART1_CR1      (*(volatile uint32_t *)(UART1 + 0x00))
#define UART1_CR2      (*(volatile uint32_t *)(UART1 + 0x04))
#define UART1_CR3      (*(volatile uint32_t *)(UART1 + 0x08))
#define UART1_BRR      (*(volatile uint32_t *)(UART1 + 0x0c))
#define UART1_ISR      (*(volatile uint32_t *)(UART1 + 0x1c))
#define UART1_ICR      (*(volatile uint32_t *)(UART1 + 0x20))
#define UART1_RDR      (*(volatile uint32_t *)(UART1 + 0x24))
#define UART1_TDR      (*(volatile uint32_t *)(UART1 + 0x28))

#define UART_CR1_UART_ENABLE    (1 << 0)
#define UART_CR1_SYMBOL_LEN     (1 << 12)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_OVER8          (1 << 15)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR2_STOPBITS       (3 << 12)
#define UART_CR2_LINEN          (1 << 14)
#define UART_CR2_CLKEN          (1 << 11)
#define UART_CR3_HDSEL          (1 << 3)
#define UART_CR3_SCEN           (1 << 5)
#define UART_CR3_IREN           (1 << 1)
#define UART_ISR_TX_EMPTY       (1 << 7)
#define UART_ISR_RX_NOTEMPTY    (1 << 5)

#define GPIOG_MODE  (*(volatile uint32_t *)(GPIOG_BASE + 0x00))
#define GPIOG_OTYPE (*(volatile uint32_t *)(GPIOG_BASE + 0x04))
#define GPIOG_OSPD  (*(volatile uint32_t *)(GPIOG_BASE + 0x08))
#define GPIOG_PUPD  (*(volatile uint32_t *)(GPIOG_BASE + 0x0c))
#define GPIOG_ODR   (*(volatile uint32_t *)(GPIOG_BASE + 0x14))
#define GPIOG_BSRR  (*(volatile uint32_t *)(GPIOG_BASE + 0x18))
#define GPIOG_AFL   (*(volatile uint32_t *)(GPIOG_BASE + 0x20))
#define GPIOG_AFH   (*(volatile uint32_t *)(GPIOG_BASE + 0x24))

#define GPIO_MODE_AF (2)

#define CPU_FREQ (110000000)

static void uart1_pins_setup(void)
{
    uint32_t reg;
    /* Set mode = AF */
    reg = GPIOG_MODE & ~ (0x03 << (UART1_RX_PIN * 2));
    GPIOG_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOG_MODE & ~ (0x03 << (UART1_TX_PIN * 2));
    GPIOG_MODE = reg | (2 << (UART1_TX_PIN * 2));
    
    /* Alternate function: use low pins (2 and 3) */
    reg = GPIOG_AFL & ~(0xf << (UART1_TX_PIN * 4));
    GPIOG_AFL = reg | (UART1_PIN_AF << (UART1_TX_PIN * 4));
    reg = GPIOG_AFH & ~(0xf << ((UART1_RX_PIN - 8) * 4));
    GPIOG_AFH = reg | (UART1_PIN_AF << ((UART1_RX_PIN - 8) * 4));

}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF */
    uart1_pins_setup();

    reg = RCC_CCIPR1 & (~ (RCC_CCIPR1_LPUART1SEL_MASK << RCC_CCIPR1_LPUART1SEL_SHIFT));
    RCC_CCIPR1 = reg | (1 << RCC_CCIPR1_LPUART1SEL_SHIFT);

    /* Enable 16-bit oversampling */
    UART1_CR1 &= (~UART_CR1_OVER8);

    /* Configure clock */
    UART1_BRR |= (uint16_t)(CPU_FREQ / bitrate);

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
    
    /* Clear flags for async mode */
    UART1_CR2 &= ~(UART_CR2_LINEN | UART_CR2_CLKEN); 
    UART1_CR3 &= ~(UART_CR3_SCEN | UART_CR3_HDSEL | UART_CR3_IREN); 

    /* Configure for RX+TX, turn on. */
    UART1_CR1 |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
}

int uart_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART1_ISR;
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART1_TDR = c;
    return 1;
}

int uart_rx(uint8_t *c, int len)
{
    volatile uint32_t reg;
    int i = 0;
    reg = UART1_ISR;
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART1_RDR;
        return 1;
    }
    return 0;
}

