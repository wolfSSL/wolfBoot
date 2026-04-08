/* app_nxp_lpc54s018m.c
 *
 * Test application for LPC54S018M-EVK
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

#include <stdint.h>
#include "target.h"
#include "wolfboot/wolfboot.h"

#ifdef DEBUG_UART
extern void uart_write(const char *buf, unsigned int sz);

static void print_str(const char *s)
{
    unsigned int len = 0;
    while (s[len] != '\0')
        len++;
    uart_write(s, len);
}

static void print_hex32(uint32_t val)
{
    static const char hex[] = "0123456789abcdef";
    char buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++)
        buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
    uart_write(buf, 10);
}
#endif

/* LPC54S018M-EVK GPIO register definitions */
/* GPIO base: 0x4008C000 */
#define GPIO_BASE       0x4008C000
#define GPIO_DIR(port)  (*(volatile uint32_t *)(GPIO_BASE + 0x2000 + (port) * 4))
#define GPIO_SET(port)  (*(volatile uint32_t *)(GPIO_BASE + 0x2200 + (port) * 4))
#define GPIO_CLR(port)  (*(volatile uint32_t *)(GPIO_BASE + 0x2280 + (port) * 4))

/* SYSCON base for clock gating */
#define SYSCON_BASE         0x40000000
#define SYSCON_AHBCLKCTRL0  (*(volatile uint32_t *)(SYSCON_BASE + 0x200))

/* AHB clock control bits for GPIO ports (AHBCLKCTRL[0]) */
#define AHBCLKCTRL0_GPIO2   (1UL << 16)
#define AHBCLKCTRL0_GPIO3   (1UL << 17)

/* LPC54S018M-EVK User LEDs (accent LEDs active low) */
#define LED1_PORT   3
#define LED1_PIN    14  /* USR_LED1: P3.14 */
#define LED2_PORT   3
#define LED2_PIN    3   /* USR_LED2: P3.3 */
#define LED3_PORT   2
#define LED3_PIN    2   /* USR_LED3: P2.2 */

static void leds_init(void)
{
    /* Enable GPIO port 2 and 3 clocks */
    SYSCON_AHBCLKCTRL0 |= AHBCLKCTRL0_GPIO2 | AHBCLKCTRL0_GPIO3;

    /* Set LED pins as output, initially off (high = off for active-low LEDs) */
    GPIO_SET(LED1_PORT) = (1UL << LED1_PIN);
    GPIO_DIR(LED1_PORT) |= (1UL << LED1_PIN);

    GPIO_SET(LED2_PORT) = (1UL << LED2_PIN);
    GPIO_DIR(LED2_PORT) |= (1UL << LED2_PIN);

    GPIO_SET(LED3_PORT) = (1UL << LED3_PIN);
    GPIO_DIR(LED3_PORT) |= (1UL << LED3_PIN);
}

static void led_on(int port, int pin)
{
    GPIO_CLR(port) = (1UL << pin);  /* Active low */
}

static void led_off(int port, int pin)
{
    GPIO_SET(port) = (1UL << pin);
}

static void check_parts(uint32_t *pboot_ver, uint32_t *pupdate_ver,
    uint8_t *pboot_state, uint8_t *pupdate_state)
{
    *pboot_ver = wolfBoot_current_firmware_version();
    *pupdate_ver = wolfBoot_update_firmware_version();
    if (wolfBoot_get_partition_state(PART_BOOT, pboot_state) != 0)
        *pboot_state = IMG_STATE_NEW;
    if (wolfBoot_get_partition_state(PART_UPDATE, pupdate_state) != 0)
        *pupdate_state = IMG_STATE_NEW;

#ifdef DEBUG_UART
    print_str("    boot:   ver=");
    print_hex32(*pboot_ver);
    print_str(" state=");
    print_hex32(*pboot_state);
    print_str("\n    update: ver=");
    print_hex32(*pupdate_ver);
    print_str(" state=");
    print_hex32(*pupdate_state);
    print_str("\n");
#endif
}

void main(void)
{
    uint32_t boot_ver, update_ver;
    uint8_t boot_state, update_state;

    leds_init();

    boot_ver = wolfBoot_current_firmware_version();
    update_ver = wolfBoot_update_firmware_version();
    if (wolfBoot_get_partition_state(PART_BOOT, &boot_state) != 0)
        boot_state = IMG_STATE_NEW;
    if (wolfBoot_get_partition_state(PART_UPDATE, &update_state) != 0)
        update_state = IMG_STATE_NEW;

    /* LED1 on immediately to show app is running */
    led_on(LED1_PORT, LED1_PIN);

    /* Confirm boot if state is TESTING or NEW */
    if (boot_ver != 0 &&
        (boot_state == IMG_STATE_TESTING || boot_state == IMG_STATE_NEW))
    {
        wolfBoot_success();
    }

    if (boot_ver == 1 && update_ver != 0) {
        /* Update available: LED3 on, trigger update */
        led_on(LED3_PORT, LED3_PIN);
        wolfBoot_update_trigger();
    }
    else if (boot_ver != 1) {
        /* v2+: LED2 on */
        led_on(LED2_PORT, LED2_PIN);
    }

#ifdef DEBUG_UART
    print_str("App running\n");
#endif
    while (1) {
        __asm__ volatile ("wfi");
    }
}


#include "sys/stat.h"
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int status) { _kill(status, -1); while (1) {} }
int _read(int file, char *ptr, int len)
    { (void)file; (void)ptr; (void)len; return -1; }
int _write(int file, char *ptr, int len)
    { (void)file; (void)ptr; return len; }
int _close(int file) { (void)file; return -1; }
int _isatty(int file) { (void)file; return 1; }
int _lseek(int file, int ptr, int dir)
    { (void)file; (void)ptr; (void)dir; return 0; }
int _fstat(int file, struct stat *st)
    { (void)file; st->st_mode = S_IFCHR; return 0; }
