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

/* wolfBoot hands over on MSIS at 96 MHz (hal/stm32u3.c clock_96mhz) */
#define SYSCLK_HZ 96000000u

/* RCC */
#define RCC_BASE          0x40030C00u
#define RCC_AHB2ENR1      (*(volatile uint32_t *)(RCC_BASE + 0x8Cu))
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0xA4u))

/* GPIOA */
#define GPIOA_BASE        0x42020000u
#define GPIO_MODER(x)     (*(volatile uint32_t *)((x) + 0x00u))
#define GPIO_OTYPER(x)    (*(volatile uint32_t *)((x) + 0x04u))
#define GPIO_OSPEEDR(x)   (*(volatile uint32_t *)((x) + 0x08u))
#define GPIO_PUPDR(x)     (*(volatile uint32_t *)((x) + 0x0Cu))
#define GPIO_AFRH(x)      (*(volatile uint32_t *)((x) + 0x24u))

/* USART1: TX PA9, RX PA10, AF7 (NUCLEO-U385RG-Q VCP) */
#define USART1_BASE       0x40013800u
#define USART_CR1(b)      (*(volatile uint32_t *)((b) + 0x00u))
#define USART_CR2(b)      (*(volatile uint32_t *)((b) + 0x04u))
#define USART_CR3(b)      (*(volatile uint32_t *)((b) + 0x08u))
#define USART_BRR(b)      (*(volatile uint32_t *)((b) + 0x0Cu))
#define USART_ISR(b)      (*(volatile uint32_t *)((b) + 0x1Cu))
#define USART_RDR(b)      (*(volatile uint32_t *)((b) + 0x24u))
#define USART_TDR(b)      (*(volatile uint32_t *)((b) + 0x28u))

static void gpio_config_usart1_pa9_pa10(void)
{
    uint32_t v;
    RCC_AHB2ENR1 |= (1u << 0);

    v = GPIO_MODER(GPIOA_BASE);
    v &= ~((3u << (9u * 2u)) | (3u << (10u * 2u)));
    v |= (2u << (9u * 2u)) | (2u << (10u * 2u));
    GPIO_MODER(GPIOA_BASE) = v;

    v = GPIO_OTYPER(GPIOA_BASE);
    v &= ~((1u << 9) | (1u << 10));
    GPIO_OTYPER(GPIOA_BASE) = v;

    v = GPIO_OSPEEDR(GPIOA_BASE);
    v &= ~((3u << (9u * 2u)) | (3u << (10u * 2u)));
    v |= (2u << (9u * 2u)) | (2u << (10u * 2u));
    GPIO_OSPEEDR(GPIOA_BASE) = v;

    v = GPIO_PUPDR(GPIOA_BASE);
    v &= ~((3u << (9u * 2u)) | (3u << (10u * 2u)));
    v |= (1u << (10u * 2u));
    GPIO_PUPDR(GPIOA_BASE) = v;

    v = GPIO_AFRH(GPIOA_BASE);
    v &= ~((0xFu << ((9u - 8u) * 4u)) | (0xFu << ((10u - 8u) * 4u)));
    v |= (7u << ((9u - 8u) * 4u)) | (7u << ((10u - 8u) * 4u));
    GPIO_AFRH(GPIOA_BASE) = v;
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
    gpio_config_usart1_pa9_pa10();
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
