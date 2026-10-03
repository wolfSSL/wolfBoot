/* uart_drv_stm32h7s.c
 *
 * Driver for the back-end of the UART_FLASH module.
 *
 * Example implementation for stm32h7s, using USART3.
 *
 * On the NUCLEO-H7S3L8 USART3 on PD8/PD9 is the port routed to the
 * on-board ST-LINK virtual COM port.
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

#ifdef TARGET_stm32h7s

#include <stdint.h>
#include "hal/stm32h7s.h"

static void uart_pins_setup(void)
{
    uint32_t reg;

    RCC_AHB4ENR |= UART_GPIO_RCC_EN;
    DMB();

    /* Mode = alternate function */
    reg = GPIO_MODE(UART_GPIO_BASE);
    reg &= ~((0x3U << (UART_TX_PIN * 2)) | (0x3U << (UART_RX_PIN * 2)));
    reg |= (GPIO_MODE_AF << (UART_TX_PIN * 2)) |
           (GPIO_MODE_AF << (UART_RX_PIN * 2));
    GPIO_MODE(UART_GPIO_BASE) = reg;

    /* Both pins are above 7, so the high alternate-function register */
    reg = GPIO_AFH(UART_GPIO_BASE);
    reg &= ~((0xFU << ((UART_TX_PIN - 8) * 4)) |
             (0xFU << ((UART_RX_PIN - 8) * 4)));
    reg |= (UART_PIN_AF << ((UART_TX_PIN - 8) * 4)) |
           (UART_PIN_AF << ((UART_RX_PIN - 8) * 4));
    GPIO_AFH(UART_GPIO_BASE) = reg;
}

void uart_init(void)
{
    RCC_APB1ENR1 |= RCC_APB1ENR1_USART3EN;
    DMB();

    uart_pins_setup();

    UART_CR1 = 0;
    UART_CR2 = 0;
    UART_CR3 = 0;
    /* Oversampling by 16, so BRR is simply the clock over the baud rate */
    UART_BRR = (UART_PCLK / UART_BAUD);
    UART_CR1 = UART_CR1_TE | UART_CR1_RE | UART_CR1_UE;
}

void uart_write(const char *buf, unsigned int sz)
{
    unsigned int i;

    for (i = 0; i < sz; i++) {
        while ((UART_ISR & UART_ISR_TXE) == 0)
            ;
        UART_TDR = (uint32_t)buf[i];
    }
    while ((UART_ISR & UART_ISR_TC) == 0)
        ;
}

#endif /* TARGET_stm32h7s */
