/* app_stm32n6.c
 *
 * Test bare-metal application for NUCLEO-N657X0-Q.
 *
 * Copyright (C) 2025 wolfSSL Inc.
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

#include <stdio.h>
#include <stdint.h>
#include "system.h"
#include "hal.h"
#include "hal/stm32n6.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

/* User LEDs: active LOW on Port G (LD6=PG0 green, LD7=PG8 blue, LD5=PG10 red) */
#define LED_GREEN_PIN   0
#define LED_BLUE_PIN    8
#define LED_RED_PIN     10

/* CPU clock for SysTick delay */
#define CPU_FREQ        600000000UL /* IC1 = PLL1(1200MHz) / 2 */
#define SYSTICK_COUNTFLAG (1 << 16)

static void led_init(void)
{
    uint32_t reg;

    RCC_AHB4ENR |= RCC_AHB4ENR_GPIOGEN | RCC_AHB4ENR_PWREN;
    DMB();

    PWR_SVMCR3 |= PWR_SVMCR3_VDDIO2SV | PWR_SVMCR3_VDDIO3SV;
    DMB();

    /* Set PG0, PG8, PG10 to output mode */
    reg = GPIO_MODER(GPIOG_BASE);
    reg &= ~(0x3 << (LED_GREEN_PIN * 2));
    reg |= (0x1 << (LED_GREEN_PIN * 2));
    reg &= ~(0x3 << (LED_BLUE_PIN * 2));
    reg |= (0x1 << (LED_BLUE_PIN * 2));
    reg &= ~(0x3 << (LED_RED_PIN * 2));
    reg |= (0x1 << (LED_RED_PIN * 2));
    GPIO_MODER(GPIOG_BASE) = reg;
}

static void led_on(uint32_t gpio_base, int pin)
{
    GPIO_BSRR(gpio_base) = (1 << (pin + 16)); /* active LOW */
}

static void led_off(uint32_t gpio_base, int pin)
{
    GPIO_BSRR(gpio_base) = (1 << pin);
}

static void systick_init(void)
{
    SYSTICK_RVR = (CPU_FREQ / 1000) - 1;
    SYSTICK_CVR = 0;
    SYSTICK_CSR = 0x5; /* enable, processor clock, no interrupt */
}

static void delay_ms(uint32_t ms)
{
    while (ms > 0) {
        while (!(SYSTICK_CSR & SYSTICK_COUNTFLAG))
            ;
        ms--;
    }
}

static const char* state_name(uint8_t state)
{
    switch (state) {
        case IMG_STATE_NEW:      return "NEW";
        case IMG_STATE_UPDATING: return "UPDATING";
        case IMG_STATE_TESTING:  return "TESTING";
        case IMG_STATE_SUCCESS:  return "SUCCESS";
        default:                 return "UNKNOWN";
    }
}

static void print_partition_info(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state = 0, update_state = 0;

    boot_ver = wolfBoot_current_firmware_version();
    update_ver = wolfBoot_update_firmware_version();
    wolfBoot_get_partition_state(PART_BOOT, &boot_state);
    wolfBoot_get_partition_state(PART_UPDATE, &update_state);

    printf("Partition Info\r\n");
    printf("  Boot:   version %lu, state %s\r\n",
           (unsigned long)boot_ver, state_name(boot_state));
    printf("  Update: version %lu, state %s\r\n",
           (unsigned long)update_ver, state_name(update_state));
}

void main(void)
{
    uint32_t version;
    uint8_t boot_state = 0;
    int led_pin;

    /* hal_init() not called -- XSPI2 already configured by wolfBoot for XIP */
    led_init();
    led_on(GPIOG_BASE, LED_GREEN_PIN);

    version = wolfBoot_current_firmware_version();

    printf("\r\n=== STM32N6 wolfBoot Test App ===\r\n");
    printf("Firmware Version: %lu\r\n", (unsigned long)version);
    print_partition_info();

    /* Auto-handle boot state */
    wolfBoot_get_partition_state(PART_BOOT, &boot_state);
    if (boot_state == IMG_STATE_TESTING) {
        printf("State TESTING -> marking success\r\n");
        wolfBoot_success();
    } else if (boot_state != IMG_STATE_SUCCESS) {
        printf("Calling wolfBoot_success()\r\n");
        wolfBoot_success();
    }
    printf("Boot OK (state: %s)\r\n", state_name(boot_state));

    /* Enable icache for XIP performance (hal_prepare_boot disables it) */
    DSB();
    ISB();
    SCB_ICIALLU = 0;
    DSB();
    ISB();
    SCB_CCR |= SCB_CCR_IC;
    DSB();
    ISB();

    systick_init();

    /* Blink LED based on version: blue for v1, red for v>1 */
    printf("Blinking %s LED\r\n", (version > 1) ? "red" : "blue");

    led_pin = (version > 1) ? LED_RED_PIN : LED_BLUE_PIN;
    while (1) {
        led_on(GPIOG_BASE, led_pin);
        delay_ms(500);
        led_off(GPIOG_BASE, led_pin);
        delay_ms(500);
    }
}
