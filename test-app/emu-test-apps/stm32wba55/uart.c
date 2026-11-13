/* uart.c
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

#include <stdint.h>
#include "emu_app.h"

/* wolfBoot hands over on HSI16 (wolfHAL_board.c pll_clock_off) */
#define SYSCLK_HZ 16000000u

/* RCC */
#define RCC_BASE          0x46020C00u
#define RCC_AHB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x8Cu))
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0xA4u))

/* GPIOA / GPIOB */
#define GPIOA_BASE        0x42020000u
#define GPIOB_BASE        0x42020400u
#define GPIO_MODER(x)     (*(volatile uint32_t *)((x) + 0x00u))
#define GPIO_OTYPER(x)    (*(volatile uint32_t *)((x) + 0x04u))
#define GPIO_OSPEEDR(x)   (*(volatile uint32_t *)((x) + 0x08u))
#define GPIO_PUPDR(x)     (*(volatile uint32_t *)((x) + 0x0Cu))
#define GPIO_AFRH(x)      (*(volatile uint32_t *)((x) + 0x24u))

/* USART1: TX PB12, RX PA8, AF7 (NUCLEO-WBA55CG VCP) */
#define USART1_BASE       0x40013800u
#define USART_CR1(b)      (*(volatile uint32_t *)((b) + 0x00u))
#define USART_CR2(b)      (*(volatile uint32_t *)((b) + 0x04u))
#define USART_CR3(b)      (*(volatile uint32_t *)((b) + 0x08u))
#define USART_BRR(b)      (*(volatile uint32_t *)((b) + 0x0Cu))
#define USART_ISR(b)      (*(volatile uint32_t *)((b) + 0x1Cu))
#define USART_RDR(b)      (*(volatile uint32_t *)((b) + 0x24u))
#define USART_TDR(b)      (*(volatile uint32_t *)((b) + 0x28u))

static void gpio_config_pin_af7(uint32_t port, uint32_t pin)
{
    uint32_t v;

    v = GPIO_MODER(port) & ~(3u << (pin * 2u));
    GPIO_MODER(port) = v | (2u << (pin * 2u));

    GPIO_OTYPER(port) &= ~(1u << pin);

    v = GPIO_OSPEEDR(port) & ~(3u << (pin * 2u));
    GPIO_OSPEEDR(port) = v | (2u << (pin * 2u));

    v = GPIO_PUPDR(port) & ~(3u << (pin * 2u));
    GPIO_PUPDR(port) = v | (1u << (pin * 2u));

    v = GPIO_AFRH(port) & ~(0xFu << ((pin - 8u) * 4u));
    GPIO_AFRH(port) = v | (7u << ((pin - 8u) * 4u));
}

static void usart1_init_115200(void)
{
    RCC_APB2ENR |= (1u << 14);
    /* wolfBoot may have left the UART enabled: do not toggle UE, the
     * emulator would reopen the PTY and the update server lose it */
    USART_BRR(USART1_BASE) = SYSCLK_HZ / 115200u;
    USART_CR1(USART1_BASE) |= (1u << 0) | (1u << 2) | (1u << 3);
}

void emu_uart_init(void)
{
    RCC_AHB2ENR |= (1u << 0) | (1u << 1);
    gpio_config_pin_af7(GPIOB_BASE, 12u);
    gpio_config_pin_af7(GPIOA_BASE, 8u);
    usart1_init_115200();
}

void emu_uart_write(uint8_t c)
{
    while ((USART_ISR(USART1_BASE) & (1u << 7)) == 0u) {
    }
    USART_TDR(USART1_BASE) = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((USART_ISR(USART1_BASE) & (1u << 5)) == 0u) {
        return 0;
    }
    *c = (uint8_t)USART_RDR(USART1_BASE);
    return 1;
}
