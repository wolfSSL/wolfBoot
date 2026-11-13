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

/* wolfBoot hands over on HSIDIV3 (hal/stm32c5.c clock_psi_off) */
#define SYSCLK_HZ 48000000u

/* RCC */
#define RCC_BASE          0x44020C00u
#define RCC_AHB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x8Cu))
#define RCC_APB1LENR      (*(volatile uint32_t *)(RCC_BASE + 0x9Cu))

/* GPIOA */
#define GPIOA_BASE        0x42020000u
#define GPIO_MODER(x)     (*(volatile uint32_t *)((x) + 0x00u))
#define GPIO_OTYPER(x)    (*(volatile uint32_t *)((x) + 0x04u))
#define GPIO_OSPEEDR(x)   (*(volatile uint32_t *)((x) + 0x08u))
#define GPIO_PUPDR(x)     (*(volatile uint32_t *)((x) + 0x0Cu))
#define GPIO_AFRL(x)      (*(volatile uint32_t *)((x) + 0x20u))

/* USART2: TX PA2, RX PA3, AF7 (NUCLEO-C5A3ZG VCP) */
#define USART2_BASE       0x40004400u
#define USART_CR1(b)      (*(volatile uint32_t *)((b) + 0x00u))
#define USART_CR2(b)      (*(volatile uint32_t *)((b) + 0x04u))
#define USART_CR3(b)      (*(volatile uint32_t *)((b) + 0x08u))
#define USART_BRR(b)      (*(volatile uint32_t *)((b) + 0x0Cu))
#define USART_ISR(b)      (*(volatile uint32_t *)((b) + 0x1Cu))
#define USART_RDR(b)      (*(volatile uint32_t *)((b) + 0x24u))
#define USART_TDR(b)      (*(volatile uint32_t *)((b) + 0x28u))

static void gpio_config_usart2_pa2_pa3(void)
{
    uint32_t v;
    RCC_AHB2ENR |= (1u << 0);

    v = GPIO_MODER(GPIOA_BASE);
    v &= ~((3u << (2u * 2u)) | (3u << (3u * 2u)));
    v |= (2u << (2u * 2u)) | (2u << (3u * 2u));
    GPIO_MODER(GPIOA_BASE) = v;

    v = GPIO_OTYPER(GPIOA_BASE);
    v &= ~((1u << 2) | (1u << 3));
    GPIO_OTYPER(GPIOA_BASE) = v;

    v = GPIO_OSPEEDR(GPIOA_BASE);
    v &= ~((3u << (2u * 2u)) | (3u << (3u * 2u)));
    v |= (2u << (2u * 2u)) | (2u << (3u * 2u));
    GPIO_OSPEEDR(GPIOA_BASE) = v;

    v = GPIO_PUPDR(GPIOA_BASE);
    v &= ~((3u << (2u * 2u)) | (3u << (3u * 2u)));
    v |= (1u << (3u * 2u));
    GPIO_PUPDR(GPIOA_BASE) = v;

    v = GPIO_AFRL(GPIOA_BASE);
    v &= ~((0xFu << (2u * 4u)) | (0xFu << (3u * 4u)));
    v |= (7u << (2u * 4u)) | (7u << (3u * 4u));
    GPIO_AFRL(GPIOA_BASE) = v;
}

static void usart2_init_115200(void)
{
    RCC_APB1LENR |= (1u << 17);
    /* wolfBoot may have left the UART enabled: do not toggle UE, the
     * emulator would reopen the PTY and the update server lose it */
    USART_BRR(USART2_BASE) = SYSCLK_HZ / 115200u;
    USART_CR1(USART2_BASE) |= (1u << 0) | (1u << 2) | (1u << 3);
}

void emu_uart_init(void)
{
    gpio_config_usart2_pa2_pa3();
    usart2_init_115200();
}

void emu_uart_write(uint8_t c)
{
    while ((USART_ISR(USART2_BASE) & (1u << 7)) == 0u) {
    }
    USART_TDR(USART2_BASE) = (uint32_t)c;
}

int emu_uart_read(uint8_t *c)
{
    if ((USART_ISR(USART2_BASE) & (1u << 5)) == 0u) {
        return 0;
    }
    *c = (uint8_t)USART_RDR(USART2_BASE);
    return 1;
}
