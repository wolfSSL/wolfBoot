/* uart_drv_stm32l0.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32L0, using UART2 (PA2/PA3).
 *
 * Pinout: RX=PA3, TX=PA2 
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

#define UART2 (0x40004400)
#define UART2_CR1      (*(volatile uint32_t *)(UART2 + 0x00))
#define UART2_CR2      (*(volatile uint32_t *)(UART2 + 0x04))
#define UART2_CR3      (*(volatile uint32_t *)(UART2 + 0x08))
#define UART2_BRR      (*(volatile uint32_t *)(UART2 + 0x0c))
#define UART2_ISR      (*(volatile uint32_t *)(UART2 + 0x1c))
#define UART2_ICR      (*(volatile uint32_t *)(UART2 + 0x20))
#define UART2_RDR      (*(volatile uint32_t *)(UART2 + 0x24))
#define UART2_TDR      (*(volatile uint32_t *)(UART2 + 0x28))

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

#define RCC_IOPENR              (*(volatile uint32_t *)(0x4002102C))
#define APB1_CLOCK_ER           (*(volatile uint32_t *)(0x40021038))
#define IOPAEN (1 << 0)
#define IOPCEN (1 << 2)
#define UART2_APB1_CLOCK_ER_VAL 	(1 << 17)


#define GPIOA_BASE 0x50000000
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPE (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x0c))
#define GPIOA_ODR   (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

#define GPIO_MODE_AF (2)
#define UART2_PIN_AF 4
#define UART2_RX_PIN 2
#define UART2_TX_PIN 3

#define CPU_FREQ (24000000)

static void uart2_pins_setup(void)
{
    uint32_t reg;
    RCC_IOPENR |= IOPAEN;
    /* Set mode = AF */
    reg = GPIOA_MODE & ~ (0x03 << (UART2_RX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART2_RX_PIN * 2));
    reg = GPIOA_MODE & ~ (0x03 << (UART2_TX_PIN * 2));
    GPIOA_MODE = reg | (2 << (UART2_TX_PIN * 2));
    
    /* Alternate function: use low pins (2 and 3) */
    reg = GPIOA_AFL & ~(0xf << (UART2_TX_PIN * 4));
    GPIOA_AFL = reg | (UART2_PIN_AF << (UART2_TX_PIN * 4));
    reg = GPIOA_AFL & ~(0xf << (UART2_RX_PIN * 4));
    GPIOA_AFL = reg | (UART2_PIN_AF << (UART2_RX_PIN * 4));

}

int uart_init(uint32_t bitrate, uint8_t data, char parity, uint8_t stop)
{
    uint32_t reg;
    /* Enable pins and configure for AF */
    uart2_pins_setup();

    /* Turn on the device */
    APB1_CLOCK_ER |= UART2_APB1_CLOCK_ER_VAL;

    /* Enable 16-bit oversampling */
    UART2_CR1 &= (~UART_CR1_OVER8);

    /* Configure clock */
    UART2_BRR |= (uint16_t)(CPU_FREQ / bitrate);

    /* Configure data bits */
    if (data == 8)
        UART2_CR1 &= ~UART_CR1_SYMBOL_LEN;
    else
        UART2_CR1 |= UART_CR1_SYMBOL_LEN;

    /* Configure parity */
    switch (parity) {
        case 'O':
            UART2_CR1 |= UART_CR1_PARITY_ODD;
            /* fall through to enable parity */
            /* FALL THROUGH */
        case 'E':
            UART2_CR1 |= UART_CR1_PARITY_ENABLED;
            break;
        default:
            UART2_CR1 &= ~(UART_CR1_PARITY_ENABLED | UART_CR1_PARITY_ODD);
    }
    /* Set stop bits */
    reg = UART2_CR2 & ~UART_CR2_STOPBITS;
    if (stop > 1)
        UART2_CR2 = reg & (2 << 12);
    else
        UART2_CR2 = reg;
    
    /* Clear flags for async mode */
    UART2_CR2 &= ~(UART_CR2_LINEN | UART_CR2_CLKEN); 
    UART2_CR3 &= ~(UART_CR3_SCEN | UART_CR3_HDSEL | UART_CR3_IREN); 

    /* Configure for RX+TX, turn on. */
    UART2_CR1 |= UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE | UART_CR1_UART_ENABLE;

    return 0;
}

int uart_tx(const uint8_t c)
{
    volatile uint32_t reg;
    do {
        reg = UART2_ISR;
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART2_TDR = c;
    return 1;
}

int uart_rx(uint8_t *c, int len)
{
    volatile uint32_t reg;
    int i = 0;
    reg = UART2_ISR;
    if (reg & UART_ISR_RX_NOTEMPTY) {
        *c = (uint8_t)UART2_RDR;
        return 1;
    }
    return 0;
}

