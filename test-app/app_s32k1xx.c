/* app_s32k1xx.c
 *
 * Test bare-metal application for NXP S32K1xx
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hal.h"
#include "wolfboot/wolfboot.h"
#include "target.h"

#ifdef TARGET_s32k1xx

/* System Control Block */
#define AIRCR (*(volatile uint32_t *)(0xE000ED0C))
#define AIRCR_VKEY          (0x05FA << 16)
#define AIRCR_SYSRESETREQ   (1 << 2)

/* SysTick Timer */
#define SYST_CSR    (*(volatile uint32_t *)(0xE000E010))
#define SYST_RVR    (*(volatile uint32_t *)(0xE000E014))
#define SYST_CVR    (*(volatile uint32_t *)(0xE000E018))
#define SYST_CALIB  (*(volatile uint32_t *)(0xE000E01C))

#define SYST_CSR_ENABLE     (1 << 0)
#define SYST_CSR_TICKINT    (1 << 1)
#define SYST_CSR_CLKSOURCE  (1 << 2)
#define SYST_CSR_COUNTFLAG  (1 << 16)

/* GPIO for LED (example: PTD15 on S32K142 EVB) */
#define PCC_BASE            (0x40065000UL)
#define PCC_PORTD           (*(volatile uint32_t *)(PCC_BASE + 0x130UL))
#define PCC_CGC             (1UL << 30)

#define PORTD_BASE          (0x4004C000UL)
#define PORTD_PCR15         (*(volatile uint32_t *)(PORTD_BASE + 0x03CUL))
#define PORT_PCR_MUX_GPIO   (1UL << 8)

#define GPIOD_BASE          (0x400FF0C0UL)
#define GPIOD_PDOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x00UL))
#define GPIOD_PSOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x04UL))
#define GPIOD_PCOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x08UL))
#define GPIOD_PTOR          (*(volatile uint32_t *)(GPIOD_BASE + 0x0CUL))
#define GPIOD_PDIR          (*(volatile uint32_t *)(GPIOD_BASE + 0x10UL))
#define GPIOD_PDDR          (*(volatile uint32_t *)(GPIOD_BASE + 0x14UL))

#define LED_PIN             15

/* Clock speed (FIRC = 48 MHz) */
#ifndef CLOCK_SPEED
#define CLOCK_SPEED         48000000UL
#endif

/* Simple delay counter */
static volatile uint32_t systick_count = 0;

void SysTick_Handler(void)
{
    systick_count++;
}

static void delay_ms(uint32_t ms)
{
    uint32_t start = systick_count;
    while ((systick_count - start) < ms) {
        __asm__ volatile ("wfi");
    }
}

static void systick_init(void)
{
    /* Configure SysTick for 1ms tick */
    SYST_RVR = (CLOCK_SPEED / 1000) - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;
}

static void led_init(void)
{
    /* Enable clock to PORTD */
    PCC_PORTD |= PCC_CGC;

    /* Configure PTD15 as GPIO */
    PORTD_PCR15 = PORT_PCR_MUX_GPIO;

    /* Set PTD15 as output */
    GPIOD_PDDR |= (1UL << LED_PIN);

    /* LED off initially (active low on most boards) */
    GPIOD_PSOR = (1UL << LED_PIN);
}

static void led_on(void)
{
    GPIOD_PCOR = (1UL << LED_PIN);  /* Active low */
}

static void led_off(void)
{
    GPIOD_PSOR = (1UL << LED_PIN);
}

static void led_toggle(void)
{
    GPIOD_PTOR = (1UL << LED_PIN);
}

void arch_reboot(void)
{
    AIRCR = AIRCR_VKEY | AIRCR_SYSRESETREQ;
    while (1) {
        __asm__ volatile ("wfi");
    }
}

#ifdef DEBUG_UART
extern void uart_init(void);
extern void uart_write(const char* buf, unsigned int sz);

static void print_version(uint32_t version)
{
    char buf[16];
    int i = 0;

    uart_write("Version: ", 9);

    /* Simple number to string */
    if (version == 0) {
        uart_write("0", 1);
    } else {
        char tmp[10];
        int j = 0;
        while (version > 0) {
            tmp[j++] = '0' + (version % 10);
            version /= 10;
        }
        while (j > 0) {
            buf[i++] = tmp[--j];
        }
        uart_write(buf, i);
    }
    uart_write("\n", 1);
}
#endif

void main(void)
{
    uint32_t version;
    int i;

    /* Initialize hardware */
    hal_init();
    systick_init();
    led_init();

    /* Enable interrupts */
    __asm__ volatile ("cpsie i");

#ifdef DEBUG_UART
    uart_write("wolfBoot S32K1xx Test App\n", 26);
#endif

    /* Get current firmware version */
    version = wolfBoot_current_firmware_version();

#ifdef DEBUG_UART
    print_version(version);
#endif

    /* Mark firmware as successful if version is even */
    if ((version & 0x01) == 0) {
        wolfBoot_success();
#ifdef DEBUG_UART
        uart_write("Firmware marked successful\n", 27);
#endif
    }

    /* Blink LED to show we're running */
    led_on();

#ifdef DEBUG_UART
    uart_write("Entering main loop...\n", 22);
#endif

    /* Main loop - blink LED */
    while (1) {
        led_toggle();
        delay_ms(500);  /* Toggle every 500ms */
    }
}

#endif /* TARGET_s32k1xx */

