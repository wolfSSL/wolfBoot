/* main.c
 *
 * Test bare-metal blinking led application
 *
 * Copyright (C) 2018 wolfSSL Inc.
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
#include "system.h"
#include "timer.h"
#include "led.h"

#ifdef PLATFORM_stm32f4

void main(void) {
    flash_set_waitstates();
    clock_config();
    led_pwm_setup();
    pwm_init(CPU_FREQ, 0);
    /* Dim the led by altering the PWM duty-cicle 
     * in isr_tim2 (timer.c)
     *
     * Every 50ms, the duty cycle of the PWM connected
     * to the blue led increases/decreases making a pulse
     * effect.
     */
    timer_init(CPU_FREQ, 1, 50);
    asm volatile ("cpsie i");
    while(1)
        WFI();
}
#endif

#ifdef PLATFORM_nrf52
#define GPIO_BASE (0x50000000)
#define GPIO_OUT        *((volatile uint32_t *)(GPIO_BASE + 0x504))
#define GPIO_OUTSET     *((volatile uint32_t *)(GPIO_BASE + 0x508))
#define GPIO_OUTCLR     *((volatile uint32_t *)(GPIO_BASE + 0x50C))
#define GPIO_PIN_CNF     ((volatile uint32_t *)(GPIO_BASE + 0x700)) // Array

static void gpiotoggle(uint32_t pin)
{
    uint32_t reg_val = GPIO_OUT;
    GPIO_OUTCLR = reg_val & (1 << pin);
    GPIO_OUTSET = (~reg_val) & (1 << pin);
}

void main(void)
{
    uint32_t pin = 19;
    int i;
    GPIO_PIN_CNF[pin] = 1; /* Output */
    while(1) {
        gpiotoggle(pin);
        for (i = 0; i < 800000; i++)  // Wait a bit.
              asm volatile ("nop");
    }
}

#endif


