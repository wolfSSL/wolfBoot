/* timer.c
 *
 * Test bare-metal blinking led application
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

#ifdef PLATFORM_stm32f4
#include <stdint.h>

#include "system.h"
#include "led.h"


/* STM32 specific defines */
#define APB1_CLOCK_ER           (*(volatile uint32_t *)(0x40023840))
#define APB1_CLOCK_RST          (*(volatile uint32_t *)(0x40023820))
#define TIM4_APB1_CLOCK_ER_VAL 	(1 << 2)
#define TIM2_APB1_CLOCK_ER_VAL 	(1 << 0)

#define TIM2_BASE (0x40000000)
#define TIM2_CR1  (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_DIER (*(volatile uint32_t *)(TIM2_BASE + 0x0c))
#define TIM2_SR   (*(volatile uint32_t *)(TIM2_BASE + 0x10))
#define TIM2_PSC  (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR  (*(volatile uint32_t *)(TIM2_BASE + 0x2c))

#define TIM4_BASE (0x40000800)
#define TIM4_CR1    (*(volatile uint32_t *)(TIM4_BASE + 0x00))
#define TIM4_DIER   (*(volatile uint32_t *)(TIM4_BASE + 0x0c))
#define TIM4_SR     (*(volatile uint32_t *)(TIM4_BASE + 0x10))
#define TIM4_CCMR1  (*(volatile uint32_t *)(TIM4_BASE + 0x18))
#define TIM4_CCMR2  (*(volatile uint32_t *)(TIM4_BASE + 0x1c))
#define TIM4_CCER   (*(volatile uint32_t *)(TIM4_BASE + 0x20))
#define TIM4_PSC    (*(volatile uint32_t *)(TIM4_BASE + 0x28))
#define TIM4_ARR    (*(volatile uint32_t *)(TIM4_BASE + 0x2c))
#define TIM4_CCR4   (*(volatile uint32_t *)(TIM4_BASE + 0x40))

#define TIM_DIER_UIE (1 << 0)
#define TIM_SR_UIF   (1 << 0)
#define TIM_CR1_CLOCK_ENABLE (1 << 0)
#define TIM_CR1_UPD_RS       (1 << 2)
#define TIM_CR1_ARPE         (1 << 7)

#define TIM_CCER_CC4_ENABLE  (1 << 12)
#define TIM_CCMR1_OC1M_PWM1  (0x06 << 4)
#define TIM_CCMR2_OC4M_PWM1  (0x06 << 12)

#define AHB1_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOD_AHB1_CLOCK_ER (1 << 3)

#define GPIOD_BASE 0x40020c00
#define GPIOD_MODE (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OTYPE (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_PUPD (*(volatile uint32_t *)(GPIOD_BASE + 0x0c))
#define GPIOD_ODR  (*(volatile uint32_t *)(GPIOD_BASE + 0x14))

static uint32_t master_clock = 0;

/** Use TIM4_CH4, which is linked to PD15 AF1 **/
int pwm_init(uint32_t clock, uint32_t threshold)
{
    uint32_t val = (clock / 100000); /* Frequency is 100 KHz */
    uint32_t lvl;
    master_clock = clock;

    if (threshold > 100)
        return -1;

    lvl = (val * threshold) / 100;
    if (lvl != 0)
        lvl--;

    APB1_CLOCK_RST |= TIM4_APB1_CLOCK_ER_VAL;
    asm volatile ("dmb");
    APB1_CLOCK_RST &= ~TIM4_APB1_CLOCK_ER_VAL;
    APB1_CLOCK_ER |= TIM4_APB1_CLOCK_ER_VAL;

    /* disable CC */
    TIM4_CCER  &= ~TIM_CCER_CC4_ENABLE;
    TIM4_CR1    = 0;
    TIM4_PSC    = 0;
    TIM4_ARR    = val - 1;
    TIM4_CCR4   = lvl;
    TIM4_CCMR1  &= ~(0x03 << 0);
    TIM4_CCMR1  &= ~(0x07 << 4);
    TIM4_CCMR1  |= TIM_CCMR1_OC1M_PWM1;
    TIM4_CCMR2  &= ~(0x03 << 8);
    TIM4_CCMR2  &= ~(0x07 << 12);
    TIM4_CCMR2  |= TIM_CCMR2_OC4M_PWM1;
    TIM4_CCER  |= TIM_CCER_CC4_ENABLE;
    TIM4_CR1    |= TIM_CR1_CLOCK_ENABLE | TIM_CR1_ARPE;
    asm volatile ("dmb");
    return 0;
}

int timer_init(uint32_t clock, uint32_t prescaler, uint32_t interval_ms)
{
    uint32_t val = 0;
    uint32_t psc = 1;
    uint32_t err = 0;
    clock = ((clock * prescaler) / 1000) * interval_ms;

    while (psc < 65535) {
        val = clock / psc;
        err = clock % psc;
        if ((val < 65535) && (err == 0)) {
            val--;
            break;
        }
        val = 0;
        psc++;
    }
    if (val == 0)
        return -1;

    nvic_irq_enable(NVIC_TIM2_IRQN);
    nvic_irq_setprio(NVIC_TIM2_IRQN, 0);
    APB1_CLOCK_RST |= TIM2_APB1_CLOCK_ER_VAL;
    asm volatile ("dmb");
    APB1_CLOCK_RST &= ~TIM2_APB1_CLOCK_ER_VAL;
    APB1_CLOCK_ER |= TIM2_APB1_CLOCK_ER_VAL;

    TIM2_CR1    = 0;
    asm volatile ("dmb");
    TIM2_PSC    = psc;
    TIM2_ARR    = val;
    TIM2_CR1    |= TIM_CR1_CLOCK_ENABLE;
    TIM2_DIER   |= TIM_DIER_UIE;
    asm volatile ("dmb");
    return 0;
}

extern volatile uint32_t time_elapsed;
void isr_tim2(void)
{
    static volatile uint32_t tim2_ticks = 0;
    TIM2_SR &= ~TIM_SR_UIF;

    /* Dim the led by altering the PWM duty-cicle */
    if (++tim2_ticks > 15)
        tim2_ticks = 0;
    if (tim2_ticks > 8)
        pwm_init(master_clock, 10 * (16 - tim2_ticks));
    else
        pwm_init(master_clock, 10 * tim2_ticks);

    time_elapsed++;
}
#else
void isr_tim2(void)
{
}

#endif /* PLATFORM_stm32f4 */
