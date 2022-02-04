/* nrf52.c
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "wolfboot/wolfboot.h"


#define GPIO_BASE (0x50000000)
#define GPIO_OUT        *((volatile uint32_t *)(GPIO_BASE + 0x504))
#define GPIO_OUTSET     *((volatile uint32_t *)(GPIO_BASE + 0x508))
#define GPIO_OUTCLR     *((volatile uint32_t *)(GPIO_BASE + 0x50C))
#define GPIO_PIN_CNF     ((volatile uint32_t *)(GPIO_BASE + 0x700)) // Array


#define BAUD_115200 0x01D7E000

#define UART0_BASE (0x40002000)
#define UART0_TASK_STARTTX *((volatile uint32_t *)(UART0_BASE + 0x008))
#define UART0_TASK_STOPTX  *((volatile uint32_t *)(UART0_BASE + 0x00C))
#define UART0_EVENT_ENDTX  *((volatile uint32_t *)(UART0_BASE + 0x120))
#define UART0_ENABLE       *((volatile uint32_t *)(UART0_BASE + 0x500))
#define UART0_TXD_PTR      *((volatile uint32_t *)(UART0_BASE + 0x544))
#define UART0_TXD_MAXCOUNT *((volatile uint32_t *)(UART0_BASE + 0x548))
#define UART0_BAUDRATE     *((volatile uint32_t *)(UART0_BASE + 0x524))

static void gpiotoggle(uint32_t pin)
{
    uint32_t reg_val = GPIO_OUT;
    GPIO_OUTCLR = reg_val & (1 << pin);
    GPIO_OUTSET = (~reg_val) & (1 << pin);
}


void uart_init(void)
{
    UART0_BAUDRATE = BAUD_115200;
    UART0_ENABLE = 1;

}

void uart_write(char c)
{
    UART0_EVENT_ENDTX = 0;

    UART0_TXD_PTR = (uint32_t)(&c);
    UART0_TXD_MAXCOUNT = 1;
    UART0_TASK_STARTTX = 1;
    while(UART0_EVENT_ENDTX == 0)
        ;
}

static const char START='*';
void main(void)
{
    //uint32_t pin = 19;
    uint32_t pin = 6;
    int i;
    uint32_t version = 0;
    uint8_t *v_array = (uint8_t *)&version;
    GPIO_PIN_CNF[pin] = 1; /* Output */

    version = wolfBoot_current_firmware_version();

    uart_init();
    uart_write(START);
    for (i = 3; i >= 0; i--) {
        uart_write(v_array[i]);
    }
    while(1) {
        gpiotoggle(pin);
        for (i = 0; i < 800000; i++)  // Wait a bit.
              asm volatile ("nop");
    }
}
