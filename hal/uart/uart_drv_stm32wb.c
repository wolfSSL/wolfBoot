/* uart_drv_stm32wb.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32WB, using UART1.
 *
 * Pinout: RX=PB7, TX=PB6 (VCOM port UART1 -> STLINK USB)
 *
 * Copyright (C) 2021 wolfSSL Inc.
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

/* Driver hardcoded to work on UART1 (PB6/PB7) */
#define UART1 (0x40013800)
#define UART1_PIN_AF 7
#define UART1_RX_PIN 7
#define UART1_TX_PIN 6

#define UART1_CR1      (*(volatile uint32_t *)(UART1 + 0x00))
#define UART1_CR2      (*(volatile uint32_t *)(UART1 + 0x04))
#define UART1_BRR      (*(volatile uint32_t *)(UART1 + 0x0C))
#define UART1_ISR      (*(volatile uint32_t *)(UART1 + 0x1C))
#define UART1_RDR      (*(volatile uint32_t *)(UART1 + 0x24))
#define UART1_TDR      (*(volatile uint32_t *)(UART1 + 0x28))
#define UART_CR1_UART_ENABLE    (1 << 0)
#define UART_CR1_TX_ENABLE      (1 << 3)
#define UART_CR1_RX_ENABLE      (1 << 2)
#define UART_CR1_SYMBOL_LEN     (1 << 28)
#define UART_CR1_FIFO_ENABLE    (1 << 29)
#define UART_CR1_PARITY_ENABLED (1 << 10)
#define UART_CR1_PARITY_ODD     (1 << 9)
#define UART_ISR_TX_EMPTY       (1 << 7)
#define UART_ISR_RX_NOTEMPTY    (1 << 5)

#define CLOCK_SPEED (64000000) /* 64 MHz (STM32WB55) */

#define AHB2_CLOCK_ER (*(volatile uint32_t *)(0x5800004c))
#define GPIOB_AHB2_CLOCK_ER (1 << 1)
#define GPIOB_BASE 0x48000400
#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#define GPIO_MODE_AF (2)

static void uart_pins_setup(void)
{
    uint32_t reg;
    AHB2_CLOCK_ER |= GPIOB_AHB2_CLOCK_ER;
    /* Set mode = AF */
    reg = GPIOB_MODE & ~ (0x03 << (UART1_RX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_RX_PIN * 2));
    reg = GPIOB_MODE & ~ (0x03 << (UART1_TX_PIN * 2));
    GPIOB_MODE = reg | (2 << (UART1_TX_PIN * 2));

    /* Alternate function: use low pins (6 and 7) */
    reg = GPIOB_AFL & ~(0xf << ((UART1_TX_PIN) * 4));
    GPIOB_AFL = reg | (UART1_PIN_AF << ((UART1_TX_PIN) * 4));
    reg = GPIOB_AFL & ~(0xf << ((UART1_RX_PIN) * 4));
    GPIOB_AFL = reg | (UART1_PIN_AF << ((UART1_RX_PIN) * 4));
}

int uart_tx(const uint8_t c)
{
    uint32_t reg;
    do {
        reg = UART1_ISR;
    } while ((reg & UART_ISR_TX_EMPTY) == 0);
    UART1_TDR = c;
    return 1;
}

int uart_rx(uint8_t *c)
{
    volatile uint32_t reg = UART1_ISR;
    if ((reg & UART_ISR_RX_NOTEMPTY) != 0) {
        reg = UART1_RDR;
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
    
    UART1_CR1 &= ~(UART_CR1_UART_ENABLE);
    UART1_CR1 &= ~(UART_CR1_FIFO_ENABLE);

    /* Configure for TX + RX */
    UART1_CR1 |= (UART_CR1_TX_ENABLE | UART_CR1_RX_ENABLE);

    /* Configure clock */
    UART1_BRR =   CLOCK_SPEED / (2 * bitrate);

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
    /* Set stop bits (not supported) */
    (void)stop;

    /* Turn on uart */
    UART1_CR1 |= UART_CR1_UART_ENABLE;
    return 0;
}

