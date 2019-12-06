/* led.c
 *
 * Test bare-metal blinking led application
 *
 * Copyright (C) 2019 wolfSSL Inc.
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
#include "wolfboot/wolfboot.h"

#ifdef PLATFORM_stm32f4

#define AHB1_CLOCK_ER (*(volatile uint32_t *)(0x40023830))
#define GPIOD_AHB1_CLOCK_ER (1 << 3)

#define GPIOD_BASE 0x40020c00
#define GPIOD_MODE  (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OTYPE (*(volatile uint32_t *)(GPIOD_BASE + 0x04))
#define GPIOD_OSPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x08))
#define GPIOD_PUPD  (*(volatile uint32_t *)(GPIOD_BASE + 0x0c))
#define GPIOD_ODR   (*(volatile uint32_t *)(GPIOD_BASE + 0x14))
#define GPIOD_BSRR  (*(volatile uint32_t *)(GPIOD_BASE + 0x18))
#define GPIOD_AFL   (*(volatile uint32_t *)(GPIOD_BASE + 0x20))
#define GPIOD_AFH   (*(volatile uint32_t *)(GPIOD_BASE + 0x24))
#define LED_PIN (15)
#define LED_BOOT_PIN (14)
#define GPIO_OSPEED_100MHZ (0x03)
void led_pwm_setup(void)
{
    uint32_t reg;
    AHB1_CLOCK_ER |= GPIOD_AHB1_CLOCK_ER;
    reg = GPIOD_MODE & ~ (0x03 << (LED_PIN * 2));
    GPIOD_MODE = reg | (2 << (LED_PIN * 2));

    reg = GPIOD_OSPD & ~(0x03 << (LED_PIN * 2));
    GPIOD_OSPD = reg | (0x03 << (LED_PIN * 2));

    reg = GPIOD_PUPD & ~(0x03 <<  (LED_PIN * 2));
    GPIOD_PUPD = reg | (0x02 << (LED_PIN * 2));

    /* Alternate function: use high pin */
    reg = GPIOD_AFH & ~(0xf << ((LED_PIN - 8) * 4));
    GPIOD_AFH = reg | (0x2 << ((LED_PIN - 8) * 4));
}

void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;// - 2 * (wolfBoot_current_firmware_version() & 0x01);
    AHB1_CLOCK_ER |= GPIOD_AHB1_CLOCK_ER;
    reg = GPIOD_MODE & ~(0x03 << (pin * 2));
    GPIOD_MODE = reg | (1 << (pin * 2));
    reg = GPIOD_PUPD & ~(0x03 << (pin * 2));
    GPIOD_PUPD = reg | (1 << (pin * 2));
    GPIOD_BSRR |= (1 << pin);
}

#endif /* PLATFORM_stm32f4 */

#ifdef PLATFORM_stm32l0
#define LED_BOOT_PIN (5)

#define RCC_IOPENR (*(volatile uint32_t *)(0x4002102C))
#define IOPAEN (1 << 0)


#define GPIOA_BASE 0x50000000
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPE (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x0c))
#define GPIOA_ODR   (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))


void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    RCC_IOPENR |= IOPAEN;
    reg = GPIOA_MODE & ~(0x03 << (pin * 2));
    GPIOA_MODE = reg | (1 << (pin * 2));
    reg = GPIOA_PUPD & ~(0x03 << (pin * 2));
    GPIOA_PUPD = reg | (1 << (pin * 2));
    GPIOA_BSRR |= (1 << pin);
}


#endif /* PLATFORM_stm32l0 */

#ifdef PLATFORM_stm32g0
#include <stdint.h>
#include "wolfboot/wolfboot.h"


/*GPIOA5*/
#define RCC_IOPENR (*(volatile uint32_t *)(0x40021034)) // 40021034
#define RCC_IOPENR_GPIOAEN (1 << 0)

#define GPIOA_BASE 0x50000000
#define GPIOA_MODE  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPE (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPD  (*(volatile uint32_t *)(GPIOA_BASE + 0x0c))
#define GPIOA_ODR   (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_BSRR  (*(volatile uint32_t *)(GPIOA_BASE + 0x18))
#define GPIOA_AFL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFH   (*(volatile uint32_t *)(GPIOA_BASE + 0x24))
#define LED_PIN (5)
#define LED_BOOT_PIN (5)
#define GPIO_OSPEED_100MHZ (0x03)

void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    RCC_IOPENR |= RCC_IOPENR_GPIOAEN;
    reg = GPIOA_MODE & ~(0x03 << (pin * 2));
    GPIOA_MODE = reg | (1 << (pin * 2)); // general purpose output mode
    reg = GPIOA_PUPD & ~(0x03 << (pin * 2));
    GPIOA_PUPD = reg | (1 << (pin * 2)); // pull-up
    GPIOA_BSRR |= (1 << pin); // set pin
}

#endif /** PLATFORM_stm32g0 **/

#ifdef PLATFORM_stm32wb
#define LED_BOOT_PIN (0)
#define RCC_AHB2_CLOCK_ER (*(volatile uint32_t *)(0x5800004C))
#define GPIOB_AHB2_CLOCK_ER (1 << 1)

#define GPIOB_BASE 0x48000400
#define GPIOB_MODE  (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPE (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPD  (*(volatile uint32_t *)(GPIOB_BASE + 0x0c))
#define GPIOB_ODR   (*(volatile uint32_t *)(GPIOB_BASE + 0x14))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFL   (*(volatile uint32_t *)(GPIOB_BASE + 0x20))
#define GPIOB_AFH   (*(volatile uint32_t *)(GPIOB_BASE + 0x24))


void boot_led_on(void)
{
    uint32_t reg;
    uint32_t pin = LED_BOOT_PIN;
    RCC_AHB2_CLOCK_ER |= GPIOB_AHB2_CLOCK_ER;
    reg = GPIOB_MODE & ~(0x03 << (pin * 2));
    GPIOB_MODE = reg | (1 << (pin * 2));
    reg = GPIOB_PUPD & ~(0x03 << (pin * 2));
    GPIOB_PUPD = reg | (1 << (pin * 2));
    GPIOB_BSRR |= (1 << pin);
}


#endif /* PLATFORM_stm32wb */
